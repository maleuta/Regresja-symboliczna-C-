"""A PySR environment pilot, not the final C++/PySR benchmark suite."""

import argparse
import csv
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import platform
import sys
import time


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path("artifacts/data"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/pysr-pilot"))
    parser.add_argument("--problem", choices=["spring", "projectile", "oscillator"], default="spring")
    parser.add_argument("--variant", choices=["clean", "noisy"], default="clean")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if args.iterations < 1 or not math.isfinite(args.timeout) or args.timeout <= 0 or args.seed < 0:
        parser.error("iterations and timeout must be positive; seed must be nonnegative")
    return args


def main():
    args = arguments()
    root = Path(__file__).resolve().parents[1]
    # Keep first-import downloads/caches local; configure before importing Julia/numpy.
    os.environ["JULIA_DEPOT_PATH"] = str(root / ".tools/julia-depot")
    os.environ["PYTHON_JULIAPKG_PROJECT"] = str(root / ".tools/julia-project")
    for variable in ["JULIA_NUM_THREADS", "OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS"]:
        os.environ[variable] = "1"
    args.output.mkdir(parents=True, exist_ok=True)

    print("Importing PySR/Julia (first run may download and compile dependencies)...", flush=True)
    started = time.perf_counter()
    from pysr import PySRRegressor, jl
    import numpy as np

    startup_s = time.perf_counter() - started

    def load(split):
        path = args.data / f"{args.problem}_{args.variant}_{split}.csv"
        with path.open(newline="") as stream:
            reader = csv.DictReader(stream)
            names = reader.fieldnames
            if not names or len(names) < 2 or names[-1] != "y" or len(set(names)) != len(names):
                raise ValueError(f"Invalid generated dataset header: {path}")
            rows = list(reader)
            if not rows or any(set(row) != set(names) or None in row.values() for row in rows):
                raise ValueError(f"Empty or malformed dataset: {path}")
            x = np.array([[float(row[name]) for name in names[:-1]] for row in rows], dtype=np.float64)
            y = np.array([float(row["y"]) for row in rows], dtype=np.float64)
        if not np.isfinite(x).all() or not np.isfinite(y).all():
            raise ValueError(f"Nonfinite dataset values: {path}")
        return x, y, names[:-1], hashlib.sha256(path.read_bytes()).hexdigest()

    train_x, train_y, names, train_hash = load("train")
    val_x, val_y, val_names, val_hash = load("validation")
    test_x, test_y, test_names, test_hash = load("test")
    if names != val_names or names != test_names:
        raise ValueError("Feature names/order differ across splits")
    options = dict(
        binary_operators=["+", "-", "*", "/"],
        unary_operators=["cos"] if args.problem == "oscillator" else [],
        populations=1,
        population_size=32,
        ncycles_per_iteration=50,
        maxsize=20,
        maxdepth=8,
        precision=64,
        parallelism="serial",
        deterministic=True,
        random_state=args.seed,
        batching=False,
        warm_start=False,
        progress=False,
        verbosity=0,
        output_directory=str(args.output / "equations"),
    )

    print("Warming up Float64 search on a separate disposable model...", flush=True)
    started = time.perf_counter()
    warmup = PySRRegressor(**options, niterations=1, run_id="warmup")
    warmup.fit(train_x[:16], train_y[:16], variable_names=names)
    warmup_fit_s = time.perf_counter() - started

    print("Running fresh pilot search...", flush=True)
    model = PySRRegressor(**options, niterations=args.iterations,
                          timeout_in_seconds=args.timeout, run_id="pilot")
    started = time.perf_counter()
    model.fit(train_x, train_y, variable_names=names)
    search_fit_s = time.perf_counter() - started

    candidates = []
    for index, row in model.equations_.iterrows():
        prediction = np.asarray(model.predict(val_x, index=int(index)), dtype=np.float64)
        if not np.isfinite(prediction).all():
            continue
        with np.errstate(over="ignore", invalid="ignore"):
            error = float(np.mean((prediction - val_y) ** 2))
        if math.isfinite(error):
            candidates.append((error, int(row["complexity"]), int(index)))
    if not candidates:
        raise RuntimeError("No candidate has finite validation predictions")
    validation_mse, complexity, index = min(candidates)
    test_prediction = np.asarray(model.predict(test_x, index=index), dtype=np.float64)
    with np.errstate(over="ignore", invalid="ignore"):
        test_mse = float(np.mean((test_prediction - test_y) ** 2))
    if not np.isfinite(test_prediction).all() or not math.isfinite(test_mse):
        raise RuntimeError("Validation-selected formula is invalid on test data")
    selected = model.equations_.loc[index]
    model.equations_[["equation", "loss", "complexity"]].to_csv(args.output / "equations.csv", index=False)
    with (args.output / "predictions.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["target", "prediction"])
        writer.writerows(zip(test_y.tolist(), test_prediction.tolist()))
    result = dict(
        mode="environment_pilot",
        problem=args.problem,
        variant=args.variant,
        seed=args.seed,
        equation=str(selected["equation"]),
        complexity=complexity,
        train_mse=float(selected["loss"]),
        validation_mse=validation_mse,
        test_mse=test_mse,
        startup_s=startup_s,
        warmup_fit_s=warmup_fit_s,
        search_fit_s=search_fit_s,
        timing_note="search_fit_s excludes import/warmup, but includes Python fit and export overhead; not pure kernel time",
        data_sha256=dict(train=train_hash, validation=val_hash, test=test_hash),
        versions=dict(python=platform.python_version(), pysr=importlib.metadata.version("pysr"),
                      numpy=np.__version__, julia=str(jl.seval("string(VERSION)")),
                      symbolic_regression=str(jl.seval("string(Base.pkgversion(SymbolicRegression))"))),
        settings=options | dict(niterations=args.iterations, timeout_in_seconds=args.timeout),
    )
    (args.output / "result.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    # Preserve the actual Julia resolution as well as Python's committed lockfile.
    for filename in ["Project.toml", "Manifest.toml"]:
        source = root / ".tools/julia-project" / filename
        (args.output / filename).write_bytes(source.read_bytes())
    print(json.dumps({key: result[key] for key in ["equation", "complexity", "test_mse", "search_fit_s"]}, indent=2))
    print(f"Saved result.json, equations.csv and predictions.csv in {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Pilot failed: {error}", file=sys.stderr)
        sys.exit(1)

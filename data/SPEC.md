# Synthetic physical datasets

Generate all files with `build/sr_generate artifacts/data`. The optional second
argument is the base seed (default `20261019`). The generator is C++23; no data
generation is delegated to Python.

| Problem | Target `y` | Input domain and units | Binary / unary search operators |
|---|---|---|---|
| `spring` | `2.5*x + 0.75` (N), linear spring with force offset | `x` in [-2, 2] m | `+`, `-`, `*`, `/` / none |
| `projectile` | `v0*t - 4.905*t*t` (m), vertical displacement without drag | `t` in [0, 2] s; `v0` in [12, 20] m/s | `+`, `-`, `*`, `/` / none |
| `oscillator` | `1.5*cos(2*t + 0.3)` (m), harmonic displacement | `t` in [0, 4] s | `+`, `-`, `*`, `/` / `cos` |

These are idealized synthetic observations, not experimental measurements.
The projectile problem uses two features to exercise named-variable handling.

Each problem has independent train/validation/test samples of 256/128/256 rows.
For problem index `p` (0, 1, 2), split index `s` (0, 1, 2), the sampling seed is
`base_seed + 100*p + s`; the noise seed is the sampling seed plus 10000.
Input features are sampled independently and uniformly over their domains.

Clean and noisy variants use identical inputs. Only targets receive independent
Gaussian noise with sigma `0.01 * population_std(clean_train_y)`. The same sigma
is applied to all three splits; test/validation targets never determine it.
All numeric CSV values use double precision and 17 significant digits. The
target column is `y`; CSV contains no split or variant indicator as a feature.
There are 18 CSVs (3 problems × 2 variants × 3 splits), indexed by `manifest.csv`.

`mt19937_64` seeds make repeated generation deterministic for the same standard
library/build. Standard distribution implementations can differ across C++
libraries: archive the generated CSVs and manifest for cross-machine comparison.
The PySR adapter records SHA-256 hashes of the actual files it consumes.

Use train for search, validation for selecting the candidate, and test once for
the final metrics. A pilot is an environment check, not the planned 36-run
comparison. That comparison retains three search seeds, identical operators,
complexity limits, Float64, one CPU thread and equal timed budgets.

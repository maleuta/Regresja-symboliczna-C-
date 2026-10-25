# Symbolic Regression in C++23 — Project Plan

## 1. Project topic and objective

**Topic:** implementation of a symbolic regression algorithm in C++23 and comparison of its quality and performance with PySR.

The objective of the project is to develop a program that searches for interpretable mathematical expressions from tabular data. Basic evolutionary search and expression configuration will be implemented, followed by an evaluation of correctness, relationship recovery and performance. Implementation in C++ has been approved by the instructor; **C++23** is the adopted standard for this project and subsequent tasks.

## 2. Research questions

1. To what extent does the algorithm recover synthetic physical relationships from clean and noisy data?
2. How does the implementation compare with PySR in test error, expression complexity and runtime under identical computational conditions?

## 3. Project scope

The project will include:

- Evolutionary search within one population: initialization, selection, mutation and basic crossover; configurable population size, iteration count, seed and stopping rules.
- Expression trees, named variables, CSV input, expression evaluation, prediction and basic coordinate-search optimization of constants.
- Selection of unary/binary operators, registration of custom operators and loss, complexity constraints on expressions and operator arguments, and a simple fixed-structure template with one learned subtree.
- Correctness tests, performance measurements, a PySR comparison and a concise LaTeX report. The algorithm, tests and benchmark harness will be developed in C++23; Python will only run PySR. Custom functions will be exposed through a small C++ API.

Multiple populations, GPU, bindings to other languages and full PySR parity are outside scope.

The comparison will use spring force, projectile motion and harmonic oscillation, each clean and noisy: **3 functions × 2 variants × 3 seeds × 2 engines = 36 runs**, each with a **90-second search budget**. Both engines will use identical data/splits, operators, loss, complexity limits and precision on the same CPU with one thread. Startup/JIT will be separated from search; versions/configurations and failures will be retained, and test error, complexity and measured runtime will be reported. Model selection will use validation data; no speedup is assumed.

## 4. Implementation plan

Deliverables are due at the start of each meeting. A short progress presentation/report will be prepared for every class. **02.11.2026 is a day without classes**, so the next milestone after 26 October is 9 November.

| Date | Milestone and planned work | Deliverable / completion criterion |
|---|---|---|
| **12.10.2026** | **Goal and schedule.** This stage covers presentation of the problem, required features and implementation limits. The role of synthetic data and a PySR baseline in evaluation will be specified. The proposed scope, milestone dates and final presentation arrangements will be agreed with the instructor. | Schedule and scope submitted for approval; unresolved arrangements recorded. |
| **19.10.2026** | **Setup and baseline.** A minimal C++23/CMake project will be created and a PySR pilot run will verify the baseline environment. The three physical functions, noise variants and train/validation/test splits will be defined. A LaTeX outline will cover the objective, brief related work and planned evaluation. | Project builds; PySR exports a formula and error; dataset specification is recorded; report outline compiles. |
| **26.10.2026** | **Expressions and evaluation.** Formulas will be represented as trees containing named variables, constants and unary/binary operators. Implementation will cover CSV input, expression evaluation and a compact operator/loss registration API. Reference calculations and invalid-input cases will establish a tested foundation for the search. | Reference expressions match expected values; malformed data and invalid arithmetic are handled; API examples run. |
| **09.11.2026** | **Evolutionary search.** Random initialization, tournament selection, mutation and basic crossover will be implemented within one population. Population size, iteration count, seed and stopping limits will be configurable. Verification will cover preservation of valid trees by search operations and saving the best formula with its loss. | A seeded search completes under its configured stopping rules; structural tests pass; the resulting formula and loss are saved. |
| **16.11.2026** | **MVP checkpoint.** This stage covers basic coordinate-search optimization of constants and retention of the best candidate at each complexity. The search result will be connected to prediction on new data. A fixed linear expression will test coefficient optimization separately from the random search, followed by an end-to-end smoke test. | Fit/predict works; constant optimization improves the fixed expression; a repeatable seeded smoke test passes. |
| **23.11.2026** | **Customization and constraints.** Custom unary/binary operators and a custom loss will be demonstrated without modifying the search loop. Expression size/depth and operator-argument complexity limits will be enforced, including a restricted power exponent. Boundary cases and generated candidates will test that mutation and crossover respect these limits. | Each customization has a runnable example; constraint and arithmetic boundary tests pass; search produces admissible candidates. |
| **30.11.2026** | **Required features complete.** A fixed template with one learned subtree and explicit allowed-variable lists will be added. Tests will check that search changes only the permitted subtree and uses only allowed variables. The stage will conclude with completion of the benchmark harness and freezing of the datasets, configurations and implementation used for comparison. | All required features have runnable examples; tests pass; template invariants hold; both engines export comparable records. |
| **07.12.2026** | **Benchmarks and comparison.** The 36 clean/noisy comparisons and a small expression-evaluation microbenchmark will be run. The summary will cover test error, formula complexity and runtime across seeds, retaining failures and separating startup costs. These measurements will form the report's results and discussion, including unsuccessful recoveries. | All 36 runs and performance measurements are recorded; comparison table and draft results/discussion are complete. |
| **14.12.2026** | **Complete final package.** This stage covers resolution of remaining issues and completion of the concise LaTeX report, including conclusions and reproduction instructions. Verification will include a clean build, repeated checks and a rehearsal of the working demonstration. Sources, configurations, raw results and the report will be ready for submission together. | Code, tests, benchmarks, PySR comparison and report are complete; clean build and demo work; final LaTeX compiles. |
| **21.12.2026** | **Final demonstration and submission.** The demonstration will show the program running on a small dataset and explain the recovered formula and comparison results. The sources, report and experiment records will be submitted, followed by final corrections if needed. **21 December is provisional** within the final week, **21–27.12.2026**; the exact presentation date requires confirmation. | Working program demonstrated; complete project package submitted; final feedback addressed. |

**All deliverables will be complete by 14 December.** The final week is reserved for demonstration, submission and corrections.

## 5. Expected outcomes

The expected project outcomes are:

- A working C++23 program with source code, configurations, tests and examples of the required features, ready to demonstrate on synthetic data.
- Experiment records and a comparison of test error, expression complexity and runtime for the developed implementation and PySR, including failed runs and reproduction instructions.
- A compiling LaTeX report covering related work, objective, methods/data, results, discussion and conclusions, together with a demonstration of the working solution.

The results will support an assessment of the implementation's capabilities and limitations; superiority over PySR or exact recovery of every relationship is not assumed.

Sources: [Course assignment](https://home.agh.edu.pl/~thachaj/sp1/Studio%20projektowe%201.md), [AGH calendar](https://www.agh.edu.pl/studenci/harmonogram-roku-akademickiego), [PySR API](https://pysr.ai/v2.6.0/api).

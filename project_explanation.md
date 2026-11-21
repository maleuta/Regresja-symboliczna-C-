# Project explanation: symbolic regression in C++23

This document explains how the implementation works and why it is built this way.
It is meant for the authors, the supervisor and anyone continuing the project. It
complements the user-facing [README.md](README.md) (Polish), the schedule in
[SEMESTER_PLAN.md](SEMESTER_PLAN.md) and the article draft in
[report/report.tex](report/report.tex). Code fragments are quoted from the sources;
when they differ, the sources win.

Last updated: 21.11.2026 (state for the 23.11 milestone).

---

## 1. Status against the semester plan

| Date | Milestone | State | Where |
|---|---|---|---|
| 19.10 | C++23/CMake project, PySR pilot, data specification, report outline | done | `CMakeLists.txt`, `baseline/`, `data/SPEC.md`, `report/` |
| 26.10 | Expression trees, CSV, evaluation, operator/loss registry | done | `expression.*`, `dataset.*` |
| 09.11 | Evolutionary search: initialization, tournament, mutation, crossover, configuration, stopping rules, saved best formula | done (implemented for 23.11) | `search.*`, `apps/search.cpp` |
| 16.11 | MVP: coordinate-search constant optimization, best candidate per complexity, fit + predict, reproducible test | done (implemented for 23.11) | `optimize_constants`, hall of fame, `sr_search` |
| 23.11 | Custom operators and loss without changing the search loop; size, depth, argument and exponent limits; boundary tests; search produces only admissible candidates | done | `constraints.*`, `Variation`, `apps/custom_functions.cpp`, `apps/constraints.cpp`, `tests/test_search.cpp` |
| 30.11 | Fixed template with one learned subtree; explicit variable lists; benchmarking tool; freeze | next | (`SearchOptions::variables` already exists) |

The 09.11 and 16.11 milestones were not implemented on time. The 23.11
milestone depends on a working search loop, because it requires showing that
custom operators are used and limits are respected *by the search*. So all three
were implemented together.

---

## 2. Repository map

```
include/sr/dataset.hpp      src/dataset.cpp       Dataset, CSV read/write, validation
include/sr/benchmarks.hpp   src/benchmarks.cpp    three physical laws, sampling, noise
include/sr/expression.hpp   src/expression.cpp    Expression tree, FunctionRegistry, evaluate/predict/loss
include/sr/constraints.hpp  src/constraints.cpp   complexity limits and the violation() checker
include/sr/search.hpp       src/search.cpp        options, Variation, constant optimizer, fit()
apps/example.cpp            sr_example            evaluate a given spring formula
apps/generate_data.cpp      sr_generate           write the 18 CSV files + manifest
apps/custom_functions.cpp   sr_custom             custom exp/hypot/Huber, also inside the search
apps/constraints.cpp        sr_constraints        rule checks + constrained search demo
apps/search.cpp             sr_search             command-line symbolic regression
tests/test_sr.cpp           sr_tests              CSV, data, expression, custom function, loss
tests/test_search.cpp       sr_search_tests       editing, pow, constraints, variation, optimizer, search
tests/test_util.hpp                               check / near / throws helpers
baseline/run_pysr.py                              PySR pilot (Python is used only for PySR)
```

Dependencies only go downward: `search` → `constraints` → `expression` → `dataset`.
The library has no third-party C++ dependencies.

---

## 3. Data (`Dataset`)

```cpp
struct Dataset {
    std::vector<std::string> feature_names;
    std::vector<std::vector<double>> inputs;   // row-major, inputs[row][feature]
    std::vector<double> targets;
    std::string target_name = "y";
    void validate() const;                     // names, widths, finiteness
};
```

Variables are bound to columns **by name**, never by position. A formula found on
one CSV can then be evaluated on another CSV whose columns are in a different
order. `validate()` is the single gate for data correctness: every public function
that consumes a `Dataset` calls it. The CSV contract is described in the README;
the data generation protocol is in `data/SPEC.md`.

---

## 4. Expressions

### 4.1 Representation

```cpp
class Expression {
    Kind kind_;                         // constant, variable, unary, binary
    double value_;                      // constant value
    std::string name_;                  // variable or operator name
    std::vector<Expression> children_;  // 0, 1 or 2 subtrees
};
```

* **Value semantics.** Children are stored by value, so copying a tree produces a
  fully independent tree. Evolution copies trees all the time (offspring, hall of
  fame, crossover donors), and value semantics rules out aliasing bugs: an edit of
  the child can never change the parent stored in the population.
* **Factories enforce arity.** The constructor is private. `constant`,
  `variable`, `unary` and `binary` are the only ways to build a node, so a node
  always has the right number of children. `constant()` rejects NaN/Inf.
* **Operators are names.** A node stores only the operator name. The function is
  looked up in a `FunctionRegistry` at evaluation time. This is what makes custom
  operators work in the search without changing it (section 6.1).

Alternatives considered: a flat array (postfix) representation would be faster to
copy and evaluate, but much harder to edit and to explain. Shared pointers would
make copies cheap but bring aliasing. For trees of at most ~20 nodes, deep copies
are cheap enough.

### 4.2 Preorder addressing and immutable edits

Mutations and crossover must pick "a random node" and replace it. Nodes are
numbered in **preorder**: the root is 0, then the whole left subtree, then the
right subtree. For `+(*(2.5, x), 0.75)` the indices are `0:+ 1:* 2:2.5 3:x 4:0.75`.

```cpp
const Expression& subtree(std::size_t index) const;           // read node i
Expression replaced(std::size_t index, Expression replacement) const;  // copy with node i swapped
```

`subtree` walks down. At each node it skips whole children by their `size()` until
the index falls inside one of them:

```cpp
while (index != 0) {
    --index;  // step from the current node into its first child
    for (const auto& child : node->children_) {
        const std::size_t child_size = child.size();
        if (index < child_size) { node = &child; break; }
        index -= child_size;
    }
}
```

`replaced` does the same walk recursively on a copy. **Trees are never modified in
place.** Every edit returns a new tree and the original stays valid. That is why a
parent can safely remain in the population while its offspring are built.
Both operations cost O(n · depth), which is negligible for n ≤ 20.

`constants()` lists constant values in preorder. `with_constants(values)` rebuilds
the tree with new values in the same order and throws if the count differs. The
constant optimizer works on this flat vector (section 6.6).

### 4.3 Evaluation: reference interpreter and compiled prediction

`evaluate(expression, functions, names, values)` is a straightforward recursive
interpreter for one row. It checks the variable names (no duplicates, finite values)
on every call, which makes it the readable **reference implementation**.

`predict(expression, functions, dataset)` is the hot path of the search: one call per
candidate over all training rows. It first **compiles** the tree into a postfix
program, resolving every operator and variable once:

```cpp
struct Instruction {
    Expression::Kind kind;
    double value = 0;                       // constant
    std::size_t variable = 0;               // column index
    const UnaryFunction* unary = nullptr;   // resolved registry entry
    const BinaryFunction* binary = nullptr;
};
```

Then it runs the program for each row on a small reused stack. The hash lookups of
operator names and the linear search for variable names are paid once per tree,
not once per node and row. A test (`predict_matches_evaluate_tests`) checks on
300 random trees that both paths give identical values and fail on the same
inputs.

### 4.4 Errors and undefined arithmetic

Two exception types have two different meanings:

* `std::invalid_argument`: the **program** is wrong (unknown operator or variable,
  wrong arity, bad data, duplicate registration, bad options). These are bugs or
  configuration errors and must never be silently ignored.
* `std::domain_error`: the **arithmetic** is undefined for this input (division
  by zero, NaN/Inf result, overflow, negative or nonfinite loss). For a candidate
  formula this is normal. The search catches only this type and gives the
  candidate a loss of +∞, as PySR does.

Every operator result passes through `finite()`, so a custom operator that returns
Inf/NaN is handled the same way as a built-in one.

### 4.5 Function registry and `pow`

`FunctionRegistry` maps names to `std::function` objects. Built-ins:
binary `+ - * / pow`, unary `sin cos square`, losses `mse mae`.
`add_unary/add_binary/add_loss` add user functions and reject empty names, empty
functions and duplicates.

`pow` is defined only where the real power exists:

```cpp
add_binary("pow", [](double base, double exponent) {
    if (base == 0 && exponent < 0) throw std::domain_error("Zero raised to a negative power");
    if (base < 0 && std::trunc(exponent) != exponent)
        throw std::domain_error("Negative base raised to a non-integer power");
    return std::pow(base, exponent);
});
```

Overflow, e.g. `pow(10, 400)`, is caught by `finite()`. Defining `pow` without
"protected" fallbacks such as `|base|^e` keeps the found formulas physically
honest: a formula that only works because of a hidden absolute value is rejected.

---

## 5. Constraints (`constraints.hpp`)

**Complexity** is the node count `Expression::size()`, which is PySR's default
(every operator, variable and constant costs 1). Depth counts a leaf as 1.

```cpp
struct ArgumentLimit {                 // PySR: constraints={"pow": (-1, 1)}
    std::optional<std::size_t> first;  // nullopt = unlimited
    std::optional<std::size_t> second; // unary operators use only `first`
};

struct ExponentLimit {
    std::string operator_name = "pow";
    double max_abs_value = 3;   // |e| <= bound for constant exponents
    bool constant_only = true;  // exponent must be a constant leaf
    bool integer_only = false;  // constant exponent must be whole
};

struct Constraints {
    std::size_t max_size = 20;  // PySR maxsize
    std::size_t max_depth = 10; // PySR maxdepth
    std::unordered_map<std::string, ArgumentLimit> arguments;
    std::optional<ExponentLimit> exponent;
};
```

The "bounded power exponent" from the plan has two layers:
1. *Structural*: an argument limit `{"pow": {3, 1}}` limits the base to size 3 and
   the exponent to size 1 (a constant or a variable), exactly like PySR.
2. *Value*: `ExponentLimit` additionally bounds the exponent's value and can
   require an integer or a constant. PySR has no direct equivalent, but it is the
   natural reading of "complexity of power exponents" in the task statement.
   `t^7.3` is rarely physical, while `t^2` or `r^-2` are.

`violation(expression, constraints)` returns `std::optional<std::string>`: no value
means admissible, otherwise a human-readable reason, for example
`"exponent 2.5 of pow must be an integer"` or `"argument 1 of cos has size 5 > 3"`.
It checks size, then depth, then each node in preorder. A message instead of a
`bool` makes tests and demos self-explanatory and costs nothing on the success
path. `satisfies()` is the boolean shortcut.

`Constraints::validate()` rejects meaningless settings: zero sizes or depths, an
argument limit of 0 (an argument always has at least one node), and a negative or
NaN exponent bound.

---

## 6. Search (`search.hpp`, `search.cpp`)

### 6.1 Overview

The search is single-population **regularized evolution** (Real et al., also the
core of PySR/SymbolicRegression.jl) with constant optimization and a hall of fame:

```
validate options; P <- population_size random admissible trees; evaluate; record in hall of fame
repeat `iterations` times:
    repeat population_size times:                     # one "cycle" per offspring
        stop if time limit reached
        parent <- tournament(P)
        child  <- crossover(parent, tournament(P))  with prob. crossover_probability
                  mutate(parent)                    otherwise
                  (retry up to max_attempts until admissible, else copy parent)
        evaluate child (undefined arithmetic -> +inf)
        with prob. optimize_probability: optimize constants of child
        record child in hall of fame
        replace the OLDEST member of P by child
        stop if best train loss <= target_loss
    refine constants of hall-of-fame entries that have not converged
Pareto front <- hall of fame without dominated entries
model <- simplest front entry whose (validation) loss is within tolerance of the best
```

Custom operators work **without changing the loop** because the loop never
mentions an operator: `SearchOptions` holds operator and loss *names*,
`Variation` builds nodes with those names, and evaluation resolves them in the
registry. `apps/custom_functions.cpp` registers `hypot` and a Huber loss and finds
`hypot(Fy, Fx)` for the resultant of two perpendicular forces.

### 6.2 Configuration (`SearchOptions`)

| Option | Default | Meaning |
|---|---|---|
| `unary_operators`, `binary_operators` | `{}`, `{+,-,*,/}` | registry names used by the search |
| `variables` | all features | allowed variables (explicit list prepared for 30.11) |
| `loss` | `mse` | registry name of the pointwise loss |
| `constraints` | size 20, depth 10 | section 5 |
| `population_size` | 64 | members, kept constant |
| `iterations` | 40 | each creates `population_size` offspring |
| `seed` | 0 | `std::mt19937_64` seed |
| `time_limit_seconds`, `target_loss` | none | optional stopping rules |
| `tournament_size` | 8 | distinct contestants |
| `tournament_win_probability` | 0.7 | k-th best wins with p(1-p)^k |
| `crossover_probability` | 0.2 | otherwise mutation (PySR default 0.2) |
| `mutation_weights` | 2,2,2,1,3,2 | see 6.4 |
| `max_attempts` | 10 | proposals per offspring before keeping the parent |
| `initial_max_size` | 7 | initial trees and new subtrees |
| `constant_scale` | 1 | new constants ~ N(0, scale²) |
| `optimize_probability` | 0.05 | constant optimization of an offspring |
| `optimization` | 60 evals | `ConstantOptimizationOptions` |
| `selection_tolerance` | 0.05 | see 6.7 |

`validate(options, functions, train)` checks everything **before** the search starts
and throws `invalid_argument` with a precise message:
- probabilities and sizes;
- operator names exist *with the right arity* (`"sin"` listed as binary is an error);
- no duplicate names;
- the loss exists;
- every argument limit refers to an operator in the search set, and a unary operator
  has no second limit;
- the exponent operator is in the binary set;
- variables are dataset columns.

Misconfiguration is far more common than bugs in the algorithm, so it pays to
reject it loudly.

### 6.3 Generating admissible trees (`Variation::grow`)

Random trees use a "grow" method with a **size budget** instead of a depth budget:

```cpp
const bool can_unary  = !unary_.empty()  && size_budget >= 2 && depth_budget >= 2;
const bool can_binary = !binary_.empty() && size_budget >= 3 && depth_budget >= 2;
if (!can_unary && !can_binary) return random_leaf(random);
// pick an operator uniformly among eligible ones; for a binary operator split the
// remaining budget: left gets U[1, budget-2], right gets what the left did not use
```

`grow_argument` is the place where constraints enter generation:
* the child budget is reduced to the operator's `ArgumentLimit`;
* for the exponent slot of `pow` with `constant_only`, a constant is drawn directly
  from the allowed range (an integer in [-⌊k⌋, ⌊k⌋] when `integer_only`).

Generated trees are therefore **admissible by construction**. The property test
checks 400 random trees against `violation()`. The caller chooses the size budget
uniformly, which controls the size distribution of the initial population.

### 6.4 Mutations and crossover

| Mutation | Effect | Applicable when |
|---|---|---|
| `perturb_constant` | c ← c + N(0, (0.5·max(1,\|c\|))²) | a constant exists |
| `change_operator` | rename a node to another operator of the same arity | ≥2 operators of that arity |
| `replace_leaf` | a leaf becomes a random variable or constant | always |
| `replace_subtree` | a node becomes a new grown subtree within the remaining size/depth | always |
| `insert_node` | wrap a node: `op(node)` or `op(node, leaf)` / `op(leaf, node)` | any operator exists |
| `delete_node` | an operator node is replaced by one of its children | an operator exists |

`crossover_once` swaps a random subtree of each parent and yields two children.
The weights favour structural growth (`insert_node`) and local edits over full
replacement, which is roughly the balance of PySR's defaults. Constants are mainly
tuned by the optimizer, not by `perturb_constant`.

**How the limits are guaranteed.** Raw edits (`mutate_once`, `crossover_once`)
may produce inadmissible trees, for example `insert_node` at max size. The public
`mutate`/`crossover` wrap them:

```cpp
for (std::size_t attempt = 0; attempt < max_attempts_; ++attempt) {
    Expression child = mutate_once(parent, mutations[distribution(random)], random);
    if (admissible(child)) { result.expression = std::move(child); return result; }
    ++result.inadmissible;
}
return result;  // empty: the search keeps a copy of the parent
```

`admissible()` = `violation()` of the constraints **plus** a symbol check (only
allowed operators with the right arity, only allowed variables). So no
inadmissible tree can enter the population, by construction and by filtering.
Both are tested. `SearchStatistics` counts rejected proposals
(`inadmissible_proposals`) and the cases where no admissible offspring was found
(`rejected_offspring`). The demo shows these numbers. Rejection with retry was
preferred over "repairing" a tree: it is simpler, unbiased, and the rejection rate
is a useful diagnostic.

### 6.5 Evaluation, selection and replacement

```cpp
const auto train_loss = [&](const Expression& expression) {
    ++statistics.loss_evaluations;
    try { return mean_loss(predict(expression, functions, train), train.targets, loss); }
    catch (const std::domain_error&) { ++statistics.failed_evaluations; return infinity; }
};
```

* **Tournament.** `tournament_size` distinct members are drawn by a partial
  Fisher–Yates shuffle and sorted by train loss. The k-th best wins with
  probability p(1−p)^k.
* **Replacement.** The child replaces the *oldest* member. Members are replaced
  in birth order, so a ring index (`oldest = (oldest + 1) % n`) implements this
  without storing ages. Aging (regularized evolution) keeps diversity: even a very
  good formula eventually leaves the population and survives only through its
  descendants and the hall of fame.

**Why p = 0.7.** With p = 0.9 the population converged prematurely on
`t·(v0 − c)`. On the projectile problem (3 seeds, 200 × 100 offspring) the setting
p = 0.9 recovered the law 0/3 times. p = 0.7 recovered it 3/3 times, with an older
version of the optimizer. This was the single most effective setting.

### 6.6 Constant optimization (coordinate search with a parabolic step)

The plan specifies coordinate search instead of PySR's BFGS: no gradients,
robust to the +∞ regions of undefined arithmetic, and easy to explain. For each
constant cᵢ in turn:

1. evaluate L(cᵢ + hᵢ) and L(cᵢ − hᵢ);
2. if the three losses are convex, evaluate the vertex of the parabola through
   them: c* = cᵢ − hᵢ (L₊ − L₋) / (2 (L₊ − 2L₀ + L₋));
3. accept the best of the (up to) four points. After a success hᵢ becomes the
   length of the accepted move; after a failure hᵢ is halved.

Stop when all steps are below `min_step · max(1, |cᵢ|)` or after `max_evaluations`.

The parabolic step matters a lot. A coefficient that enters the model linearly,
like a and b in a·x + b under MSE, has an exactly quadratic loss along its
coordinate. The vertex then lands on the optimum in one step. Pure compass search
(±h, halve on failure) stalled around 1e-9 MSE on the spring law within its
60-evaluation budget, because every refinement restarted from large steps. With the parabola, the test
`a·x + b` from (1, 0) reaches (2.5, 0.75) to 1e-9, and the search finds the spring
law with test MSE exactly 0.

The objective given by the search returns +∞ for inadmissible trees. This matters
because the optimizer can move a `pow` exponent: a move that violates
`ExponentLimit` is simply never accepted.

Where it runs:
* on a random 5 % of offspring;
* at the end of each iteration, on every hall-of-fame entry whose constants have
  not converged. An entry is refined again in later iterations while refinement
  still improves it.

### 6.7 Hall of fame, Pareto front and model selection

`hall` maps complexity → best candidate (by train loss). At the end it becomes a
**Pareto front**: entries in ascending complexity, keeping only those with strictly
lower loss than every simpler one. This matches PySR's `equations_` table.

Model selection uses the **validation** loss when validation data are given
(the plan requires it), otherwise the train loss:

```
threshold = max(min_loss · (1 + selection_tolerance), 1e-12 · loss of the simplest entry)
model     = the least complex front entry with loss <= threshold
```

Choosing the plain minimum was tried first and picked formulas of complexity
13–19 for the spring law. On noisy data a complex formula fits the noise slightly
better. On clean data, losses of 1e-30 versus 1e-20 differ only by round-off. The
relative 5 % tolerance handles the first case. The absolute floor (1e-12 of the
constant model's loss, i.e. "numerically zero") handles the second. PySR's default
`model_selection="best"` uses a related idea (losses within 1.5× of the minimum,
then the steepest log-loss drop per complexity). Ours is simpler and is enough for
now.

### 6.8 Stopping rules, statistics, determinism

`StopReason` is `iterations`, `time_limit` or `target_loss`. The time limit is
checked before every offspring, the target loss after every offspring and every
iteration. `SearchStatistics` reports completed iterations, offspring, loss
evaluations, failed (undefined) evaluations, inadmissible proposals, rejected
offspring and wall time.

Without a time limit, a run is **deterministic** for a fixed seed and standard
library. All randomness comes from one `mt19937_64`, and the iteration order is
fixed. (`std::*_distribution` implementations differ between standard libraries,
the same caveat as in `data/SPEC.md`.) `search_tests` runs the search twice and
compares the hall of fame exactly. Rerunning the 18-run grid of section 9 gave
identical results.

---

## 7. Programs

* **`sr_search [TRAIN [VALIDATION [TEST]]] [options]`**: the general tool. Without
  files it generates the clean spring problem in memory (used by CTest). It prints
  the statistics, the hall of fame and the selected model with train/validation/test
  loss. `--output DIR` writes `hall_of_fame.csv`
  (`complexity,train_loss,validation_loss,selected,expression`) and `result.json`
  (configuration, statistics, equation, losses; nonfinite numbers as `null`). The
  field names (`equation`, `complexity`, `test_loss`, `search_s`) mirror the PySR
  adapter, so the 30.11/07.12 comparison can read both. Options cover operators,
  loss, limits (`--max-size`, `--max-depth`, `--constraint OP:A[:B]`,
  `--max-exponent`, `--integer-exponent`), stopping rules and the main search
  parameters; see `sr_search --help`.
* **`sr_custom [PROJECTILE_CSV]`**: registers `exp`, `hypot` and a Huber loss,
  evaluates them, shows rejected cases (overflow, duplicate registration), and then
  runs the unchanged search with `hypot` and the Huber loss.
* **`sr_constraints [PROJECTILE_TRAIN_CSV]`**: prints the violation message for six
  hand-written trees (exponent too large, not an integer, not a constant, argument
  too large…). It then runs a constrained search on the projectile data and checks
  that every member and hall-of-fame entry is admissible; otherwise it exits with an
  error.

Example (`sr_constraints`):

```
pow(t, 2.5) -> exponent 2.5 of pow must be an integer
pow(t, 4) -> exponent 4 of pow exceeds |3|
cos(*(2, +(t, 1))) -> argument 1 of cos has size 5 > 3
...
Inadmissible proposals rejected: 1144
Admissible members and hall-of-fame entries: 69 of 69
```

---

## 8. Tests

`ctest` runs 7 targets: two test executables and five programs. The suite passes in
Debug, in Release and with AddressSanitizer + UndefinedBehaviorSanitizer
(`-DSR_SANITIZERS=ON`). It compiles without warnings under
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.

`tests/test_search.cpp` groups:

| Group | Verifies |
|---|---|
| `tree_editing_tests` | preorder indices, `replaced` at root/inner/last node, originals unchanged, out-of-range errors, constants round trip |
| `pow_tests` | integer/fractional/negative powers; undefined `(-8)^(1/3)`, `0^-1`; overflow `10^400`, `(-1e300)^2` |
| `constraint_tests` | each limit exactly at the boundary passes and one step beyond fails (size, depth, each argument of unary/binary operators, exponent value/integer/constant, nested power); invalid settings rejected |
| `option_validation_tests` | 27 invalid configurations rejected by `validate` and by `fit`, including unregistered or wrong-arity operators and limits for operators outside the set |
| `predict_matches_evaluate_tests` | compiled prediction equals the reference interpreter on 300 random trees, including failures |
| `variation_property_tests` | 10 seeds × (40 generated trees + 400 steps): every generated tree and every returned offspring is well-formed and admissible; some raw edits *are* inadmissible (the limits are actually exercised); each mutation does what its name says |
| `constant_optimization_tests` | `a·x+b` fitted to (2.5, 0.75); evaluation budget; tree without constants; forbidden (+∞) region never entered |
| `search_tests` | determinism; Pareto shape; iteration/target/time stopping rules; spring law recovered with 5 nodes and test MSE < 1e-20 on new data; CSV output |
| `constrained_search_tests` | the final population and hall of fame of a constrained search are admissible; rejections happened |
| `undefined_arithmetic_search_tests` | data contain x = 0 and the operators include `/` and `pow`: undefined candidates occur and are rejected, and x² is still found |
| `custom_operator_search_tests` | a custom `hypot`, a custom loss (call counter) and a throwing custom unary operator: the loss drives the search, `hypot` is found, domain errors are rejected |

---

## 9. Preliminary results (not the formal comparison)

18 runs of `sr_search` (3 laws × clean/noisy × seeds 0–2), each 200 iterations of
100 offspring (20 000 offspring, 1.3–4.3 s on one core). Operators `+ - * /`, plus
`cos` for the oscillator. Selection on validation, metric on test (MSE).

| Problem | Variant | Recovered (seeds) | Typical selected formula |
|---|---|---|---|
| spring | clean | 3/3 (test MSE 0 / 6e-32) | `+(0.75, *(x, 2.5))`, complexity 5 |
| spring | noisy | 3/3 (test MSE 1.09e-3 ≈ noise variance) | `2.50034·x + 0.75113`, complexity 5 |
| projectile | clean | 1/3 | `*(t, -(v0, *(t, 4.905)))`, complexity 7 |
| projectile | noisy | 2/3 | `t·(v0 − 4.90472·t)`, complexity 7 |
| oscillator | clean | 2/3 by accuracy (6e-27; 7e-12 at complexity 20) | `1.5·cos(t + (t − cos(−1.8755)))` = 1.5 cos(2t + 0.3) |
| oscillator | noisy | 1/3 clean form | `1.49893·cos(2t + 0.29915)`, complexity 8 |

Observations to carry into the report:
* For projectile the population sometimes converges to a local optimum and never
  leaves it: a 15 s run of the same seed does not help. This is the known
  weakness of a *single* population; PySR uses 31 islands with migration.
* One projectile run selected a formula with a `7.6e-8 / t` term. It is harmless
  on training and validation data but explodes on test points with t ≈ 0. A
  validation split does not protect against singularities that the split does not
  sample.
* The oscillator needs the phase as `cos(t + (t − c))`: there is no
  simplification step that would rewrite it to `cos(2t + c')`. The search pays 2
  extra nodes for it.
* With a longer budget (15 s) three of the five failed runs were recovered.

These are smoke results with a 2–4 s budget. The planned comparison uses 90 s,
36 runs and identical settings for both systems (07.12).

---

## 10. Decision log

| Decision | Alternatives | Reason |
|---|---|---|
| Trees by value, immutable edits | shared pointers, in-place edits | no aliasing between parents and offspring; trees are small |
| Operators by name + registry | virtual node classes, templates | custom operators need no change in tree or search code |
| `domain_error` → +∞ loss | protected operators (e.g. x/0 = 0) | honest formulas; same convention as PySR |
| Preorder index addressing | parent pointers, paths | one integer identifies a node; easy uniform sampling |
| Compiled postfix in `predict` | interpret the tree per row | removes name lookups from the hot loop; reference interpreter kept for tests |
| Constraints as `violation()` messages | bool checks | self-explaining tests and demos |
| Admissible-by-construction generation + filter with retry | repair invalid trees | simple and unbiased; rejection rate is a diagnostic |
| Regularized evolution (replace oldest) | replace worst, generational GA | proven in PySR; keeps diversity |
| Tournament p = 0.7 | PySR's 0.982 with 31 populations | one population needs less selection pressure (6.5) |
| Coordinate search with parabolic step | BFGS (PySR), Nelder–Mead | required by the plan; derivative-free; exact for linear coefficients |
| Validation selection with tolerance and round-off floor | plain minimum, PySR "best" score | plain minimum overfits; simple rule is enough so far |
| No simulated annealing, no adaptive parsimony, no simplification | as in PySR | outside the agreed scope for now; candidates for later work |

---

## 11. Known limitations and next steps

* Single population, no migration, no annealing, no algebraic simplification
  (see section 9 for their visible effects).
* `selection_tolerance` and the round-off floor are heuristics. The
  final comparison should report the full Pareto front as well as the selected
  model.
* Nested constraints (PySR `nested_constraints`, e.g. no `cos` inside `cos`) are
  not implemented. They were not part of the plan.
* **30.11:** a fixed template with one learned subtree, explicit allowed-variable
  lists per subtree, the benchmarking tool, and freezing data/configuration for the
  comparison. `SearchOptions::variables` and the `result.json` format are already
  in place.

#pragma once

#include "sr/constraints.hpp"
#include "sr/dataset.hpp"
#include "sr/expression.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace sr {

using Random = std::mt19937_64;

// ---------------------------------------------------------------- constant optimization

struct ConstantOptimizationOptions {
    std::size_t max_evaluations = 60;  // objective calls, including the starting point
    double initial_step = 0.5;         // first step for constant c is initial_step * max(1, |c|)
    double min_step = 1e-10;           // relative step below which a coordinate is converged
};

struct OptimizedExpression {
    Expression expression;
    double loss;
    std::size_t evaluations;
};

// Coordinate search over the constants of a fixed tree. For each constant c_i in turn it
// evaluates c_i + h_i and c_i - h_i and, when the three losses are convex, the vertex of the
// parabola through them (exact for a coefficient entering linearly under MSE). The best point
// is accepted and h_i becomes the length of the move; without improvement h_i is halved.
// The objective may return +infinity for inadmissible or undefined candidates.
OptimizedExpression optimize_constants(const Expression& expression,
                                       const std::function<double(const Expression&)>& objective,
                                       const ConstantOptimizationOptions& options = {});

// ---------------------------------------------------------------- candidates and observation

struct Candidate {
    Expression expression;
    double train_loss;
    std::optional<double> validation_loss;
    std::size_t complexity() const { return expression.size(); }
};

// One tournament: the drawn contestants (population indices, best train loss first) and
// the position of the winner among them.
struct TournamentDraw {
    std::vector<std::size_t> contestants;
    std::size_t winner = 0;
};

// One evolution cycle, reported before the child replaces the oldest member.
// References are valid only during the callback.
struct OffspringEvent {
    std::size_t iteration;           // 0-based iteration containing this cycle
    std::size_t cycle;               // 0-based offspring counter over the whole run
    std::string operation;           // mutation name, "crossover" or "parent_copy"
    const Candidate& parent;
    const Candidate* second_parent;  // crossover only
    const Candidate& proposal;       // admissible variation (or parent copy) before constant optimization
    const Candidate& child;          // after the optional constant optimization
    std::size_t inadmissible;        // proposals rejected by the constraints for this child
    bool optimized;                  // constants of the child were optimized
    std::size_t replaced;            // population index of the replaced (oldest) member
    const std::vector<Candidate>& population;     // before the replacement
    const TournamentDraw& tournament;             // selected parent
    const TournamentDraw* second_tournament;      // selected second parent, crossover only
    bool hall_of_fame_updated;                    // child became the best of its complexity
};

// State after initialization (iterations == 0) and after each completed iteration,
// plus a final snapshot when a stopping rule ends an iteration early.
struct IterationSnapshot {
    std::size_t iterations;   // completed iterations
    std::size_t offspring;    // offspring created so far
    double seconds;
    const std::vector<Candidate>& population;
    std::vector<Candidate> front;         // current Pareto front (train loss)
    std::vector<Candidate> hall_of_fame;  // best member of every complexity seen, ascending complexity
};

// Optional hooks for tracing and visualization. They consume no random numbers, so a run
// with observers gives the same result as a run without them.
struct SearchObserver {
    std::function<void(const OffspringEvent&)> on_offspring;
    std::function<void(const IterationSnapshot&)> on_iteration;
};

// ---------------------------------------------------------------- search configuration

// Relative frequencies of mutation kinds; kinds that cannot be applied to a tree are skipped.
struct MutationWeights {
    double perturb_constant = 2;
    double change_operator = 2;
    double replace_leaf = 2;
    double replace_subtree = 1;
    double insert_node = 3;
    double delete_node = 2;
};

struct SearchOptions {
    // Operator and loss names refer to FunctionRegistry entries: custom functions registered
    // there are used by the search without changing its code.
    std::vector<std::string> unary_operators;
    std::vector<std::string> binary_operators{"+", "-", "*", "/"};
    std::vector<std::string> variables;  // empty: all dataset features
    std::string loss = "mse";
    Constraints constraints;

    std::size_t population_size = 64;
    std::size_t iterations = 40;  // one iteration creates population_size offspring
    std::uint64_t seed = 0;
    std::optional<double> time_limit_seconds;  // wall-clock budget of the evolution
    std::optional<double> target_loss;         // stop once the best train loss is <= target

    std::size_t tournament_size = 8;
    double tournament_win_probability = 0.7;  // the k-th best wins with p(1-p)^k
    double crossover_probability = 0.2;
    MutationWeights mutation_weights;
    std::size_t max_attempts = 10;      // proposals per offspring before keeping the parent
    std::size_t initial_max_size = 7;   // also caps the size of a newly grown subtree
    double constant_scale = 1;          // new constants are drawn from N(0, constant_scale^2)
    double optimize_probability = 0.05; // chance to optimize constants of an offspring
    ConstantOptimizationOptions optimization;
    // Model selection: the least complex Pareto entry whose selection loss is
    // <= max(min_loss * (1 + selection_tolerance), 1e-12 * loss of the simplest entry).
    // The second term treats round-off-level losses as equal. Validation loss is used when available.
    double selection_tolerance = 0.05;
    SearchObserver observer;
};

// Throws std::invalid_argument describing the first inconsistent option.
void validate(const SearchOptions& options, const FunctionRegistry& functions, const Dataset& train);

// ---------------------------------------------------------------- variation operators

// Grows and edits trees using only the configured operators and variables. Raw edits may
// violate constraints; mutate/crossover retry and return only admissible trees.
class Variation {
public:
    enum class Mutation { perturb_constant, change_operator, replace_leaf, replace_subtree, insert_node, delete_node };

    struct Offspring {
        std::optional<Expression> expression;  // empty when every attempt was inadmissible
        std::size_t inadmissible = 0;          // rejected proposals
        std::string operation;                 // mutation name or "crossover"; empty when none
    };

    Variation(const SearchOptions& options, std::vector<std::string> variables);

    // Admissible by construction; size <= min(max_size, size_budget).
    Expression random_tree(Random& random, std::size_t size_budget) const;
    bool applicable(const Expression& expression, Mutation mutation) const;
    Expression mutate_once(const Expression& expression, Mutation mutation, Random& random) const;
    std::pair<Expression, Expression> crossover_once(const Expression& first, const Expression& second,
                                                     Random& random) const;

    Offspring mutate(const Expression& parent, Random& random) const;
    Offspring crossover(const Expression& first, const Expression& second, Random& random) const;

    // Constraints plus allowed operators (with arity) and variables.
    std::optional<std::string> violation(const Expression& expression) const;
    bool admissible(const Expression& expression) const { return !violation(expression); }

private:
    Expression grow(Random& random, std::size_t size_budget, std::size_t depth_budget) const;
    Expression grow_argument(Random& random, const std::string& op, std::size_t argument,
                             std::size_t size_budget, std::size_t depth_budget) const;
    Expression random_leaf(Random& random) const;
    Expression random_exponent(Random& random) const;
    std::optional<std::size_t> argument_limit(const std::string& op, std::size_t argument) const;

    std::vector<std::string> unary_;
    std::vector<std::string> binary_;
    std::vector<std::string> variables_;
    Constraints constraints_;
    MutationWeights weights_;
    std::size_t max_attempts_;
    std::size_t new_subtree_size_;
    double constant_scale_;
};

// ---------------------------------------------------------------- search result

enum class StopReason { iterations, time_limit, target_loss };
std::string to_string(StopReason reason);
std::string to_string(Variation::Mutation mutation);

struct SearchStatistics {
    std::size_t iterations = 0;          // completed iterations
    std::size_t offspring = 0;           // created offspring
    std::size_t loss_evaluations = 0;    // train-loss computations, including constant optimization
    std::size_t failed_evaluations = 0;  // evaluations with undefined arithmetic (loss = +inf)
    std::size_t inadmissible_proposals = 0;
    std::size_t rejected_offspring = 0;  // no admissible proposal: parent copy kept
    double seconds = 0;
    StopReason stop_reason = StopReason::iterations;
};

struct SearchResult {
    std::vector<Candidate> hall_of_fame;  // Pareto front, ascending complexity, decreasing train loss
    Candidate best;                       // selected model, see SearchOptions::selection_tolerance
    std::vector<Candidate> population;    // final population
    SearchStatistics statistics;
};

// Single-population regularized evolution. Deterministic for a fixed seed and standard library
// unless a time limit stops it. Throws std::invalid_argument for invalid options or data.
SearchResult fit(const Dataset& train, const SearchOptions& options, const FunctionRegistry& functions);
SearchResult fit(const Dataset& train, const Dataset& validation, const SearchOptions& options,
                 const FunctionRegistry& functions);

// CSV columns: complexity,train_loss,validation_loss,selected,expression (expression quoted).
void write_hall_of_fame_csv(std::ostream& output, const SearchResult& result);

} // namespace sr

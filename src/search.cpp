#include "sr/search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numeric>
#include <ostream>
#include <set>
#include <stdexcept>

namespace sr {
namespace {

constexpr double infinity = std::numeric_limits<double>::infinity();

std::size_t uniform_index(Random& random, std::size_t count) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(random);
}

std::size_t uniform_size(Random& random, std::size_t low, std::size_t high) {
    return std::uniform_int_distribution<std::size_t>(low, high)(random);
}

bool chance(Random& random, double probability) {
    return std::bernoulli_distribution(probability)(random);
}

template<class T>
const T& pick(Random& random, const std::vector<T>& items) {
    return items[uniform_index(random, items.size())];
}

bool contains(const std::vector<std::string>& items, const std::string& name) {
    return std::find(items.begin(), items.end(), name) != items.end();
}

bool is_operator(const Expression& node) {
    return node.kind() == Expression::Kind::unary || node.kind() == Expression::Kind::binary;
}

// Preorder view of a tree; position i corresponds to Expression::subtree(i).
struct NodeInfo {
    const Expression* node;
    std::size_t depth;  // root = 1
};

void collect_nodes(const Expression& node, std::size_t depth, std::vector<NodeInfo>& output) {
    output.push_back({&node, depth});
    for (const auto& child : node.children()) collect_nodes(child, depth + 1, output);
}

std::vector<NodeInfo> nodes_of(const Expression& expression) {
    std::vector<NodeInfo> output;
    collect_nodes(expression, 1, output);
    return output;
}

template<class Predicate>
std::vector<std::size_t> positions(const std::vector<NodeInfo>& nodes, Predicate predicate) {
    std::vector<std::size_t> output;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (predicate(*nodes[i].node)) output.push_back(i);
    }
    return output;
}

std::optional<std::string> symbol_violation(const Expression& node, const std::vector<std::string>& unary,
                                            const std::vector<std::string>& binary,
                                            const std::vector<std::string>& variables) {
    switch (node.kind()) {
    case Expression::Kind::constant:
        break;
    case Expression::Kind::variable:
        if (!contains(variables, node.name())) return "variable " + node.name() + " is not allowed";
        break;
    case Expression::Kind::unary:
        if (!contains(unary, node.name())) return "unary operator " + node.name() + " is not allowed";
        break;
    case Expression::Kind::binary:
        if (!contains(binary, node.name())) return "binary operator " + node.name() + " is not allowed";
        break;
    }
    for (const auto& child : node.children()) {
        if (auto message = symbol_violation(child, unary, binary, variables)) return message;
    }
    return std::nullopt;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}

bool is_probability(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }

void require_unique(const std::vector<std::string>& names, const std::string& what) {
    std::set<std::string> seen;
    for (const auto& name : names) require(seen.insert(name).second, "Duplicate " + what + ": " + name);
}

constexpr std::array mutations{
    Variation::Mutation::perturb_constant, Variation::Mutation::change_operator,
    Variation::Mutation::replace_leaf,     Variation::Mutation::replace_subtree,
    Variation::Mutation::insert_node,      Variation::Mutation::delete_node,
};

double weight_of(const MutationWeights& weights, Variation::Mutation mutation) {
    switch (mutation) {
    case Variation::Mutation::perturb_constant: return weights.perturb_constant;
    case Variation::Mutation::change_operator: return weights.change_operator;
    case Variation::Mutation::replace_leaf: return weights.replace_leaf;
    case Variation::Mutation::replace_subtree: return weights.replace_subtree;
    case Variation::Mutation::insert_node: return weights.insert_node;
    case Variation::Mutation::delete_node: return weights.delete_node;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------- constant optimization

OptimizedExpression optimize_constants(const Expression& expression,
                                       const std::function<double(const Expression&)>& objective,
                                       const ConstantOptimizationOptions& options) {
    require(static_cast<bool>(objective), "Constant optimization needs an objective");
    require(options.max_evaluations >= 1, "Constant optimization needs at least one evaluation");
    require(std::isfinite(options.initial_step) && options.initial_step > 0 && std::isfinite(options.min_step) &&
                options.min_step > 0,
            "Constant optimization steps must be finite and positive");

    std::vector<double> values = expression.constants();
    Expression best_expression = expression;
    double best = objective(expression);
    std::size_t evaluations = 1;
    if (values.empty()) return {std::move(best_expression), best, evaluations};

    std::vector<double> steps(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) steps[i] = options.initial_step * std::max(1.0, std::abs(values[i]));

    const auto try_point = [&](std::size_t i, double value) -> double {
        if (!std::isfinite(value) || evaluations >= options.max_evaluations) return infinity;
        auto trial = values;
        trial[i] = value;
        ++evaluations;
        return objective(expression.with_constants(trial));
    };
    while (evaluations < options.max_evaluations) {
        bool active = false;
        for (std::size_t i = 0; i < values.size() && evaluations < options.max_evaluations; ++i) {
            const double scale = std::max(1.0, std::abs(values[i]));
            if (steps[i] < options.min_step * scale) continue;
            active = true;
            const double centre = values[i];
            const double up = try_point(i, centre + steps[i]);
            const double down = try_point(i, centre - steps[i]);
            double best_value = centre, best_loss = best;
            if (up < best_loss) best_value = centre + steps[i], best_loss = up;
            if (down < best_loss) best_value = centre - steps[i], best_loss = down;
            // Vertex of the parabola through (c-h, down), (c, best), (c+h, up).
            const double curvature = up - 2 * best + down;
            if (std::isfinite(curvature) && curvature > 0) {
                const double vertex = centre - steps[i] * (up - down) / (2 * curvature);
                if (vertex != centre) {
                    const double loss = try_point(i, vertex);
                    if (loss < best_loss) best_value = vertex, best_loss = loss;
                }
            }
            if (best_loss < best) {
                steps[i] = std::max(std::abs(best_value - centre), options.min_step * scale);
                values[i] = best_value;
                best = best_loss;
            } else {
                steps[i] *= 0.5;
            }
        }
        if (!active) break;
    }
    best_expression = expression.with_constants(values);
    return {std::move(best_expression), best, evaluations};
}

// ---------------------------------------------------------------- options

void validate(const SearchOptions& options, const FunctionRegistry& functions, const Dataset& train) {
    train.validate();
    require(options.population_size >= 2, "population_size must be at least 2");
    require(options.iterations >= 1, "iterations must be at least 1");
    require(options.tournament_size >= 1 && options.tournament_size <= options.population_size,
            "tournament_size must be in [1, population_size]");
    require(is_probability(options.tournament_win_probability) && options.tournament_win_probability > 0,
            "tournament_win_probability must be in (0, 1]");
    require(is_probability(options.crossover_probability), "crossover_probability must be in [0, 1]");
    require(is_probability(options.optimize_probability), "optimize_probability must be in [0, 1]");
    require(options.max_attempts >= 1, "max_attempts must be at least 1");
    require(options.initial_max_size >= 1, "initial_max_size must be at least 1");
    require(std::isfinite(options.constant_scale) && options.constant_scale > 0, "constant_scale must be positive");
    require(std::isfinite(options.selection_tolerance) && options.selection_tolerance >= 0,
            "selection_tolerance must be finite and nonnegative");
    if (options.time_limit_seconds) {
        require(std::isfinite(*options.time_limit_seconds) && *options.time_limit_seconds >= 0,
                "time_limit_seconds must be finite and nonnegative");
    }
    if (options.target_loss) {
        require(std::isfinite(*options.target_loss) && *options.target_loss >= 0,
                "target_loss must be finite and nonnegative");
    }
    double weight_sum = 0;
    for (const auto mutation : mutations) {
        const double weight = weight_of(options.mutation_weights, mutation);
        require(std::isfinite(weight) && weight >= 0, "Mutation weights must be finite and nonnegative");
        weight_sum += weight;
    }
    require(weight_sum > 0, "At least one mutation weight must be positive");
    const auto& optimization = options.optimization;
    require(optimization.max_evaluations >= 1 && std::isfinite(optimization.initial_step) &&
                optimization.initial_step > 0 && std::isfinite(optimization.min_step) && optimization.min_step > 0,
            "Constant optimization options must be positive");

    require_unique(options.unary_operators, "unary operator");
    require_unique(options.binary_operators, "binary operator");
    for (const auto& name : options.unary_operators) {
        try { functions.unary(name); }
        catch (const std::invalid_argument&) { throw std::invalid_argument("Unary operator is not registered: " + name); }
    }
    for (const auto& name : options.binary_operators) {
        try { functions.binary(name); }
        catch (const std::invalid_argument&) { throw std::invalid_argument("Binary operator is not registered: " + name); }
    }
    try { functions.loss(options.loss); }
    catch (const std::invalid_argument&) { throw std::invalid_argument("Loss is not registered: " + options.loss); }

    options.constraints.validate();
    for (const auto& [name, limit] : options.constraints.arguments) {
        if (contains(options.unary_operators, name)) {
            require(!limit.second, "Unary operator " + name + " has no second argument");
        } else {
            require(contains(options.binary_operators, name), "Argument limit for an operator outside the search set: " + name);
        }
    }
    if (const auto& exponent = options.constraints.exponent) {
        require(contains(options.binary_operators, exponent->operator_name),
                "Exponent limit for a binary operator outside the search set: " + exponent->operator_name);
    }

    require_unique(options.variables, "variable");
    for (const auto& name : options.variables) {
        require(contains(train.feature_names, name), "Variable is not a dataset feature: " + name);
    }
}

// ---------------------------------------------------------------- variation

Variation::Variation(const SearchOptions& options, std::vector<std::string> variables)
    : unary_(options.unary_operators), binary_(options.binary_operators), variables_(std::move(variables)),
      constraints_(options.constraints), weights_(options.mutation_weights), max_attempts_(options.max_attempts),
      new_subtree_size_(options.initial_max_size), constant_scale_(options.constant_scale) {
    constraints_.validate();
    require(!variables_.empty(), "Search needs at least one variable");
    require(max_attempts_ >= 1 && new_subtree_size_ >= 1, "max_attempts and initial_max_size must be positive");
    require(std::isfinite(constant_scale_) && constant_scale_ > 0, "constant_scale must be positive");
}

std::optional<std::size_t> Variation::argument_limit(const std::string& op, std::size_t argument) const {
    const auto found = constraints_.arguments.find(op);
    if (found == constraints_.arguments.end()) return std::nullopt;
    return argument == 0 ? found->second.first : found->second.second;
}

Expression Variation::random_leaf(Random& random) const {
    if (chance(random, 0.5)) return Expression::variable(pick(random, variables_));
    return Expression::constant(std::normal_distribution<double>(0, constant_scale_)(random));
}

Expression Variation::random_exponent(Random& random) const {
    const auto& limit = *constraints_.exponent;
    if (limit.integer_only) {
        const auto bound = static_cast<long long>(std::floor(limit.max_abs_value));
        return Expression::constant(static_cast<double>(std::uniform_int_distribution<long long>(-bound, bound)(random)));
    }
    if (limit.max_abs_value == 0) return Expression::constant(0);
    return Expression::constant(std::uniform_real_distribution<double>(-limit.max_abs_value, limit.max_abs_value)(random));
}

Expression Variation::grow_argument(Random& random, const std::string& op, std::size_t argument,
                                    std::size_t size_budget, std::size_t depth_budget) const {
    if (const auto limit = argument_limit(op, argument)) size_budget = std::min(size_budget, *limit);
    if (argument == 1 && constraints_.exponent && op == constraints_.exponent->operator_name) {
        if (constraints_.exponent->constant_only) return random_exponent(random);
        Expression tree = grow(random, size_budget, depth_budget);
        return tree.kind() == Expression::Kind::constant ? random_exponent(random) : tree;
    }
    return grow(random, size_budget, depth_budget);
}

// "Grow" initialization with a size budget: operators are used while the budget allows, so the
// caller controls the distribution of sizes. Argument limits shrink the budget of each child.
Expression Variation::grow(Random& random, std::size_t size_budget, std::size_t depth_budget) const {
    size_budget = std::min(size_budget, constraints_.max_size);
    const bool can_unary = !unary_.empty() && size_budget >= 2 && depth_budget >= 2;
    const bool can_binary = !binary_.empty() && size_budget >= 3 && depth_budget >= 2;
    if (!can_unary && !can_binary) return random_leaf(random);

    const std::size_t unary_count = can_unary ? unary_.size() : 0;
    const std::size_t choice = uniform_index(random, unary_count + (can_binary ? binary_.size() : 0));
    if (choice < unary_count) {
        const auto& op = unary_[choice];
        return Expression::unary(op, grow_argument(random, op, 0, size_budget - 1, depth_budget - 1));
    }
    const auto& op = binary_[choice - unary_count];
    const std::size_t children_budget = size_budget - 1;
    const std::size_t left_budget = uniform_size(random, 1, children_budget - 1);
    auto left = grow_argument(random, op, 0, left_budget, depth_budget - 1);
    auto right = grow_argument(random, op, 1, children_budget - left.size(), depth_budget - 1);
    return Expression::binary(op, std::move(left), std::move(right));
}

Expression Variation::random_tree(Random& random, std::size_t size_budget) const {
    return grow(random, std::max<std::size_t>(size_budget, 1), constraints_.max_depth);
}

bool Variation::applicable(const Expression& expression, Mutation mutation) const {
    const auto nodes = nodes_of(expression);
    switch (mutation) {
    case Mutation::perturb_constant:
        return !positions(nodes, [](const Expression& node) { return node.kind() == Expression::Kind::constant; }).empty();
    case Mutation::change_operator:
        return !positions(nodes, [&](const Expression& node) {
                    return (node.kind() == Expression::Kind::unary && unary_.size() >= 2) ||
                           (node.kind() == Expression::Kind::binary && binary_.size() >= 2);
                }).empty();
    case Mutation::replace_leaf:
    case Mutation::replace_subtree:
        return true;
    case Mutation::insert_node:
        return !unary_.empty() || !binary_.empty();
    case Mutation::delete_node:
        return !positions(nodes, is_operator).empty();
    }
    return false;
}

Expression Variation::mutate_once(const Expression& expression, Mutation mutation, Random& random) const {
    const auto nodes = nodes_of(expression);
    auto choose = [&](auto predicate) {
        const auto candidates = positions(nodes, predicate);
        if (candidates.empty()) throw std::logic_error("Mutation is not applicable to this tree");
        return pick(random, candidates);
    };

    switch (mutation) {
    case Mutation::perturb_constant: {
        const auto index = choose([](const Expression& node) { return node.kind() == Expression::Kind::constant; });
        const double value = nodes[index].node->value();
        const double scale = 0.5 * std::max(1.0, std::abs(value));
        const double moved = value + std::normal_distribution<double>(0, scale)(random);
        return expression.replaced(index, Expression::constant(std::isfinite(moved) ? moved : value));
    }
    case Mutation::change_operator: {
        const auto index = choose([&](const Expression& node) {
            return (node.kind() == Expression::Kind::unary && unary_.size() >= 2) ||
                   (node.kind() == Expression::Kind::binary && binary_.size() >= 2);
        });
        const Expression& node = *nodes[index].node;
        const auto& names = node.kind() == Expression::Kind::unary ? unary_ : binary_;
        std::vector<std::string> alternatives;
        for (const auto& name : names) if (name != node.name()) alternatives.push_back(name);
        const auto& name = pick(random, alternatives);
        const auto& children = node.children();
        return expression.replaced(index, node.kind() == Expression::Kind::unary
                                              ? Expression::unary(name, children[0])
                                              : Expression::binary(name, children[0], children[1]));
    }
    case Mutation::replace_leaf: {
        const auto index = choose([](const Expression& node) { return node.children().empty(); });
        return expression.replaced(index, random_leaf(random));
    }
    case Mutation::replace_subtree: {
        const auto index = uniform_index(random, nodes.size());
        const std::size_t outside = expression.size() - nodes[index].node->size();
        const std::size_t available = constraints_.max_size > outside ? constraints_.max_size - outside : 1;
        const std::size_t depth = nodes[index].depth;
        const std::size_t depth_budget = constraints_.max_depth >= depth ? constraints_.max_depth - depth + 1 : 1;
        const std::size_t size = uniform_size(random, 1, std::min(available, new_subtree_size_));
        return expression.replaced(index, grow(random, size, depth_budget));
    }
    case Mutation::insert_node: {
        const auto index = uniform_index(random, nodes.size());
        const Expression& node = *nodes[index].node;
        const std::size_t choice = uniform_index(random, unary_.size() + binary_.size());
        if (choice < unary_.size()) return expression.replaced(index, Expression::unary(unary_[choice], node));
        const auto& op = binary_[choice - unary_.size()];
        if (constraints_.exponent && op == constraints_.exponent->operator_name) {
            return expression.replaced(index, Expression::binary(op, node, random_exponent(random)));
        }
        return expression.replaced(index, chance(random, 0.5) ? Expression::binary(op, node, random_leaf(random))
                                                              : Expression::binary(op, random_leaf(random), node));
    }
    case Mutation::delete_node: {
        const auto index = choose(is_operator);
        const auto& children = nodes[index].node->children();
        return expression.replaced(index, children[uniform_index(random, children.size())]);
    }
    }
    throw std::logic_error("Unknown mutation");
}

std::pair<Expression, Expression> Variation::crossover_once(const Expression& first, const Expression& second,
                                                            Random& random) const {
    const auto i = uniform_index(random, first.size());
    const auto j = uniform_index(random, second.size());
    return {first.replaced(i, second.subtree(j)), second.replaced(j, first.subtree(i))};
}

Variation::Offspring Variation::mutate(const Expression& parent, Random& random) const {
    std::vector<double> weights;
    for (const auto mutation : mutations) weights.push_back(applicable(parent, mutation) ? weight_of(weights_, mutation) : 0);
    Offspring result;
    if (std::accumulate(weights.begin(), weights.end(), 0.0) <= 0) return result;
    std::discrete_distribution<std::size_t> distribution(weights.begin(), weights.end());
    for (std::size_t attempt = 0; attempt < max_attempts_; ++attempt) {
        const auto mutation = mutations[distribution(random)];
        Expression child = mutate_once(parent, mutation, random);
        if (admissible(child)) {
            result.expression = std::move(child);
            result.operation = to_string(mutation);
            return result;
        }
        ++result.inadmissible;
    }
    return result;
}

Variation::Offspring Variation::crossover(const Expression& first, const Expression& second, Random& random) const {
    Offspring result;
    for (std::size_t attempt = 0; attempt < max_attempts_; ++attempt) {
        auto [left, right] = crossover_once(first, second, random);
        for (auto* child : {&left, &right}) {
            if (admissible(*child)) {
                result.expression = std::move(*child);
                result.operation = "crossover";
                return result;
            }
            ++result.inadmissible;
        }
    }
    return result;
}

std::optional<std::string> Variation::violation(const Expression& expression) const {
    if (auto message = symbol_violation(expression, unary_, binary_, variables_)) return message;
    return sr::violation(expression, constraints_);
}

// ---------------------------------------------------------------- search

std::string to_string(StopReason reason) {
    switch (reason) {
    case StopReason::iterations: return "iterations";
    case StopReason::time_limit: return "time_limit";
    case StopReason::target_loss: return "target_loss";
    }
    return "unknown";
}

std::string to_string(Variation::Mutation mutation) {
    switch (mutation) {
    case Variation::Mutation::perturb_constant: return "perturb_constant";
    case Variation::Mutation::change_operator: return "change_operator";
    case Variation::Mutation::replace_leaf: return "replace_leaf";
    case Variation::Mutation::replace_subtree: return "replace_subtree";
    case Variation::Mutation::insert_node: return "insert_node";
    case Variation::Mutation::delete_node: return "delete_node";
    }
    return "unknown";
}

namespace {

// Pareto front of the hall of fame: ascending complexity, strictly decreasing train loss.
std::vector<Candidate> pareto_front(const std::map<std::size_t, std::pair<Candidate, bool>>& hall) {
    std::vector<Candidate> front;
    for (const auto& [complexity, entry] : hall) {
        if (front.empty() || entry.first.train_loss < front.back().train_loss) front.push_back(entry.first);
    }
    return front;
}

SearchResult run_search(const Dataset& train, const Dataset* validation, const SearchOptions& options,
                        const FunctionRegistry& functions) {
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    validate(options, functions, train);
    const auto variables = options.variables.empty() ? train.feature_names : options.variables;
    if (validation) {
        validation->validate();
        for (const auto& name : variables) {
            require(contains(validation->feature_names, name), "Validation data lacks variable: " + name);
        }
    }
    const Variation variation(options, variables);
    const LossFunction& loss = functions.loss(options.loss);
    Random random(options.seed);
    SearchStatistics statistics;

    // Undefined arithmetic (division by zero, overflow, NaN) gives +inf instead of an error,
    // so such candidates lose every tournament and never enter the hall of fame.
    const auto train_loss = [&](const Expression& expression) {
        ++statistics.loss_evaluations;
        try {
            return mean_loss(predict(expression, functions, train), train.targets, loss);
        } catch (const std::domain_error&) {
            ++statistics.failed_evaluations;
            return infinity;
        }
    };
    const std::function<double(const Expression&)> objective = [&](const Expression& expression) {
        return variation.admissible(expression) ? train_loss(expression) : infinity;
    };
    const auto evaluated = [&](Expression expression) {
        const double value = train_loss(expression);
        return Candidate{std::move(expression), value, std::nullopt};
    };

    // Hall of fame: best train loss for each complexity; the flag marks converged constants.
    std::map<std::size_t, std::pair<Candidate, bool>> hall;
    double best_loss = infinity;
    // Returns true when the candidate became the hall-of-fame entry of its complexity.
    const auto record = [&](const Candidate& candidate) {
        if (!std::isfinite(candidate.train_loss)) return false;
        best_loss = std::min(best_loss, candidate.train_loss);
        auto [entry, inserted] = hall.try_emplace(candidate.complexity(), candidate, false);
        if (inserted) return true;
        if (candidate.train_loss >= entry->second.first.train_loss) return false;
        entry->second = {candidate, false};
        return true;
    };
    const auto target_reached = [&] { return options.target_loss && best_loss <= *options.target_loss; };
    const auto out_of_time = [&] { return options.time_limit_seconds && elapsed() >= *options.time_limit_seconds; };

    std::vector<Candidate> population;
    population.reserve(options.population_size);
    const std::size_t initial_max = std::min(options.initial_max_size, options.constraints.max_size);
    for (std::size_t i = 0; i < options.population_size; ++i) {
        population.push_back(evaluated(variation.random_tree(random, uniform_size(random, 1, initial_max))));
        record(population.back());
    }

    std::vector<std::size_t> order(population.size());
    // Fills `draw` (reported to observers) and returns the selected member.
    const auto tournament = [&](TournamentDraw& draw) -> const Candidate& {
        // Distinct contestants by a partial Fisher-Yates shuffle, then rank by train loss.
        std::iota(order.begin(), order.end(), std::size_t{0});
        for (std::size_t k = 0; k < options.tournament_size; ++k) {
            std::swap(order[k], order[k + uniform_index(random, order.size() - k)]);
        }
        const auto end = order.begin() + static_cast<std::ptrdiff_t>(options.tournament_size);
        std::stable_sort(order.begin(), end, [&](std::size_t a, std::size_t b) {
            return population[a].train_loss < population[b].train_loss;
        });
        draw.contestants.assign(order.begin(), end);
        draw.winner = options.tournament_size - 1;
        for (std::size_t k = 0; k + 1 < options.tournament_size; ++k) {
            if (chance(random, options.tournament_win_probability)) {
                draw.winner = k;
                break;
            }
        }
        return population[draw.contestants[draw.winner]];
    };
    TournamentDraw first_draw, second_draw;

    const auto& observer = options.observer;
    std::optional<std::size_t> last_snapshot;  // offspring count of the previous snapshot
    const auto snapshot = [&] {
        if (observer.on_iteration && last_snapshot != statistics.offspring) {
            last_snapshot = statistics.offspring;
            std::vector<Candidate> entries;
            for (const auto& [complexity, entry] : hall) entries.push_back(entry.first);
            observer.on_iteration(
                {statistics.iterations, statistics.offspring, elapsed(), population, pareto_front(hall), std::move(entries)});
        }
    };
    snapshot();

    std::size_t oldest = 0;  // members are replaced in birth order, so this index is the oldest one
    bool stop = false;
    if (target_reached()) {
        stop = true;
        statistics.stop_reason = StopReason::target_loss;
    }
    for (std::size_t iteration = 0; iteration < options.iterations && !stop; ++iteration) {
        for (std::size_t cycle = 0; cycle < options.population_size; ++cycle) {
            if (out_of_time()) {
                stop = true;
                statistics.stop_reason = StopReason::time_limit;
                break;
            }
            const Candidate& parent = tournament(first_draw);
            const Candidate* second_parent = nullptr;
            Variation::Offspring offspring;
            if (chance(random, options.crossover_probability)) {
                second_parent = &tournament(second_draw);
                offspring = variation.crossover(parent.expression, second_parent->expression, random);
            } else {
                offspring = variation.mutate(parent.expression, random);
            }
            statistics.inadmissible_proposals += offspring.inadmissible;
            Candidate child = offspring.expression ? evaluated(std::move(*offspring.expression)) : parent;
            if (!offspring.expression) ++statistics.rejected_offspring;
            bool optimized_child = false;
            std::optional<Candidate> proposal;  // pre-optimization child, kept only for observers
            if (chance(random, options.optimize_probability) && !child.expression.constants().empty()) {
                optimized_child = true;
                if (observer.on_offspring) proposal = child;
                auto optimized = optimize_constants(child.expression, objective, options.optimization);
                if (optimized.loss < child.train_loss) child = {std::move(optimized.expression), optimized.loss, std::nullopt};
            }
            const bool hall_updated = record(child);
            if (observer.on_offspring) {
                observer.on_offspring({iteration, statistics.offspring,
                                       offspring.expression ? offspring.operation : std::string("parent_copy"),
                                       parent, second_parent, proposal ? *proposal : child, child,
                                       offspring.inadmissible, optimized_child, oldest, population, first_draw,
                                       second_parent ? &second_draw : nullptr, hall_updated});
            }
            population[oldest] = std::move(child);
            oldest = (oldest + 1) % population.size();
            ++statistics.offspring;
            if (target_reached()) {
                stop = true;
                statistics.stop_reason = StopReason::target_loss;
                break;
            }
        }
        if (stop) {
            snapshot();
            break;
        }
        // Constants of hall-of-fame entries are refined at the end of each iteration until
        // a refinement no longer improves the loss; a replaced entry is refined again.
        for (auto& [complexity, entry] : hall) {
            if (entry.second) continue;
            auto optimized = optimize_constants(entry.first.expression, objective, options.optimization);
            const bool improved = optimized.loss < entry.first.train_loss;
            if (improved) {
                entry.first = {std::move(optimized.expression), optimized.loss, std::nullopt};
                best_loss = std::min(best_loss, optimized.loss);
            }
            entry.second = !improved;
        }
        ++statistics.iterations;
        snapshot();
        if (target_reached()) {
            stop = true;
            statistics.stop_reason = StopReason::target_loss;
        }
    }

    std::vector<Candidate> front = pareto_front(hall);
    if (front.empty()) throw std::runtime_error("Search found no candidate with a finite loss");
    if (validation) {
        for (auto& candidate : front) {
            try {
                candidate.validation_loss =
                    mean_loss(predict(candidate.expression, functions, *validation), validation->targets, loss);
            } catch (const std::domain_error&) {
                candidate.validation_loss = infinity;
            }
        }
    }
    const bool use_validation = validation && std::any_of(front.begin(), front.end(), [](const Candidate& c) {
        return std::isfinite(*c.validation_loss);
    });
    const auto selection_loss = [&](const Candidate& c) { return use_validation ? *c.validation_loss : c.train_loss; };
    double minimum = infinity;
    for (const auto& candidate : front) minimum = std::min(minimum, selection_loss(candidate));
    // Losses below 1e-12 of the simplest entry's loss are round-off: treat them as equal.
    const double threshold = std::max(minimum * (1 + options.selection_tolerance),
                                      std::isfinite(selection_loss(front.front())) ? 1e-12 * selection_loss(front.front()) : 0.0);
    const auto chosen = std::find_if(front.begin(), front.end(), [&](const Candidate& c) {
        return selection_loss(c) <= threshold;
    });

    statistics.seconds = elapsed();
    Candidate best = *chosen;
    return SearchResult{std::move(front), std::move(best), std::move(population), statistics};
}

} // namespace

SearchResult fit(const Dataset& train, const SearchOptions& options, const FunctionRegistry& functions) {
    return run_search(train, nullptr, options, functions);
}

SearchResult fit(const Dataset& train, const Dataset& validation, const SearchOptions& options,
                 const FunctionRegistry& functions) {
    return run_search(train, &validation, options, functions);
}

void write_hall_of_fame_csv(std::ostream& output, const SearchResult& result) {
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << "complexity,train_loss,validation_loss,selected,expression\n";
    const auto best = result.best.expression.to_string();
    for (const auto& candidate : result.hall_of_fame) {
        const auto text = candidate.expression.to_string();
        output << candidate.complexity() << ',' << candidate.train_loss << ',';
        if (candidate.validation_loss) output << *candidate.validation_loss;
        output << ',' << (text == best ? 1 : 0) << ",\"" << text << "\"\n";
    }
}

} // namespace sr

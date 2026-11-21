// Tests for tree editing, constraints, variation operators, constant optimization and search.
#include "sr/benchmarks.hpp"
#include "sr/constraints.hpp"
#include "sr/search.hpp"
#include "test_util.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using test::check;
using test::near;
using test::throws;
using E = sr::Expression;
using Mutation = sr::Variation::Mutation;

sr::Dataset spring(std::size_t rows, std::uint64_t seed) {
    return sr::generate_data(sr::physical_problems()[0], rows, seed);
}

// Every node has the number of children its kind requires and a nonempty name where needed.
bool well_formed(const E& expression) {
    const auto& children = expression.children();
    switch (expression.kind()) {
    case E::Kind::constant: if (!children.empty() || !std::isfinite(expression.value())) return false; break;
    case E::Kind::variable: if (!children.empty() || expression.name().empty()) return false; break;
    case E::Kind::unary: if (children.size() != 1 || expression.name().empty()) return false; break;
    case E::Kind::binary: if (children.size() != 2 || expression.name().empty()) return false; break;
    }
    for (const auto& child : children) if (!well_formed(child)) return false;
    return true;
}

bool message_contains(const std::optional<std::string>& message, const std::string& text) {
    return message && message->find(text) != std::string::npos;
}

void tree_editing_tests() {
    const auto f = E::binary("+", E::binary("*", E::constant(2.5), E::variable("x")), E::constant(0.75));
    check(f.subtree(0).name() == "+" && f.subtree(1).name() == "*" && f.subtree(3).name() == "x", "Preorder");
    near(f.subtree(2).value(), 2.5);
    near(f.subtree(4).value(), 0.75);
    throws<std::out_of_range>([&] { f.subtree(5); });
    const auto g = f.replaced(3, E::unary("square", E::variable("x")));
    check(g.to_string() == "+(*(2.5, square(x)), 0.75)", "Replace a nested node");
    check(f.to_string() == "+(*(2.5, x), 0.75)", "Editing must not change the original tree");
    check(f.replaced(0, E::constant(1)).to_string() == "1", "Replace the root");
    check(f.replaced(4, E::variable("y")).to_string() == "+(*(2.5, x), y)", "Replace the last node");
    throws<std::out_of_range>([&] { f.replaced(5, E::constant(1)); });
    check(f.constants() == std::vector<double>{2.5, 0.75}, "Constants in preorder");
    check(E::variable("x").constants().empty(), "No constants");
    check(f.with_constants(std::vector<double>{1, 2}).to_string() == "+(*(1, x), 2)", "Set constants");
    throws([&] { f.with_constants(std::vector<double>{1}); });
    throws([&] { f.with_constants(std::vector<double>{1, 2, 3}); });
    throws<std::domain_error>([&] { f.with_constants(std::vector<double>{NAN, 1}); });
}

void pow_tests() {
    sr::FunctionRegistry functions;
    const auto power = [&](double base, double exponent) {
        return sr::evaluate(E::binary("pow", E::constant(base), E::constant(exponent)), functions, {}, {});
    };
    near(power(2, 3), 8);
    near(power(-2, 2), 4);
    near(power(-2, 3), -8);
    near(power(4, 0.5), 2);
    near(power(2, -1), 0.5);
    near(power(0, 0), 1);
    near(power(0, 2), 0);
    throws<std::domain_error>([&] { power(-8, 1.0 / 3); });
    throws<std::domain_error>([&] { power(0, -1); });
    throws<std::domain_error>([&] { power(10, 400); });   // overflow to infinity
    throws<std::domain_error>([&] { power(-1e300, 2); }); // overflow of an even power
}

void constraint_tests() {
    const auto x = E::variable("x");
    const auto f = E::binary("+", E::binary("*", E::constant(2.5), x), E::constant(0.75));  // size 5, depth 3
    sr::Constraints c;
    c.max_size = 5;
    c.max_depth = 3;
    check(sr::satisfies(f, c), "Limits equal to the tree are admissible");
    c.max_size = 4;
    check(message_contains(sr::violation(f, c), "size 5 exceeds max_size 4"), "Size limit");
    c.max_size = 5;
    c.max_depth = 2;
    check(message_contains(sr::violation(f, c), "depth 3 exceeds max_depth 2"), "Depth limit");
    sr::Constraints minimal;
    minimal.max_size = 1;
    minimal.max_depth = 1;
    check(sr::satisfies(x, minimal), "Single leaf at minimal limits");

    sr::Constraints arguments;
    arguments.arguments["*"] = {std::nullopt, 1};
    arguments.arguments["/"] = {1, std::nullopt};
    arguments.arguments["square"] = {1, std::nullopt};
    const auto x_plus_1 = E::binary("+", x, E::constant(1));
    check(sr::satisfies(f, arguments), "Second argument of * has size 1");
    check(message_contains(sr::violation(E::binary("*", x, x_plus_1), arguments), "argument 2 of * has size 3 > 1"),
          "Second argument limit");
    check(sr::satisfies(E::binary("*", x_plus_1, x), arguments), "First argument of * is unlimited");
    check(message_contains(sr::violation(E::binary("/", x_plus_1, x), arguments), "argument 1 of / has size 3 > 1"),
          "First argument limit");
    check(sr::satisfies(E::unary("square", x), arguments), "Unary argument at limit");
    check(message_contains(sr::violation(E::unary("square", x_plus_1), arguments), "argument 1 of square"),
          "Unary argument limit");
    check(message_contains(sr::violation(E::binary("-", x, E::unary("square", x_plus_1)), arguments), "square"),
          "Nested violation is found");

    sr::Constraints power;
    power.exponent = sr::ExponentLimit{};  // pow, |e| <= 3, constant only
    const auto pow = [&](E base, E exponent) { return E::binary("pow", std::move(base), std::move(exponent)); };
    check(sr::satisfies(pow(x, E::constant(3)), power) && sr::satisfies(pow(x, E::constant(-3)), power),
          "Exponent at the bound");
    check(message_contains(sr::violation(pow(x, E::constant(3.5)), power), "exponent 3.5 of pow exceeds |3|"),
          "Exponent above the bound");
    check(message_contains(sr::violation(pow(x, x), power), "exponent of pow must be a constant"), "Constant exponent");
    check(message_contains(sr::violation(pow(pow(x, E::constant(2)), E::constant(4)), power), "exponent 4"),
          "Nested power");
    check(sr::satisfies(E::binary("*", x, E::constant(100)), power), "Exponent rule applies only to pow");
    power.exponent->integer_only = true;
    check(message_contains(sr::violation(pow(x, E::constant(2.5)), power), "must be an integer"), "Integer exponent");
    check(sr::satisfies(pow(x, E::constant(2)), power), "Integer exponent at bound");
    power.exponent->integer_only = false;
    power.exponent->constant_only = false;
    check(sr::satisfies(pow(x, x), power), "Variable exponent allowed when not constant_only");
    check(!sr::satisfies(pow(x, E::constant(4)), power), "Constant exponent still bounded");

    throws([] { sr::Constraints bad; bad.max_size = 0; bad.validate(); });
    throws([] { sr::Constraints bad; bad.max_depth = 0; bad.validate(); });
    throws([] { sr::Constraints bad; bad.arguments["*"] = {0, std::nullopt}; bad.validate(); });
    throws([] { sr::Constraints bad; bad.exponent = sr::ExponentLimit{.max_abs_value = -1}; bad.validate(); });
    throws([] { sr::Constraints bad; bad.exponent = sr::ExponentLimit{.max_abs_value = NAN}; bad.validate(); });
    throws([] { sr::Constraints bad; bad.exponent = sr::ExponentLimit{.operator_name = ""}; bad.validate(); });
}

void option_validation_tests() {
    const auto data = spring(32, 1);
    sr::FunctionRegistry functions;
    sr::validate(sr::SearchOptions{}, functions, data);
    const auto rejects = [&](auto edit) {
        sr::SearchOptions options;
        edit(options);
        throws([&] { sr::validate(options, functions, data); });
        throws([&] { sr::fit(data, options, functions); });
    };
    rejects([](auto& o) { o.population_size = 1; });
    rejects([](auto& o) { o.iterations = 0; });
    rejects([](auto& o) { o.tournament_size = 0; });
    rejects([](auto& o) { o.tournament_size = o.population_size + 1; });
    rejects([](auto& o) { o.tournament_win_probability = 0; });
    rejects([](auto& o) { o.tournament_win_probability = 1.5; });
    rejects([](auto& o) { o.crossover_probability = -0.1; });
    rejects([](auto& o) { o.optimize_probability = NAN; });
    rejects([](auto& o) { o.max_attempts = 0; });
    rejects([](auto& o) { o.initial_max_size = 0; });
    rejects([](auto& o) { o.constant_scale = 0; });
    rejects([](auto& o) { o.selection_tolerance = -1; });
    rejects([](auto& o) { o.time_limit_seconds = -1; });
    rejects([](auto& o) { o.target_loss = NAN; });
    rejects([](auto& o) { o.mutation_weights = {0, 0, 0, 0, 0, 0}; });
    rejects([](auto& o) { o.mutation_weights.insert_node = -1; });
    rejects([](auto& o) { o.optimization.max_evaluations = 0; });
    rejects([](auto& o) { o.unary_operators = {"tanh"}; });             // not registered
    rejects([](auto& o) { o.binary_operators = {"+", "sin"}; });        // wrong arity
    rejects([](auto& o) { o.unary_operators = {"cos", "cos"}; });       // duplicate
    rejects([](auto& o) { o.loss = "missing"; });
    rejects([](auto& o) { o.constraints.max_size = 0; });
    rejects([](auto& o) { o.constraints.arguments["pow"] = {1, 1}; });  // pow is not in the set
    rejects([](auto& o) { o.unary_operators = {"cos"}; o.constraints.arguments["cos"] = {1, 1}; });
    rejects([](auto& o) { o.constraints.exponent = sr::ExponentLimit{}; });  // pow is not in the set
    rejects([](auto& o) { o.variables = {"z"}; });
    rejects([](auto& o) { o.variables = {"x", "x"}; });
    throws([&] { sr::fit(data, sr::Dataset{{"q"}, {{1}}, {1}}, sr::SearchOptions{}, functions); });
}

void predict_matches_evaluate_tests() {
    sr::SearchOptions options;
    options.unary_operators = {"sin", "cos", "square"};
    options.binary_operators = {"+", "-", "*", "/", "pow"};
    options.constraints.exponent = sr::ExponentLimit{.constant_only = false};
    const auto data = sr::generate_data(sr::physical_problems()[1], 16, 5);
    const sr::Variation variation(options, data.feature_names);
    sr::FunctionRegistry functions;
    sr::Random random(17);
    for (int i = 0; i < 300; ++i) {
        const auto tree = variation.random_tree(random, 1 + static_cast<std::size_t>(i % 15));
        std::vector<double> expected;
        bool expected_failure = false;
        try {
            for (const auto& row : data.inputs) expected.push_back(sr::evaluate(tree, functions, data.feature_names, row));
        } catch (const std::domain_error&) {
            expected_failure = true;
        }
        try {
            const auto predicted = sr::predict(tree, functions, data);
            check(!expected_failure && predicted == expected, "Compiled prediction differs: " + tree.to_string());
        } catch (const std::domain_error&) {
            check(expected_failure, "Only undefined arithmetic may fail: " + tree.to_string());
        }
    }
}

sr::SearchOptions constrained_options() {
    sr::SearchOptions options;
    options.unary_operators = {"sin", "square"};
    options.binary_operators = {"+", "-", "*", "/", "pow"};
    options.constraints.max_size = 9;
    options.constraints.max_depth = 4;
    options.constraints.arguments["sin"] = {3, std::nullopt};
    options.constraints.arguments["*"] = {std::nullopt, 3};
    options.constraints.arguments["pow"] = {3, 1};
    options.constraints.exponent = sr::ExponentLimit{.max_abs_value = 2, .integer_only = true};
    return options;
}

void variation_property_tests() {
    const auto options = constrained_options();
    const sr::Variation variation(options, {"x", "y"});
    std::size_t raw_inadmissible = 0;
    std::size_t returned = 0;
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        sr::Random random(seed);
        std::vector<E> population;
        for (std::size_t i = 0; i < 40; ++i) {
            const auto tree = variation.random_tree(random, 1 + i % 12);
            check(well_formed(tree), "Generated tree is well formed");
            check(variation.admissible(tree), "Generated tree violates: " + variation.violation(tree).value_or(""));
            population.push_back(tree);
        }
        for (std::size_t step = 0; step < 400; ++step) {
            const auto& parent = population[step % population.size()];
            for (const auto mutation : {Mutation::perturb_constant, Mutation::change_operator, Mutation::replace_leaf,
                                        Mutation::replace_subtree, Mutation::insert_node, Mutation::delete_node}) {
                if (!variation.applicable(parent, mutation)) continue;
                const auto raw = variation.mutate_once(parent, mutation, random);
                check(well_formed(raw), "Raw mutation keeps arity");
                if (!variation.admissible(raw)) ++raw_inadmissible;
            }
            const auto offspring = step % 3 == 0
                ? variation.crossover(parent, population[(step * 7 + 1) % population.size()], random)
                : variation.mutate(parent, random);
            if (offspring.expression) {
                ++returned;
                check(well_formed(*offspring.expression), "Offspring is well formed");
                check(variation.admissible(*offspring.expression),
                      "Offspring violates: " + variation.violation(*offspring.expression).value_or(""));
                population[(step * 13) % population.size()] = *offspring.expression;
            }
        }
    }
    check(raw_inadmissible > 0, "Limits must be active: some raw edits have to violate them");
    check(returned > 1000, "Variation must usually produce admissible offspring");

    // Individual mutations change what their name says.
    const auto f = E::binary("+", E::binary("*", E::constant(2.5), E::variable("x")), E::constant(0.75));
    sr::Random random(3);
    check(variation.mutate_once(f, Mutation::delete_node, random).size() < f.size(), "delete_node shrinks");
    check(variation.mutate_once(f, Mutation::insert_node, random).size() > f.size(), "insert_node grows");
    const auto perturbed = variation.mutate_once(f, Mutation::perturb_constant, random);
    check(perturbed.size() == f.size() && perturbed.constants() != f.constants(), "perturb_constant moves a constant");
    const auto changed = variation.mutate_once(f, Mutation::change_operator, random);
    check(changed.size() == f.size() && changed.to_string() != f.to_string(), "change_operator renames a node");
    check(!variation.applicable(E::variable("x"), Mutation::delete_node), "A leaf has no node to delete");
    check(!variation.applicable(E::variable("x"), Mutation::perturb_constant), "No constant to perturb");
    throws<std::logic_error>([&] { variation.mutate_once(E::variable("x"), Mutation::delete_node, random); });
    check(message_contains(variation.violation(E::unary("cos", E::variable("x"))), "unary operator cos"),
          "Operators outside the set are inadmissible");
    check(message_contains(variation.violation(E::variable("z")), "variable z"), "Unknown variables are inadmissible");
    check(message_contains(variation.violation(E::binary("sin", E::variable("x"), E::variable("x"))),
                           "binary operator sin"), "Arity is part of admissibility");
}

void constant_optimization_tests() {
    const auto data = spring(64, 7);
    sr::FunctionRegistry functions;
    const auto mse = [&](const E& expression) {
        return sr::mean_loss(sr::predict(expression, functions, data), data.targets, functions.loss("mse"));
    };
    const auto start = E::binary("+", E::binary("*", E::constant(1), E::variable("x")), E::constant(0));
    const auto fitted = sr::optimize_constants(start, mse, {.max_evaluations = 200});
    near(fitted.expression.constants()[0], 2.5, 1e-9);
    near(fitted.expression.constants()[1], 0.75, 1e-9);
    check(fitted.loss < 1e-18 && fitted.loss < mse(start), "Coordinate search fits a linear model");
    check(fitted.evaluations <= 200, "Evaluation budget");
    near(fitted.loss, mse(fitted.expression), 0);

    const auto limited = sr::optimize_constants(start, mse, {.max_evaluations = 5});
    check(limited.evaluations <= 5 && limited.loss <= mse(start), "Small budget is respected");

    const auto leaf = sr::optimize_constants(E::variable("x"), mse);
    check(leaf.evaluations == 1 && leaf.expression.to_string() == "x", "Tree without constants");

    // The objective may forbid a region by returning +inf; the optimizer never enters it.
    const auto scaled = E::binary("*", E::constant(0.5), E::variable("x"));
    const auto bounded = sr::optimize_constants(scaled, [&](const E& e) {
        return e.constants()[0] > 1 ? std::numeric_limits<double>::infinity() : mse(e);
    });
    check(bounded.expression.constants()[0] <= 1 && std::isfinite(bounded.loss), "Forbidden region avoided");
    throws([&] { sr::optimize_constants(start, {}); });
    throws([&] { sr::optimize_constants(start, mse, {.max_evaluations = 0}); });
    throws([&] { sr::optimize_constants(start, mse, {.initial_step = 0}); });
}

void check_result_shape(const sr::SearchResult& result, const sr::Variation& variation, bool has_validation) {
    check(!result.hall_of_fame.empty(), "Hall of fame is not empty");
    for (std::size_t i = 0; i < result.hall_of_fame.size(); ++i) {
        const auto& candidate = result.hall_of_fame[i];
        check(std::isfinite(candidate.train_loss), "Finite hall-of-fame loss");
        check(candidate.validation_loss.has_value() == has_validation, "Validation loss presence");
        check(variation.admissible(candidate.expression), "Admissible hall of fame");
        if (i > 0) {
            check(candidate.complexity() > result.hall_of_fame[i - 1].complexity(), "Ascending complexity");
            check(candidate.train_loss < result.hall_of_fame[i - 1].train_loss, "Pareto front: loss decreases");
        }
    }
    for (const auto& member : result.population) check(variation.admissible(member.expression), "Admissible population");
    check(variation.admissible(result.best.expression), "Admissible selected model");
}

void search_tests() {
    const auto train = spring(128, 11), validation = spring(64, 12), test_data = spring(128, 13);
    sr::FunctionRegistry functions;
    sr::SearchOptions options;
    options.seed = 3;
    const auto first = sr::fit(train, validation, options, functions);
    const auto second = sr::fit(train, validation, options, functions);
    check(first.hall_of_fame.size() == second.hall_of_fame.size(), "Deterministic hall of fame size");
    for (std::size_t i = 0; i < first.hall_of_fame.size(); ++i) {
        check(first.hall_of_fame[i].expression.to_string() == second.hall_of_fame[i].expression.to_string() &&
              first.hall_of_fame[i].train_loss == second.hall_of_fame[i].train_loss, "Deterministic search");
    }
    const sr::Variation variation(options, train.feature_names);
    check_result_shape(first, variation, true);
    const auto& stats = first.statistics;
    check(stats.stop_reason == sr::StopReason::iterations && stats.iterations == options.iterations &&
          stats.offspring == options.iterations * options.population_size, "Iteration stopping rule");
    check(first.population.size() == options.population_size, "Population size is constant");
    check(stats.loss_evaluations >= options.population_size + stats.offspring, "Every member is evaluated");

    // Fit + predict on new data: the spring law 2.5 x + 0.75 is recovered with five nodes.
    const auto& best = first.best;
    const double test_mse = sr::mean_loss(sr::predict(best.expression, functions, test_data), test_data.targets,
                                          functions.loss("mse"));
    check(best.complexity() == 5 && test_mse < 1e-20, "Spring law recovered: " + best.expression.to_string());

    auto target = options;
    target.target_loss = 1e-3;
    const auto early = sr::fit(train, target, functions);
    check(early.statistics.stop_reason == sr::StopReason::target_loss && early.statistics.iterations < options.iterations &&
          early.best.train_loss <= 1e-3, "Target-loss stopping rule");
    check(!early.best.validation_loss, "No validation loss without validation data");

    auto timed = options;
    timed.time_limit_seconds = 0;
    const auto instant = sr::fit(train, timed, functions);
    check(instant.statistics.stop_reason == sr::StopReason::time_limit && instant.statistics.offspring == 0,
          "Time-limit stopping rule");
    check_result_shape(instant, variation, false);

    std::ostringstream csv;
    sr::write_hall_of_fame_csv(csv, first);
    const auto text = csv.str();
    check(text.starts_with("complexity,train_loss,validation_loss,selected,expression\n"), "CSV header");
    check(text.find(",1,\"" + best.expression.to_string() + "\"\n") != std::string::npos, "Selected row is marked");
}

void constrained_search_tests() {
    const auto data = sr::generate_data(sr::physical_problems()[1], 64, 21);
    sr::FunctionRegistry functions;
    auto options = constrained_options();
    options.iterations = 15;
    options.seed = 9;
    const auto result = sr::fit(data, options, functions);
    const sr::Variation variation(options, data.feature_names);
    check_result_shape(result, variation, false);
    check(result.statistics.inadmissible_proposals > 0, "Search proposes and rejects inadmissible trees");
    for (const auto& candidate : result.hall_of_fame) check(candidate.complexity() <= 9, "Hall of fame within max_size");
}

void undefined_arithmetic_search_tests() {
    // x = 0 is in the data, so x / x, pow(x, -1) and similar candidates are undefined.
    sr::Dataset data{{"x"}, {}, {}};
    for (int i = -8; i <= 8; ++i) {
        data.inputs.push_back({i / 4.0});
        data.targets.push_back(i * i / 16.0);
    }
    sr::FunctionRegistry functions;
    sr::SearchOptions options;
    options.binary_operators = {"/", "pow", "-"};
    options.constraints.exponent = sr::ExponentLimit{.max_abs_value = 2, .integer_only = true};
    options.iterations = 10;
    const auto result = sr::fit(data, options, functions);
    check(result.statistics.failed_evaluations > 0, "Undefined candidates are evaluated and rejected");
    check(std::isfinite(result.best.train_loss) && result.best.train_loss < 1e-20,
          "x^2 is found despite undefined candidates: " + result.best.expression.to_string());
}

void custom_operator_search_tests() {
    // Resultant of two perpendicular forces: |F| = hypot(Fx, Fy). Not expressible with + * alone.
    sr::Dataset data{{"Fx", "Fy"}, {}, {}};
    sr::Random random(4);
    std::uniform_real_distribution<double> component(-3, 3);
    for (int i = 0; i < 64; ++i) {
        const double fx = component(random), fy = component(random);
        data.inputs.push_back({fx, fy});
        data.targets.push_back(std::hypot(fx, fy));
    }
    sr::FunctionRegistry functions;
    functions.add_binary("hypot", [](double a, double b) { return std::hypot(a, b); });
    auto calls = std::make_shared<std::size_t>(0);
    functions.add_loss("counted_squared", [calls](double prediction, double target) {
        ++*calls;
        return (prediction - target) * (prediction - target);
    });
    functions.add_unary("checked_log", [](double x) {
        if (x <= 0) throw std::domain_error("log of a nonpositive number");
        return std::log(x);
    });
    sr::SearchOptions options;
    options.binary_operators = {"+", "*", "hypot"};
    options.unary_operators = {"checked_log"};
    options.loss = "counted_squared";
    options.iterations = 10;
    options.seed = 1;
    const auto result = sr::fit(data, options, functions);
    check(*calls > 0, "The custom loss drives the search");
    check(result.best.train_loss < 1e-20 && result.best.expression.to_string().find("hypot") != std::string::npos,
          "Custom binary operator found: " + result.best.expression.to_string());
    near(result.best.train_loss, sr::mean_loss(sr::predict(result.best.expression, functions, data), data.targets,
                                               functions.loss("counted_squared")), 0);
    check(result.statistics.failed_evaluations > 0, "Custom operator domain errors are rejected candidates");
}

} // namespace

int main() {
    try {
        tree_editing_tests();
        pow_tests();
        constraint_tests();
        option_validation_tests();
        predict_matches_evaluate_tests();
        variation_property_tests();
        constant_optimization_tests();
        search_tests();
        constrained_search_tests();
        undefined_arithmetic_search_tests();
        custom_operator_search_tests();
        std::cout << "Tree editing, power, constraint, variation, optimization and search tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}

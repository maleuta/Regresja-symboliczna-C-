#include "sr/benchmarks.hpp"
#include "sr/expression.hpp"
#include "test_util.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

using test::check;
using test::near;
using test::throws;

void csv_tests() {
    std::istringstream source(" y , t , v0\r\n1, 0.5, 12\r\n2,1,15\r\n");
    const auto dataset = sr::read_csv(source);
    check(dataset.feature_names == std::vector<std::string>{"t", "v0"}, "CSV feature ordering");
    near(dataset.inputs[0][0], 0.5);
    near(dataset.targets[1], 2);
    std::ostringstream saved;
    sr::write_csv(saved, dataset);
    std::istringstream restored(saved.str());
    const auto roundtrip = sr::read_csv(restored);
    check(roundtrip.inputs == dataset.inputs && roundtrip.targets == dataset.targets, "CSV round trip");
    for (const auto* bad : {"", "x,y\n", "x,z\n1,2\n", "x,x,y\n1,2,3\n", "x,y,y\n1,2,3\n",
                            "x,y\n1\n", "x,y\n1,2,3\n", "x,y\n1,nan\n", "x,y\ninf,1\n",
                            "x,y\n,1\n", "x,y\n1,\n", "x,y\n1,2junk\n", "x,y\n\n",
                            "\"x\",y\n1,2\n", "x,y\n1,1e999\n", "y\n1\n"}) {
        throws([&] { std::istringstream input(bad); sr::read_csv(input); });
    }
    sr::Dataset ragged{{"x"}, {{1, 2}}, {3}};
    throws([&] { ragged.validate(); });
    throws<std::runtime_error>([] { sr::read_csv(std::filesystem::path("missing-test-data.csv")); });
}

void generation_tests() {
    const auto problems = sr::physical_problems();
    check(problems.size() == 3, "Three physical problems required");
    near(problems[0].target(std::vector<double>{2}), 5.75);
    near(problems[1].target(std::vector<double>{1, 15}), 10.095);
    near(problems[2].target(std::vector<double>{0}), 1.5 * std::cos(0.3));
    for (const auto& problem : problems) {
        const auto clean = sr::generate_data(problem, 256, 42);
        const auto repeated = sr::generate_data(problem, 256, 42);
        const auto other = sr::generate_data(problem, 256, 43);
        check(clean.inputs == repeated.inputs && clean.targets == repeated.targets, "Data seed reproducibility");
        check(clean.inputs != other.inputs, "Splits must use independent seeds");
        for (const auto& row : clean.inputs) {
            for (std::size_t i = 0; i < row.size(); ++i) {
                check(row[i] >= problem.ranges[i].first && row[i] <= problem.ranges[i].second, "Sampling bounds");
            }
        }
        const double sigma = 0.01 * sr::target_standard_deviation(clean);
        const auto noisy = sr::with_noise(clean, sigma, 44);
        check(clean.inputs == noisy.inputs && clean.targets != noisy.targets, "Noise must change targets only");
        check(noisy.targets == sr::with_noise(clean, sigma, 44).targets, "Noise seed reproducibility");
        check(sr::with_noise(clean, 0, 44).targets == clean.targets, "Zero noise");
        throws([&] { sr::with_noise(clean, -1, 44); });
        throws([&] { sr::generate_data(problem, 0, 42); });
    }
    const sr::Dataset simple{{"x"}, {{0}, {1}}, {1, 3}};
    near(sr::target_standard_deviation(simple), 1);
}

void expression_tests() {
    using E = sr::Expression;
    sr::FunctionRegistry functions;
    const std::vector<std::string> names{"x"};
    const auto formula = E::binary("+", E::binary("*", E::constant(2.5), E::variable("x")), E::constant(0.75));
    near(sr::evaluate(formula, functions, names, std::vector<double>{2}), 5.75);
    near(sr::evaluate(E::constant(-3), functions, {}, {}), -3);
    check(formula.size() == 5 && formula.depth() == 3, "Tree size/depth");
    check(formula.to_string() == "+(*(2.5, x), 0.75)", "Unambiguous expression display");
    check(formula.to_tree() ==
          "binary: +\n"
          "|-- binary: *\n"
          "|   |-- constant: 2.5\n"
          "|   `-- variable: x\n"
          "`-- constant: 0.75\n", "Tree branches preserve left/right child order");
    check(E::constant(-3).to_tree() == "constant: -3\n", "Standalone constant tree");
    check(E::variable("velocity").to_tree() == "variable: velocity\n", "Standalone named variable tree");
    const auto right_branch = E::binary("/", E::variable("x"),
        E::unary("unregistered", E::binary("-", E::constant(1), E::constant(0))));
    check(right_branch.to_tree() ==
          "binary: /\n"
          "|-- variable: x\n"
          "`-- unary: unregistered\n"
          "    `-- binary: -\n"
          "        |-- constant: 1\n"
          "        `-- constant: 0\n", "Nested last branch prints without evaluating or looking up functions");
    near(sr::evaluate(formula, functions, names, std::vector<double>{2}), 5.75);
    const auto copy = formula;
    check(copy.to_string() == formula.to_string(), "Tree copy");
    const auto square = E::unary("square", E::variable("x"));
    near(sr::evaluate(square, functions, names, std::vector<double>{-3}), 9);
    near(sr::evaluate(E::unary("sin", E::constant(0)), functions, {}, {}), 0);
    near(sr::evaluate(E::binary("/", E::constant(3), E::constant(2)), functions, {}, {}), 1.5);
    const auto data = sr::generate_data(sr::physical_problems()[0], 32, 42);
    const auto predictions = sr::predict(formula, functions, data);
    near(sr::mean_loss(predictions, data.targets, functions.loss("mse")), 0);
    const auto projectile = E::binary("-", E::binary("*", E::variable("v0"), E::variable("t")),
                                    E::binary("*", E::constant(4.905), E::unary("square", E::variable("t"))));
    const std::vector<std::string> reversed_names{"v0", "t"};
    near(sr::evaluate(projectile, functions, reversed_names, std::vector<double>{15, 1}), 10.095);
    const auto oscillator = E::binary("*", E::constant(1.5), E::unary("cos", E::binary("+",
        E::binary("*", E::constant(2), E::variable("t")), E::constant(0.3))));
    const auto oscillation_data = sr::generate_data(sr::physical_problems()[2], 32, 43);
    near(sr::mean_loss(sr::predict(oscillator, functions, oscillation_data), oscillation_data.targets,
                       functions.loss("mse")), 0);
    throws([&] { functions.add_unary("cos", [](double x) { return x; }); });
    throws([&] { functions.add_unary("", [](double x) { return x; }); });
    throws([&] { functions.add_binary("empty", {}); });
    throws([&] { functions.loss("missing"); });
    throws([] { E::variable(""); });
    throws([&] { sr::evaluate(E::variable("unknown"), functions, names, std::vector<double>{1}); });
    throws([&] { sr::evaluate(formula, functions, names, {}); });
    throws([&] { sr::evaluate(formula, functions, std::vector<std::string>{"x", "x"}, std::vector<double>{1, 2}); });
    throws([&] { sr::evaluate(E::unary("unknown", E::constant(1)), functions, {}, {}); });
    throws([&] { sr::evaluate(E::unary("+", E::constant(1)), functions, {}, {}); });
    throws<std::domain_error>([] { E::constant(std::numeric_limits<double>::infinity()); });
    throws<std::domain_error>([&] { sr::evaluate(formula, functions, names, std::vector<double>{NAN}); });
    throws<std::domain_error>([&] { sr::evaluate(E::binary("/", E::constant(1), E::constant(0)), functions, {}, {}); });
    throws<std::domain_error>([&] { sr::evaluate(E::unary("square", E::constant(1e308)), functions, {}, {}); });
}

void custom_function_tests() {
    using E = sr::Expression;
    sr::FunctionRegistry functions;
    functions.add_unary("exp", [](double x) { return std::exp(x); });
    functions.add_binary("hypot", [](double a, double b) { return std::hypot(a, b); });
    functions.add_binary("checked", [](double, double b) -> double {
        if (b < 0) throw std::domain_error("negative argument");
        return b;
    });
    functions.add_loss("absolute_cubed", [](double prediction, double target) {
        return std::pow(std::abs(prediction - target), 3);
    });
    const std::vector<std::string> names{"x"};
    near(sr::evaluate(E::binary("hypot", E::variable("x"), E::constant(4)), functions, names, std::vector<double>{3}), 5);
    near(sr::evaluate(E::unary("exp", E::constant(0)), functions, {}, {}), 1);
    near(sr::evaluate(E::binary("+", E::unary("exp", E::constant(0)), E::constant(1)), functions, {}, {}), 2);
    throws<std::domain_error>([&] { sr::evaluate(E::unary("exp", E::constant(1000)), functions, {}, {}); });
    throws<std::domain_error>([&] { sr::evaluate(E::binary("checked", E::constant(0), E::constant(-1)), functions, {}, {}); });
    throws([&] { sr::evaluate(E::unary("hypot", E::constant(1)), functions, {}, {}); });
    throws([&] { sr::evaluate(E::binary("exp", E::constant(1), E::constant(1)), functions, {}, {}); });
    const std::vector<double> predictions{1, 5}, targets{2, 3};
    near(sr::mean_loss(predictions, targets, functions.loss("absolute_cubed")), 4.5);
    sr::FunctionRegistry independent;
    throws([&] { independent.unary("exp"); });
}

void loss_tests() {
    sr::FunctionRegistry functions;
    const std::vector<double> predictions{1, 5}, targets{2, 3};
    near(sr::mean_loss(predictions, targets, functions.loss("mse")), 2.5);
    near(sr::mean_loss(predictions, targets, functions.loss("mae")), 1.5);
    throws([&] { functions.add_loss("mse", [](double, double) { return 0; }); });
    throws([&] { sr::mean_loss({}, {}, functions.loss("mse")); });
    throws([&] { sr::mean_loss(predictions, std::vector<double>{1}, functions.loss("mse")); });
    throws([&] { sr::mean_loss(predictions, targets, {}); });
    throws<std::domain_error>([&] { sr::mean_loss(predictions, targets, [](double, double) { return -1; }); });
    throws<std::domain_error>([&] { sr::mean_loss(predictions, targets, [](double, double) { return NAN; }); });
    throws<std::domain_error>([&] { sr::mean_loss(std::vector<double>{NAN}, std::vector<double>{1}, functions.loss("mse")); });
    throws<std::domain_error>([&] { sr::mean_loss(std::vector<double>{1e308}, std::vector<double>{0}, functions.loss("mse")); });
}

} // namespace

int main() {
    try {
        csv_tests();
        generation_tests();
        expression_tests();
        custom_function_tests();
        loss_tests();
        std::cout << "CSV, physical data, expression, custom function and loss tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}

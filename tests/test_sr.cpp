#include "sr/benchmarks.hpp"
#include "sr/expression.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance = 1e-12) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          "Expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

template<class Exception = std::invalid_argument, class Function>
void throws(Function function) {
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception was not thrown");
}

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
    functions.add_unary("cube", [](double x) { return x * x * x; });
    functions.add_binary("offset", [](double a, double b) { return a + 2 * b; });
    const auto custom = E::binary("offset", E::unary("cube", E::variable("x")), E::constant(4));
    near(sr::evaluate(custom, functions, names, std::vector<double>{2}), 16);
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
    throws([&] { functions.add_unary("cube", [](double x) { return x; }); });
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
    functions.add_unary("sqrt", [](double x) { return std::sqrt(x); });
    throws<std::domain_error>([&] { sr::evaluate(E::unary("sqrt", E::constant(-1)), functions, {}, {}); });
}

void loss_tests() {
    sr::FunctionRegistry functions;
    const std::vector<double> predictions{1, 5}, targets{2, 3};
    near(sr::mean_loss(predictions, targets, functions.loss("mse")), 2.5);
    near(sr::mean_loss(predictions, targets, functions.loss("mae")), 1.5);
    functions.add_loss("scaled_absolute", [](double p, double t) { return 2 * std::abs(p - t); });
    near(sr::mean_loss(predictions, targets, functions.loss("scaled_absolute")), 3);
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
        loss_tests();
        std::cout << "CSV, physical data, expression and loss tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}

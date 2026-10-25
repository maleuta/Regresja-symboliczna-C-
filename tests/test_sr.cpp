#include "sr/benchmarks.hpp"

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

} // namespace

int main() {
    try {
        csv_tests();
        generation_tests();
        std::cout << "CSV and physical data tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}

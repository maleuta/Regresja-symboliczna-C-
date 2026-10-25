#include "sr/benchmarks.hpp"

#include <cmath>
#include <random>
#include <stdexcept>

namespace sr {

std::vector<PhysicalProblem> physical_problems() {
    return {
        {"spring", {"x"}, {{-2, 2}},
            [](std::span<const double> x) { return 2.5 * x[0] + 0.75; }},
        {"projectile", {"t", "v0"}, {{0, 2}, {12, 20}},
            [](std::span<const double> x) { return x[1] * x[0] - 4.905 * x[0] * x[0]; }},
        {"oscillator", {"t"}, {{0, 4}},
            [](std::span<const double> x) { return 1.5 * std::cos(2 * x[0] + 0.3); }}
    };
}

Dataset generate_data(const PhysicalProblem& problem, std::size_t rows, std::uint64_t seed) {
    if (!problem.target || problem.feature_names.empty() || problem.feature_names.size() != problem.ranges.size()) {
        throw std::invalid_argument("Invalid physical problem definition");
    }
    for (const auto& [low, high] : problem.ranges) {
        if (!std::isfinite(low) || !std::isfinite(high) || low >= high || !std::isfinite(high - low)) {
            throw std::invalid_argument("Invalid sampling interval");
        }
    }
    std::mt19937_64 random(seed);
    Dataset dataset;
    dataset.feature_names = problem.feature_names;
    for (std::size_t i = 0; i < rows; ++i) {
        std::vector<double> row;
        for (const auto& [low, high] : problem.ranges) {
            row.push_back(std::uniform_real_distribution<double>(low, high)(random));
        }
        dataset.targets.push_back(problem.target(row));
        dataset.inputs.push_back(std::move(row));
    }
    dataset.validate();
    return dataset;
}

double target_standard_deviation(const Dataset& dataset) {
    dataset.validate();
    double mean = 0;
    double squared = 0;
    for (std::size_t i = 0; i < dataset.targets.size(); ++i) {
        const double delta = dataset.targets[i] - mean;
        mean += delta / static_cast<double>(i + 1);
        squared += delta * (dataset.targets[i] - mean);
    }
    const double deviation = std::sqrt(squared / static_cast<double>(dataset.targets.size()));
    if (!std::isfinite(deviation)) throw std::domain_error("Target variance overflow");
    return deviation;
}

Dataset with_noise(const Dataset& clean, double sigma, std::uint64_t seed) {
    clean.validate();
    if (!std::isfinite(sigma) || sigma < 0) throw std::invalid_argument("Invalid noise sigma");
    Dataset noisy = clean;
    if (sigma == 0) return noisy;
    std::mt19937_64 random(seed);
    std::normal_distribution<double> noise(0, sigma);
    for (auto& target : noisy.targets) target += noise(random);
    noisy.validate();
    return noisy;
}

} // namespace sr

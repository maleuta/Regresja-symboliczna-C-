#pragma once

#include "sr/dataset.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace sr {

struct PhysicalProblem {
    std::string name;
    std::vector<std::string> feature_names;
    std::vector<std::pair<double, double>> ranges;
    std::function<double(std::span<const double>)> target;
};

std::vector<PhysicalProblem> physical_problems();
Dataset generate_data(const PhysicalProblem& problem, std::size_t rows, std::uint64_t seed);
double target_standard_deviation(const Dataset& dataset);
Dataset with_noise(const Dataset& clean, double sigma, std::uint64_t seed);

} // namespace sr

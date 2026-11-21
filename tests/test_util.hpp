#pragma once

#include <cmath>
#include <stdexcept>
#include <string>

namespace test {

inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline void near(double actual, double expected, double tolerance = 1e-12) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          "Expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

template<class Exception = std::invalid_argument, class Function>
void throws(Function function) {
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception was not thrown");
}

} // namespace test

#pragma once

#include "sr/expression.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace sr {

// Complexity of an expression is its node count (Expression::size), as in PySR's default.

// Maximum complexity of each operator argument, like PySR `constraints={"pow": (-1, 1)}`.
// std::nullopt means unlimited. Unary operators use only `first`.
struct ArgumentLimit {
    std::optional<std::size_t> first;
    std::optional<std::size_t> second;
};

// Additional rule for the exponent (second argument) of a power operator.
struct ExponentLimit {
    std::string operator_name = "pow";
    double max_abs_value = 3;   // bound for constant exponents: |e| <= max_abs_value
    bool constant_only = true;  // the exponent must be a constant leaf
    bool integer_only = false;  // constant exponents must be whole numbers
};

struct Constraints {
    std::size_t max_size = 20;   // PySR `maxsize`
    std::size_t max_depth = 10;  // PySR `maxdepth`; a leaf has depth 1
    std::unordered_map<std::string, ArgumentLimit> arguments;
    std::optional<ExponentLimit> exponent;

    // Rejects zero limits, a nonfinite/negative exponent bound and an empty operator name.
    void validate() const;
};

// Describes the first violated rule (root size/depth first, then nodes in preorder),
// or returns std::nullopt for an admissible tree. Operator names are not checked here.
std::optional<std::string> violation(const Expression& expression, const Constraints& constraints);
inline bool satisfies(const Expression& expression, const Constraints& constraints) {
    return !violation(expression, constraints);
}

} // namespace sr

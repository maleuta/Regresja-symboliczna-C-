#include "sr/constraints.hpp"

#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace sr {
namespace {

std::string number(double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << value;
    return output.str();
}

std::optional<std::string> argument_violation(const Expression& node, const ArgumentLimit& limit) {
    const auto& children = node.children();
    const std::optional<std::size_t> limits[] = {limit.first, limit.second};
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (limits[i] && children[i].size() > *limits[i]) {
            return "argument " + std::to_string(i + 1) + " of " + node.name() + " has size " +
                   std::to_string(children[i].size()) + " > " + std::to_string(*limits[i]);
        }
    }
    return std::nullopt;
}

std::optional<std::string> exponent_violation(const Expression& node, const ExponentLimit& limit) {
    const Expression& exponent = node.children()[1];
    if (exponent.kind() != Expression::Kind::constant) {
        if (limit.constant_only) return "exponent of " + node.name() + " must be a constant";
        return std::nullopt;
    }
    const double value = exponent.value();
    if (std::abs(value) > limit.max_abs_value) {
        return "exponent " + number(value) + " of " + node.name() + " exceeds |" +
               number(limit.max_abs_value) + "|";
    }
    if (limit.integer_only && std::trunc(value) != value) {
        return "exponent " + number(value) + " of " + node.name() + " must be an integer";
    }
    return std::nullopt;
}

std::optional<std::string> node_violation(const Expression& node, const Constraints& constraints) {
    if (node.kind() == Expression::Kind::unary || node.kind() == Expression::Kind::binary) {
        if (const auto limit = constraints.arguments.find(node.name()); limit != constraints.arguments.end()) {
            if (auto message = argument_violation(node, limit->second)) return message;
        }
        if (constraints.exponent && node.kind() == Expression::Kind::binary &&
            node.name() == constraints.exponent->operator_name) {
            if (auto message = exponent_violation(node, *constraints.exponent)) return message;
        }
    }
    for (const auto& child : node.children()) {
        if (auto message = node_violation(child, constraints)) return message;
    }
    return std::nullopt;
}

} // namespace

void Constraints::validate() const {
    if (max_size == 0 || max_depth == 0) throw std::invalid_argument("max_size and max_depth must be positive");
    for (const auto& [name, limit] : arguments) {
        if (name.empty()) throw std::invalid_argument("Argument limit needs an operator name");
        if ((limit.first && *limit.first == 0) || (limit.second && *limit.second == 0)) {
            throw std::invalid_argument("Argument limit of " + name + " must be at least 1");
        }
    }
    if (exponent) {
        if (exponent->operator_name.empty()) throw std::invalid_argument("Exponent limit needs an operator name");
        if (!std::isfinite(exponent->max_abs_value) || exponent->max_abs_value < 0) {
            throw std::invalid_argument("Exponent bound must be finite and nonnegative");
        }
    }
}

std::optional<std::string> violation(const Expression& expression, const Constraints& constraints) {
    if (const auto size = expression.size(); size > constraints.max_size) {
        return "size " + std::to_string(size) + " exceeds max_size " + std::to_string(constraints.max_size);
    }
    if (const auto depth = expression.depth(); depth > constraints.max_depth) {
        return "depth " + std::to_string(depth) + " exceeds max_depth " + std::to_string(constraints.max_depth);
    }
    return node_violation(expression, constraints);
}

} // namespace sr

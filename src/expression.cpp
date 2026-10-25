#include "sr/expression.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sr {
namespace {

double finite(double value) {
    if (!std::isfinite(value)) throw std::domain_error("Expression or loss produced a nonfinite value");
    return value;
}

void require_name(const std::string& name) {
    if (name.empty()) throw std::invalid_argument("Function or variable name cannot be empty");
}

template<class Map, class Function>
void add(Map& map, std::string name, Function function) {
    require_name(name);
    if (!function) throw std::invalid_argument("Cannot register an empty function");
    if (!map.emplace(std::move(name), std::move(function)).second) {
        throw std::invalid_argument("Function name is already registered");
    }
}

template<class Map>
const auto& lookup(const Map& map, const std::string& name) {
    const auto entry = map.find(name);
    if (entry == map.end()) throw std::invalid_argument("Unknown function: " + name);
    return entry->second;
}

double evaluate_tree(const Expression& expression, const FunctionRegistry& functions,
                     std::span<const std::string> names, std::span<const double> values) {
    switch (expression.kind()) {
    case Expression::Kind::constant:
        return expression.value();
    case Expression::Kind::variable: {
        const auto found = std::find(names.begin(), names.end(), expression.name());
        if (found == names.end()) throw std::invalid_argument("Unknown variable: " + expression.name());
        return values[static_cast<std::size_t>(found - names.begin())];
    }
    case Expression::Kind::unary:
        return finite(functions.unary(expression.name())(
            evaluate_tree(expression.children()[0], functions, names, values)));
    case Expression::Kind::binary: {
        const double left = evaluate_tree(expression.children()[0], functions, names, values);
        const double right = evaluate_tree(expression.children()[1], functions, names, values);
        return finite(functions.binary(expression.name())(left, right));
    }
    }
    throw std::logic_error("Unknown expression kind");
}

} // namespace

FunctionRegistry::FunctionRegistry() {
    add_binary("+", [](double a, double b) { return a + b; });
    add_binary("-", [](double a, double b) { return a - b; });
    add_binary("*", [](double a, double b) { return a * b; });
    add_binary("/", [](double a, double b) {
        if (b == 0) throw std::domain_error("Division by zero");
        return a / b;
    });
    add_unary("sin", [](double x) { return std::sin(x); });
    add_unary("cos", [](double x) { return std::cos(x); });
    add_unary("square", [](double x) { return x * x; });
    add_loss("mse", [](double prediction, double target) {
        const double difference = prediction - target;
        return difference * difference;
    });
    add_loss("mae", [](double prediction, double target) { return std::abs(prediction - target); });
}

void FunctionRegistry::add_unary(std::string name, UnaryFunction function) {
    add(unary_, std::move(name), std::move(function));
}
void FunctionRegistry::add_binary(std::string name, BinaryFunction function) {
    add(binary_, std::move(name), std::move(function));
}
void FunctionRegistry::add_loss(std::string name, LossFunction function) {
    add(losses_, std::move(name), std::move(function));
}
const UnaryFunction& FunctionRegistry::unary(const std::string& name) const { return lookup(unary_, name); }
const BinaryFunction& FunctionRegistry::binary(const std::string& name) const { return lookup(binary_, name); }
const LossFunction& FunctionRegistry::loss(const std::string& name) const { return lookup(losses_, name); }

Expression::Expression(Kind kind, double value, std::string name, std::vector<Expression> children)
    : kind_(kind), value_(value), name_(std::move(name)), children_(std::move(children)) {}

Expression Expression::constant(double value) { return {Kind::constant, finite(value), {}, {}}; }
Expression Expression::variable(std::string name) {
    require_name(name);
    return {Kind::variable, 0, std::move(name), {}};
}
Expression Expression::unary(std::string name, Expression child) {
    require_name(name);
    return {Kind::unary, 0, std::move(name), {std::move(child)}};
}
Expression Expression::binary(std::string name, Expression left, Expression right) {
    require_name(name);
    return {Kind::binary, 0, std::move(name), {std::move(left), std::move(right)}};
}

std::size_t Expression::size() const {
    std::size_t count = 1;
    for (const auto& child : children_) count += child.size();
    return count;
}
std::size_t Expression::depth() const {
    std::size_t child_depth = 0;
    for (const auto& child : children_) child_depth = std::max(child_depth, child.depth());
    return 1 + child_depth;
}
std::string Expression::to_string() const {
    if (kind_ == Kind::constant) {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(17) << value_;
        return output.str();
    }
    if (kind_ == Kind::variable) return name_;
    if (kind_ == Kind::unary) return name_ + "(" + children_[0].to_string() + ")";
    return name_ + "(" + children_[0].to_string() + ", " + children_[1].to_string() + ")";
}

double evaluate(const Expression& expression, const FunctionRegistry& functions,
                std::span<const std::string> names, std::span<const double> values) {
    if (names.size() != values.size()) throw std::invalid_argument("Variable/value count mismatch");
    std::set<std::string> unique;
    for (std::size_t i = 0; i < names.size(); ++i) {
        require_name(names[i]);
        if (!unique.insert(names[i]).second) throw std::invalid_argument("Duplicate variable: " + names[i]);
        finite(values[i]);
    }
    return evaluate_tree(expression, functions, names, values);
}

std::vector<double> predict(const Expression& expression, const FunctionRegistry& functions,
                            const Dataset& dataset) {
    dataset.validate();
    std::vector<double> predictions;
    predictions.reserve(dataset.inputs.size());
    for (const auto& row : dataset.inputs) predictions.push_back(evaluate(expression, functions, dataset.feature_names, row));
    return predictions;
}

double mean_loss(std::span<const double> predictions, std::span<const double> targets,
                 const LossFunction& loss) {
    if (predictions.empty() || predictions.size() != targets.size() || !loss) {
        throw std::invalid_argument("Loss requires nonempty equal-sized arrays and a callable");
    }
    double mean = 0;
    for (std::size_t i = 0; i < predictions.size(); ++i) {
        const double cost = finite(loss(finite(predictions[i]), finite(targets[i])));
        if (cost < 0) throw std::domain_error("Loss must be nonnegative");
        mean += (cost - mean) / static_cast<double>(i + 1);
    }
    return finite(mean);
}

} // namespace sr

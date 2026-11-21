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

void collect_constants(const Expression& expression, std::vector<double>& output) {
    if (expression.kind() == Expression::Kind::constant) output.push_back(expression.value());
    for (const auto& child : expression.children()) collect_constants(child, output);
}

void require_index(const Expression& expression, std::size_t index) {
    if (index >= expression.size()) throw std::out_of_range("Expression node index out of range");
}

// Postfix program with operators and variables resolved once per tree, so that predicting
// many rows does not repeat name lookups. Semantics match evaluate_tree exactly.
struct Instruction {
    Expression::Kind kind;
    double value = 0;
    std::size_t variable = 0;
    const UnaryFunction* unary = nullptr;
    const BinaryFunction* binary = nullptr;
};

void compile(const Expression& expression, const FunctionRegistry& functions,
             std::span<const std::string> names, std::vector<Instruction>& program) {
    for (const auto& child : expression.children()) compile(child, functions, names, program);
    Instruction instruction{expression.kind()};
    switch (expression.kind()) {
    case Expression::Kind::constant:
        instruction.value = expression.value();
        break;
    case Expression::Kind::variable: {
        const auto found = std::find(names.begin(), names.end(), expression.name());
        if (found == names.end()) throw std::invalid_argument("Unknown variable: " + expression.name());
        instruction.variable = static_cast<std::size_t>(found - names.begin());
        break;
    }
    case Expression::Kind::unary:
        instruction.unary = &functions.unary(expression.name());
        break;
    case Expression::Kind::binary:
        instruction.binary = &functions.binary(expression.name());
        break;
    }
    program.push_back(instruction);
}

double run(std::span<const Instruction> program, std::span<const double> row, std::vector<double>& stack) {
    stack.clear();
    for (const auto& instruction : program) {
        switch (instruction.kind) {
        case Expression::Kind::constant:
            stack.push_back(instruction.value);
            break;
        case Expression::Kind::variable:
            stack.push_back(row[instruction.variable]);
            break;
        case Expression::Kind::unary:
            stack.back() = finite((*instruction.unary)(stack.back()));
            break;
        case Expression::Kind::binary: {
            const double right = stack.back();
            stack.pop_back();
            stack.back() = finite((*instruction.binary)(stack.back(), right));
            break;
        }
        }
    }
    return stack.back();
}

void append_tree(std::ostream& output, const Expression& expression, const std::string& prefix,
                 bool root, bool last) {
    if (!root) output << prefix << (last ? "`-- " : "|-- ");
    switch (expression.kind()) {
    case Expression::Kind::constant:
        output << "constant: " << expression.to_string();
        break;
    case Expression::Kind::variable:
        output << "variable: " << expression.name();
        break;
    case Expression::Kind::unary:
        output << "unary: " << expression.name();
        break;
    case Expression::Kind::binary:
        output << "binary: " << expression.name();
        break;
    }
    output << '\n';
    const auto child_prefix = root ? std::string{} : prefix + (last ? "    " : "|   ");
    const auto& children = expression.children();
    for (std::size_t i = 0; i < children.size(); ++i) {
        append_tree(output, children[i], child_prefix, false, i + 1 == children.size());
    }
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
    // Power is defined only where the real result exists; see README for the exact domain.
    add_binary("pow", [](double base, double exponent) {
        if (base == 0 && exponent < 0) throw std::domain_error("Zero raised to a negative power");
        if (base < 0 && std::trunc(exponent) != exponent) {
            throw std::domain_error("Negative base raised to a non-integer power");
        }
        return std::pow(base, exponent);
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
const Expression& Expression::subtree(std::size_t index) const {
    require_index(*this, index);
    const Expression* node = this;
    while (index != 0) {
        --index;  // step from the current node into its first child
        for (const auto& child : node->children_) {
            const std::size_t child_size = child.size();
            if (index < child_size) {
                node = &child;
                break;
            }
            index -= child_size;
        }
    }
    return *node;
}

Expression Expression::replaced(std::size_t index, Expression replacement) const {
    require_index(*this, index);
    if (index == 0) return replacement;
    Expression copy = *this;
    std::size_t offset = 1;
    for (auto& child : copy.children_) {
        const std::size_t child_size = child.size();
        if (index < offset + child_size) {
            child = child.replaced(index - offset, std::move(replacement));
            return copy;
        }
        offset += child_size;
    }
    throw std::logic_error("Preorder index was not found");
}

std::vector<double> Expression::constants() const {
    std::vector<double> output;
    collect_constants(*this, output);
    return output;
}

Expression Expression::with_constants(std::span<const double> values) const {
    std::size_t next = 0;
    auto rebuild = [&](const auto& self, const Expression& node) -> Expression {
        if (node.kind_ == Kind::constant) {
            if (next == values.size()) throw std::invalid_argument("Too few constants for expression");
            return constant(values[next++]);
        }
        Expression copy = node;
        for (auto& child : copy.children_) child = self(self, child);
        return copy;
    };
    Expression result = rebuild(rebuild, *this);
    if (next != values.size()) throw std::invalid_argument("Too many constants for expression");
    return result;
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

std::string Expression::to_tree() const {
    std::ostringstream output;
    append_tree(output, *this, {}, true, true);
    return output.str();
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
    dataset.validate();  // checks names, row widths and finiteness once for all rows
    std::vector<double> predictions;
    predictions.reserve(dataset.inputs.size());
    std::vector<Instruction> program;
    compile(expression, functions, dataset.feature_names, program);
    std::vector<double> stack;
    stack.reserve(program.size());
    for (const auto& row : dataset.inputs) predictions.push_back(run(program, row, stack));
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

#pragma once

#include "sr/dataset.hpp"

#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace sr {

using UnaryFunction = std::function<double(double)>;
using BinaryFunction = std::function<double(double, double)>;
using LossFunction = std::function<double(double prediction, double target)>;

class FunctionRegistry {
public:
    FunctionRegistry();
    void add_unary(std::string name, UnaryFunction function);
    void add_binary(std::string name, BinaryFunction function);
    void add_loss(std::string name, LossFunction function);
    const UnaryFunction& unary(const std::string& name) const;
    const BinaryFunction& binary(const std::string& name) const;
    const LossFunction& loss(const std::string& name) const;

private:
    std::unordered_map<std::string, UnaryFunction> unary_;
    std::unordered_map<std::string, BinaryFunction> binary_;
    std::unordered_map<std::string, LossFunction> losses_;
};

// Value semantics: copying a tree creates independent children. Factories enforce arity.
class Expression {
public:
    enum class Kind { constant, variable, unary, binary };
    static Expression constant(double value);
    static Expression variable(std::string name);
    static Expression unary(std::string name, Expression child);
    static Expression binary(std::string name, Expression left, Expression right);

    Kind kind() const { return kind_; }
    double value() const { return value_; }
    const std::string& name() const { return name_; }
    const std::vector<Expression>& children() const { return children_; }
    std::size_t size() const;
    std::size_t depth() const;
    std::string to_string() const;
    // Multiline ASCII tree for terminal output and debugging; does not evaluate functions.
    std::string to_tree() const;

private:
    Expression(Kind kind, double value, std::string name, std::vector<Expression> children);
    Kind kind_;
    double value_;
    std::string name_;
    std::vector<Expression> children_;
};

// Invalid inputs/operators throw invalid_argument; nonfinite arithmetic throws domain_error.
double evaluate(const Expression& expression, const FunctionRegistry& functions,
                std::span<const std::string> variable_names, std::span<const double> values);
std::vector<double> predict(const Expression& expression, const FunctionRegistry& functions,
                            const Dataset& dataset);
double mean_loss(std::span<const double> predictions, std::span<const double> targets,
                 const LossFunction& loss);

} // namespace sr

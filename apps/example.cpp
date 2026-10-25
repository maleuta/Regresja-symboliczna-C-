#include "sr/expression.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("Usage: sr_example [SPRING_CSV]");
        std::istringstream sample("x,y\n-1,-1.75\n0,0.75\n1,3.25\n");
        const auto dataset = argc == 2 ? sr::read_csv(std::filesystem::path(argv[1])) : sr::read_csv(sample);
        sr::FunctionRegistry functions;
        functions.add_unary("bias", [](double x) { return x + 0.75; });
        functions.add_binary("scale", [](double a, double b) { return a * b; });
        functions.add_loss("custom_absolute", [](double prediction, double target) {
            return std::abs(prediction - target);
        });
        using E = sr::Expression;
        const auto formula = E::unary("bias", E::binary("scale", E::constant(2.5), E::variable("x")));
        const auto predictions = sr::predict(formula, functions, dataset);
        std::cout << "Formula (registered functions): " << formula.to_string()
                  << "\nNodes: " << formula.size() << ", depth: " << formula.depth()
                  << "\nRows: " << predictions.size()
                  << "\nMSE: " << sr::mean_loss(predictions, dataset.targets, functions.loss("mse"))
                  << "\nCustom absolute loss: "
                  << sr::mean_loss(predictions, dataset.targets, functions.loss("custom_absolute")) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

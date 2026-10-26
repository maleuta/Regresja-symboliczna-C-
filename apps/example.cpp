#include "sr/expression.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("Usage: sr_example [SPRING_CSV]");
        std::istringstream sample("x,y\n-1,-1.75\n0,0.75\n1,3.25\n");
        const auto dataset = argc == 2 ? sr::read_csv(std::filesystem::path(argv[1])) : sr::read_csv(sample);
        sr::FunctionRegistry functions;
        using E = sr::Expression;
        const auto formula = E::binary("+",
            E::binary("*", E::constant(2.5), E::variable("x")), E::constant(0.75));
        const auto predictions = sr::predict(formula, functions, dataset);
        std::cout << "Formula: " << formula.to_string()
                  << "\nTree:\n" << formula.to_tree()
                  << "\nNodes: " << formula.size() << ", depth: " << formula.depth()
                  << "\nRows: " << predictions.size()
                  << "\nMSE: " << sr::mean_loss(predictions, dataset.targets, functions.loss("mse"))
                  << "\nMAE: " << sr::mean_loss(predictions, dataset.targets, functions.loss("mae")) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

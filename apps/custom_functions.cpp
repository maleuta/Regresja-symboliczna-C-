#include "sr/expression.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("Usage: sr_custom [PROJECTILE_CSV]");
        std::istringstream sample("t,v0,y\n0.5,12,4.79\n1,15,10.12\n2,20,20.35\n");
        const auto dataset = argc == 2 ? sr::read_csv(std::filesystem::path(argv[1])) : sr::read_csv(sample);

        // User functions are registered once by name; trees refer to them like built-in operators.
        sr::FunctionRegistry functions;
        functions.add_unary("exp", [](double x) { return std::exp(x); });
        functions.add_binary("hypot", [](double a, double b) { return std::hypot(a, b); });
        functions.add_loss("huber", [](double prediction, double target) {
            constexpr double delta = 1.0;
            const double error = std::abs(prediction - target);
            return error <= delta ? 0.5 * error * error : delta * (error - 0.5 * delta);
        });
        std::cout << "Registered: unary exp, binary hypot, loss huber\n";

        using E = sr::Expression;
        const auto magnitude = E::binary("hypot", E::binary("*", E::constant(3), E::variable("x")), E::constant(4));
        const std::vector<std::string> names{"x"};
        std::cout << "\nCustom binary: " << magnitude.to_string() << "\n" << magnitude.to_tree()
                  << "x = 1 -> " << sr::evaluate(magnitude, functions, names, std::vector<double>{1}) << '\n';

        try {
            sr::evaluate(E::unary("exp", E::constant(1000)), functions, {}, {});
        } catch (const std::domain_error& error) {
            std::cout << "\nCustom unary exp(1000) rejected: " << error.what() << '\n';
        }
        try {
            functions.add_loss("huber", [](double, double) { return 0.0; });
        } catch (const std::invalid_argument& error) {
            std::cout << "Duplicate loss rejected: " << error.what() << '\n';
        }

        const auto projectile = E::binary("-", E::binary("*", E::variable("v0"), E::variable("t")),
                                          E::binary("*", E::constant(4.905), E::unary("square", E::variable("t"))));
        const auto predictions = sr::predict(projectile, functions, dataset);
        std::cout << "\nProjectile: " << projectile.to_string() << "\nRows: " << predictions.size()
                  << "\nMSE:   " << sr::mean_loss(predictions, dataset.targets, functions.loss("mse"))
                  << "\nMAE:   " << sr::mean_loss(predictions, dataset.targets, functions.loss("mae"))
                  << "\nHuber: " << sr::mean_loss(predictions, dataset.targets, functions.loss("huber")) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

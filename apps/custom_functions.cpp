#include "sr/expression.hpp"
#include "sr/search.hpp"

#include <cmath>
#include <iostream>
#include <random>
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

        // The search refers to operators and losses by name only: the custom hypot and Huber
        // loss registered above are used without any change to the search loop.
        sr::Dataset forces{{"Fx", "Fy"}, {}, {}};
        sr::Random random(2026);
        std::uniform_real_distribution<double> component(-3, 3);
        for (int i = 0; i < 128; ++i) {
            const double fx = component(random), fy = component(random);
            forces.inputs.push_back({fx, fy});
            forces.targets.push_back(std::hypot(fx, fy));  // resultant of perpendicular forces
        }
        sr::SearchOptions options;
        options.binary_operators = {"+", "-", "*", "hypot"};
        options.unary_operators = {"exp"};
        options.loss = "huber";
        options.iterations = 15;
        const auto result = sr::fit(forces, options, functions);
        std::cout << "\nSearch for |F| from (Fx, Fy) with binary [+, -, *, hypot], unary [exp], loss huber\n";
        sr::write_hall_of_fame_csv(std::cout, result);
        std::cout << "Selected: " << result.best.expression.to_string() << " (Huber loss "
                  << result.best.train_loss << ")\nUndefined candidates rejected: "
                  << result.statistics.failed_evaluations << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

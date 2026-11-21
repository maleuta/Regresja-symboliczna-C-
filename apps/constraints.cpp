// Demonstrates complexity limits: rule checks on hand-written trees and a constrained search.
#include "sr/benchmarks.hpp"
#include "sr/constraints.hpp"
#include "sr/search.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("Usage: sr_constraints [PROJECTILE_TRAIN_CSV]");
        const auto train = argc == 2 ? sr::read_csv(std::filesystem::path(argv[1]))
                                     : sr::generate_data(sr::physical_problems()[1], 256, 20261119);

        sr::SearchOptions options;
        options.binary_operators = {"+", "-", "*", "/", "pow"};
        options.unary_operators = {"cos"};
        options.constraints.max_size = 11;
        options.constraints.max_depth = 5;
        options.constraints.arguments["pow"] = {3, 1};   // like PySR constraints={"pow": (3, 1)}
        options.constraints.arguments["cos"] = {3, std::nullopt};
        options.constraints.exponent = sr::ExponentLimit{.operator_name = "pow", .max_abs_value = 3,
                                                         .constant_only = true, .integer_only = true};
        options.iterations = 60;
        options.seed = 1;

        std::cout << "Constraints: max_size 11, max_depth 5, pow arguments (3, 1), cos argument 3,\n"
                     "             pow exponent: constant integer with |e| <= 3\n\nRule checks:\n";
        using E = sr::Expression;
        const auto t = E::variable("t");
        const E examples[] = {
            E::binary("pow", t, E::constant(2)),
            E::binary("pow", t, E::constant(2.5)),
            E::binary("pow", t, E::constant(4)),
            E::binary("pow", t, t),
            E::binary("pow", E::binary("+", t, E::binary("*", t, t)), E::constant(2)),
            E::unary("cos", E::binary("*", E::constant(2), E::binary("+", t, E::constant(1)))),
        };
        for (const auto& example : examples) {
            const auto message = sr::violation(example, options.constraints);
            std::cout << "  " << example.to_string() << " -> " << (message ? *message : "admissible") << '\n';
        }

        sr::FunctionRegistry functions;
        const auto result = sr::fit(train, options, functions);
        const sr::Variation variation(options, train.feature_names);
        std::size_t admissible = 0;
        for (const auto& member : result.population) admissible += variation.admissible(member.expression);
        for (const auto& entry : result.hall_of_fame) admissible += variation.admissible(entry.expression);
        const auto& stats = result.statistics;
        std::cout << "\nConstrained search on the projectile problem (" << stats.offspring << " offspring):\n";
        sr::write_hall_of_fame_csv(std::cout, result);
        std::cout << "Selected: " << result.best.expression.to_string()
                  << "\nInadmissible proposals rejected: " << stats.inadmissible_proposals
                  << "\nOffspring kept as a parent copy: " << stats.rejected_offspring
                  << "\nAdmissible members and hall-of-fame entries: " << admissible << " of "
                  << result.population.size() + result.hall_of_fame.size() << '\n';
        if (admissible != result.population.size() + result.hall_of_fame.size()) {
            throw std::logic_error("Search produced an inadmissible candidate");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

// Command-line symbolic regression: fit on train, select on validation, report test loss.
#include "sr/benchmarks.hpp"
#include "sr/search.hpp"

#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* usage =
    "Usage: sr_search [TRAIN.csv [VALIDATION.csv [TEST.csv]]] [options]\n"
    "Without CSV files the clean spring problem is generated in memory.\n"
    "  --seed N              random seed (default 0)\n"
    "  --iterations N        iterations; each creates population-size offspring (default 40)\n"
    "  --population N        population size (default 64)\n"
    "  --time SECONDS        wall-clock limit of the evolution\n"
    "  --target LOSS         stop when the best train loss is <= LOSS\n"
    "  --unary a,b           unary operators (default: none)\n"
    "  --binary a,b          binary operators (default: +,-,*,/)\n"
    "  --loss NAME           registered loss (default mse)\n"
    "  --max-size N          maximum expression size (default 20)\n"
    "  --max-depth N         maximum expression depth (default 10)\n"
    "  --constraint OP:A[:B] maximum argument sizes of OP, -1 = unlimited\n"
    "  --max-exponent K      constant exponent of pow limited to |e| <= K\n"
    "  --integer-exponent    exponent of pow must also be an integer\n"
    "  --tournament N        tournament size (default 8)\n"
    "  --win-probability P   probability that the best contestant wins (default 0.7)\n"
    "  --crossover P         crossover probability (default 0.2)\n"
    "  --optimize P          probability of optimizing constants of an offspring (default 0.05)\n"
    "  --output DIR          write hall_of_fame.csv and result.json\n";

std::size_t to_size(std::string_view text) {
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("Expected a nonnegative integer: " + std::string(text));
    }
    return value;
}

double to_double(const std::string& text) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double value = 0;
    if (!(input >> value) || !input.eof() || !std::isfinite(value)) {
        throw std::invalid_argument("Expected a finite number: " + text);
    }
    return value;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream input(text);
    while (std::getline(input, part, separator)) parts.push_back(part);
    if (!text.empty() && text.back() == separator) parts.emplace_back();
    return parts;
}

std::optional<std::size_t> limit(const std::string& text) {
    if (text == "-1") return std::nullopt;
    return to_size(text);
}

std::string json_number(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << value;
    return output.str();
}

std::string json_string(const std::string& text) {
    std::string output = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') output += '\\';
        output += c;
    }
    return output + '"';
}

std::string join(const std::vector<std::string>& items) {
    std::string output;
    for (const auto& item : items) output += (output.empty() ? "" : ",") + item;
    return output;
}

} // namespace

int main(int argc, char** argv) {
    try {
        sr::SearchOptions options;
        std::vector<std::filesystem::path> files;
        std::optional<std::filesystem::path> output;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + argument);
                return argv[++i];
            };
            if (argument == "--help") {
                std::cout << usage;
                return 0;
            } else if (argument == "--seed") {
                options.seed = to_size(value());
            } else if (argument == "--iterations") {
                options.iterations = to_size(value());
            } else if (argument == "--population") {
                options.population_size = to_size(value());
            } else if (argument == "--time") {
                options.time_limit_seconds = to_double(value());
            } else if (argument == "--target") {
                options.target_loss = to_double(value());
            } else if (argument == "--unary") {
                const auto text = value();
                options.unary_operators = text.empty() ? std::vector<std::string>{} : split(text, ',');
            } else if (argument == "--binary") {
                const auto text = value();
                options.binary_operators = text.empty() ? std::vector<std::string>{} : split(text, ',');
            } else if (argument == "--loss") {
                options.loss = value();
            } else if (argument == "--max-size") {
                options.constraints.max_size = to_size(value());
            } else if (argument == "--max-depth") {
                options.constraints.max_depth = to_size(value());
            } else if (argument == "--constraint") {
                const auto parts = split(value(), ':');
                if (parts.size() < 2 || parts.size() > 3 || parts[0].empty()) {
                    throw std::invalid_argument("Expected --constraint OP:A[:B]");
                }
                options.constraints.arguments[parts[0]] = {limit(parts[1]),
                                                           parts.size() == 3 ? limit(parts[2]) : std::nullopt};
            } else if (argument == "--max-exponent") {
                if (!options.constraints.exponent) options.constraints.exponent.emplace();
                options.constraints.exponent->max_abs_value = to_double(value());
            } else if (argument == "--integer-exponent") {
                if (!options.constraints.exponent) options.constraints.exponent.emplace();
                options.constraints.exponent->integer_only = true;
            } else if (argument == "--tournament") {
                options.tournament_size = to_size(value());
            } else if (argument == "--win-probability") {
                options.tournament_win_probability = to_double(value());
            } else if (argument == "--crossover") {
                options.crossover_probability = to_double(value());
            } else if (argument == "--optimize") {
                options.optimize_probability = to_double(value());
            } else if (argument == "--output") {
                output = value();
            } else if (argument.starts_with("--")) {
                throw std::invalid_argument("Unknown option " + argument + "\n" + usage);
            } else {
                files.emplace_back(argument);
            }
        }
        if (files.size() > 3) throw std::invalid_argument(usage);

        std::vector<sr::Dataset> data;
        if (files.empty()) {
            const auto spring = sr::physical_problems()[0];
            for (std::uint64_t split_index = 0; split_index < 3; ++split_index) {
                data.push_back(sr::generate_data(spring, split_index == 1 ? 128 : 256, 20261019 + split_index));
            }
            std::cout << "Data: generated clean spring problem (train/validation/test)\n";
        } else {
            for (const auto& file : files) data.push_back(sr::read_csv(file));
            std::cout << "Data: " << files.size() << " CSV file(s), train = " << files[0].string() << '\n';
        }

        sr::FunctionRegistry functions;
        const auto result = data.size() >= 2 ? sr::fit(data[0], data[1], options, functions)
                                             : sr::fit(data[0], options, functions);
        const auto& stats = result.statistics;
        std::cout << "Operators: unary [" << join(options.unary_operators) << "], binary ["
                  << join(options.binary_operators) << "], loss " << options.loss << ", seed " << options.seed
                  << "\nStopped by " << sr::to_string(stats.stop_reason) << " after " << stats.iterations
                  << " iteration(s), " << stats.offspring << " offspring, " << stats.loss_evaluations
                  << " loss evaluations in " << std::setprecision(3) << stats.seconds << " s"
                  << "\nUndefined arithmetic: " << stats.failed_evaluations
                  << ", inadmissible proposals: " << stats.inadmissible_proposals
                  << ", offspring kept as parent copy: " << stats.rejected_offspring << "\n\nHall of fame:\n";
        sr::write_hall_of_fame_csv(std::cout, result);

        const auto& best = result.best;
        std::optional<double> test_loss;
        if (data.size() == 3) {
            test_loss = sr::mean_loss(sr::predict(best.expression, functions, data[2]), data[2].targets,
                                      functions.loss(options.loss));
        }
        std::cout << "\nSelected: " << best.expression.to_string() << "\nComplexity: " << best.complexity()
                  << "\nTrain " << options.loss << ": " << json_number(best.train_loss);
        if (best.validation_loss) std::cout << "\nValidation " << options.loss << ": " << json_number(*best.validation_loss);
        if (test_loss) std::cout << "\nTest " << options.loss << ": " << json_number(*test_loss);
        std::cout << '\n';

        if (output) {
            std::filesystem::create_directories(*output);
            std::ofstream hall(*output / "hall_of_fame.csv");
            sr::write_hall_of_fame_csv(hall, result);
            std::ofstream json(*output / "result.json");
            json << "{\n  \"system\": \"sr-cpp\",\n  \"seed\": " << options.seed
                 << ",\n  \"population_size\": " << options.population_size
                 << ",\n  \"iterations_requested\": " << options.iterations
                 << ",\n  \"loss\": " << json_string(options.loss)
                 << ",\n  \"unary_operators\": " << json_string(join(options.unary_operators))
                 << ",\n  \"binary_operators\": " << json_string(join(options.binary_operators))
                 << ",\n  \"max_size\": " << options.constraints.max_size
                 << ",\n  \"max_depth\": " << options.constraints.max_depth
                 << ",\n  \"stop_reason\": " << json_string(sr::to_string(stats.stop_reason))
                 << ",\n  \"iterations\": " << stats.iterations << ",\n  \"offspring\": " << stats.offspring
                 << ",\n  \"loss_evaluations\": " << stats.loss_evaluations
                 << ",\n  \"failed_evaluations\": " << stats.failed_evaluations
                 << ",\n  \"inadmissible_proposals\": " << stats.inadmissible_proposals
                 << ",\n  \"rejected_offspring\": " << stats.rejected_offspring
                 << ",\n  \"search_s\": " << json_number(stats.seconds)
                 << ",\n  \"equation\": " << json_string(best.expression.to_string())
                 << ",\n  \"complexity\": " << best.complexity()
                 << ",\n  \"train_loss\": " << json_number(best.train_loss)
                 << ",\n  \"validation_loss\": " << (best.validation_loss ? json_number(*best.validation_loss) : "null")
                 << ",\n  \"test_loss\": " << (test_loss ? json_number(*test_loss) : "null") << "\n}\n";
            if (!hall || !json) throw std::runtime_error("Cannot write results to " + output->string());
            std::cout << "Saved hall_of_fame.csv and result.json in " << output->string() << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

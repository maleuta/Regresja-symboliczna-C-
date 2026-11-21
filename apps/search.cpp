// Command-line symbolic regression: fit on train, select on validation, report test loss.
#include "sr/benchmarks.hpp"
#include "sr/search.hpp"

#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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
    "  --output DIR          write hall_of_fame.csv, progress.csv and result.json\n"
    "  --trace FILE          write a JSON trace of the run for the visualizer\n"
    "  --label TEXT          name of the run stored in the trace\n";

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

// Six significant digits: enough for plots, keeps traces small.
std::string json_short(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(6) << value;
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

// Compact record of the search used by progress.csv and the JSON trace.
struct SnapshotRecord {
    std::size_t iterations, offspring;
    double seconds;
    std::vector<std::pair<std::size_t, double>> population;  // complexity, train loss
    std::vector<sr::Candidate> front;
};

struct EventRecord {
    std::size_t cycle, iteration, inadmissible, replaced;
    std::string operation, parent, second, child;
    double parent_loss, child_loss;
    bool optimized, new_best;
};

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
        std::optional<std::filesystem::path> trace_path;
        std::string label = "sr_search";
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
            } else if (argument == "--trace") {
                trace_path = value();
            } else if (argument == "--label") {
                label = value();
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

        // Observers only record; they do not change the run (same seed -> same result).
        std::vector<SnapshotRecord> snapshots;
        std::vector<EventRecord> events;
        double best_so_far = std::numeric_limits<double>::infinity();
        if (output || trace_path) {
            options.observer.on_iteration = [&](const sr::IterationSnapshot& snapshot) {
                SnapshotRecord record{snapshot.iterations, snapshot.offspring, snapshot.seconds, {}, snapshot.front};
                for (const auto& member : snapshot.population) {
                    record.population.emplace_back(member.complexity(), member.train_loss);
                }
                if (!snapshot.front.empty()) best_so_far = std::min(best_so_far, snapshot.front.back().train_loss);
                snapshots.push_back(std::move(record));
            };
        }
        if (trace_path) {
            // Keep the first cycles (to show the mechanics) and every cycle that improved the best loss.
            options.observer.on_offspring = [&](const sr::OffspringEvent& event) {
                const bool new_best = event.child.train_loss < best_so_far;
                if (new_best) best_so_far = event.child.train_loss;
                if (event.cycle >= 60 && !new_best) return;
                if (events.size() >= 400) return;
                events.push_back({event.cycle, event.iteration, event.inadmissible, event.replaced, event.operation,
                                  event.parent.expression.to_string(),
                                  event.second_parent ? event.second_parent->expression.to_string() : std::string{},
                                  event.child.expression.to_string(), event.parent.train_loss, event.child.train_loss,
                                  event.optimized, new_best});
            };
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
            std::ofstream progress(*output / "progress.csv");
            progress.imbue(std::locale::classic());
            progress << std::setprecision(17) << "iterations,offspring,seconds,best_train_loss,front_size\n";
            for (const auto& snapshot : snapshots) {
                progress << snapshot.iterations << ',' << snapshot.offspring << ',' << snapshot.seconds << ','
                         << (snapshot.front.empty() ? "" : json_number(snapshot.front.back().train_loss)) << ','
                         << snapshot.front.size() << '\n';
            }
            if (!hall || !json || !progress) throw std::runtime_error("Cannot write results to " + output->string());
            std::cout << "Saved hall_of_fame.csv, progress.csv and result.json in " << output->string() << '\n';
        }

        if (trace_path) {
            if (trace_path->has_parent_path()) std::filesystem::create_directories(trace_path->parent_path());
            std::ofstream trace(*trace_path);
            trace.imbue(std::locale::classic());
            const auto strings = [](const std::vector<std::string>& items) {
                std::string text = "[";
                for (std::size_t i = 0; i < items.size(); ++i) text += (i ? "," : "") + json_string(items[i]);
                return text + "]";
            };
            const auto candidates = [](const std::vector<sr::Candidate>& front) {
                std::string text = "[";
                for (std::size_t i = 0; i < front.size(); ++i) {
                    text += std::string(i ? "," : "") + "{\"c\":" + std::to_string(front[i].complexity()) +
                            ",\"loss\":" + json_number(front[i].train_loss) +
                            (front[i].validation_loss ? ",\"val\":" + json_number(*front[i].validation_loss) : "") +
                            ",\"expr\":" + json_string(front[i].expression.to_string()) + "}";
                }
                return text + "]";
            };
            const auto& train = data[0];
            trace << "{\"label\":" << json_string(label) << ",\"features\":" << strings(train.feature_names)
                  << ",\"target\":" << json_string(train.target_name) << ",\"rows\":[";
            const std::size_t stride = std::max<std::size_t>(1, train.inputs.size() / 200);
            for (std::size_t row = 0, written = 0; row < train.inputs.size(); row += stride, ++written) {
                trace << (written ? "," : "") << '[';
                for (const double x : train.inputs[row]) trace << json_short(x) << ',';
                trace << json_short(train.targets[row]) << ']';
            }
            trace << "],\"options\":{\"seed\":" << options.seed << ",\"population_size\":" << options.population_size
                  << ",\"iterations\":" << options.iterations << ",\"tournament_size\":" << options.tournament_size
                  << ",\"tournament_win_probability\":" << json_number(options.tournament_win_probability)
                  << ",\"crossover_probability\":" << json_number(options.crossover_probability)
                  << ",\"unary\":" << strings(options.unary_operators)
                  << ",\"binary\":" << strings(options.binary_operators) << ",\"loss\":" << json_string(options.loss)
                  << ",\"max_size\":" << options.constraints.max_size
                  << ",\"max_depth\":" << options.constraints.max_depth << "},\"snapshots\":[";
            for (std::size_t i = 0; i < snapshots.size(); ++i) {
                const auto& snapshot = snapshots[i];
                trace << (i ? "," : "") << "{\"iterations\":" << snapshot.iterations << ",\"offspring\":"
                      << snapshot.offspring << ",\"seconds\":" << json_number(snapshot.seconds)
                      << ",\"front\":" << candidates(snapshot.front) << ",\"population\":[";
                for (std::size_t j = 0; j < snapshot.population.size(); ++j) {
                    trace << (j ? "," : "") << '[' << snapshot.population[j].first << ','
                          << json_short(snapshot.population[j].second) << ']';
                }
                trace << "]}";
            }
            trace << "],\"events\":[";
            for (std::size_t i = 0; i < events.size(); ++i) {
                const auto& event = events[i];
                trace << (i ? "," : "") << "{\"cycle\":" << event.cycle << ",\"iteration\":" << event.iteration
                      << ",\"op\":" << json_string(event.operation) << ",\"parent\":" << json_string(event.parent)
                      << ",\"parent_loss\":" << json_number(event.parent_loss)
                      << ",\"second\":" << json_string(event.second) << ",\"child\":" << json_string(event.child)
                      << ",\"child_loss\":" << json_number(event.child_loss)
                      << ",\"inadmissible\":" << event.inadmissible << ",\"optimized\":"
                      << (event.optimized ? "true" : "false") << ",\"replaced\":" << event.replaced
                      << ",\"new_best\":" << (event.new_best ? "true" : "false") << '}';
            }
            trace << "],\"result\":{\"equation\":" << json_string(best.expression.to_string())
                  << ",\"complexity\":" << best.complexity() << ",\"train_loss\":" << json_number(best.train_loss)
                  << ",\"validation_loss\":" << (best.validation_loss ? json_number(*best.validation_loss) : "null")
                  << ",\"test_loss\":" << (test_loss ? json_number(*test_loss) : "null")
                  << ",\"stop_reason\":" << json_string(sr::to_string(stats.stop_reason))
                  << ",\"seconds\":" << json_number(stats.seconds) << ",\"offspring\":" << stats.offspring
                  << ",\"inadmissible\":" << stats.inadmissible_proposals
                  << ",\"failed\":" << stats.failed_evaluations << ",\"front\":" << candidates(result.hall_of_fame)
                  << "}}\n";
            if (!trace) throw std::runtime_error("Cannot write trace " + trace_path->string());
            std::cout << "Saved trace " << trace_path->string() << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

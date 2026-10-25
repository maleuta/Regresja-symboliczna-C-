#include "sr/dataset.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <istream>
#include <locale>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace sr {
namespace {

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> cells;
    std::size_t start = 0;
    for (;;) {
        const auto end = line.find(',', start);
        cells.emplace_back(trim(std::string_view(line).substr(start, end - start)));
        if (end == std::string::npos) return cells;
        start = end + 1;
    }
}

bool valid_name(const std::string& name) {
    const auto letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    return !name.empty() && letter(name.front()) &&
        std::all_of(name.begin(), name.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9');
        });
}

double number(const std::string& cell, std::size_t line) {
    double value = 0;
    const auto result = std::from_chars(cell.data(), cell.data() + cell.size(), value);
    if (cell.empty() || result.ec != std::errc{} ||
        result.ptr != cell.data() + cell.size() || !std::isfinite(value)) {
        throw std::invalid_argument("CSV line " + std::to_string(line) + ": invalid finite number '" + cell + "'");
    }
    return value;
}

} // namespace

void Dataset::validate() const {
    if (feature_names.empty() || inputs.empty() || inputs.size() != targets.size()) {
        throw std::invalid_argument("Dataset requires features, nonempty rows and one target per row");
    }
    std::set<std::string> names;
    names.insert(target_name);
    if (!valid_name(target_name)) throw std::invalid_argument("Invalid target name");
    for (const auto& name : feature_names) {
        if (!valid_name(name) || !names.insert(name).second) {
            throw std::invalid_argument("Invalid or duplicate feature name: " + name);
        }
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i].size() != feature_names.size() || !std::isfinite(targets[i]) ||
            !std::all_of(inputs[i].begin(), inputs[i].end(), [](double x) { return std::isfinite(x); })) {
            throw std::invalid_argument("Invalid dataset row " + std::to_string(i + 1));
        }
    }
}

Dataset read_csv(std::istream& input, const std::string& target_name) {
    std::string line;
    if (!std::getline(input, line)) throw std::invalid_argument("CSV is empty");
    const auto header = split(line);
    const auto target = std::find(header.begin(), header.end(), target_name);
    if (target == header.end()) throw std::invalid_argument("Missing target column: " + target_name);
    const auto target_index = static_cast<std::size_t>(target - header.begin());
    Dataset dataset;
    dataset.target_name = target_name;
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (i != target_index) dataset.feature_names.push_back(header[i]);
    }
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        const auto cells = split(line);
        if (cells.size() != header.size()) {
            throw std::invalid_argument("CSV line " + std::to_string(line_number) + ": wrong column count");
        }
        std::vector<double> row;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const double value = number(cells[i], line_number);
            if (i == target_index) dataset.targets.push_back(value);
            else row.push_back(value);
        }
        dataset.inputs.push_back(std::move(row));
    }
    if (input.bad()) throw std::runtime_error("Failed to read CSV stream");
    dataset.validate();
    return dataset;
}

Dataset read_csv(const std::filesystem::path& path, const std::string& target_name) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open CSV: " + path.string());
    return read_csv(input, target_name);
}

void write_csv(std::ostream& output, const Dataset& dataset) {
    dataset.validate();
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    for (const auto& name : dataset.feature_names) output << name << ',';
    output << dataset.target_name << '\n';
    for (std::size_t i = 0; i < dataset.inputs.size(); ++i) {
        for (const double value : dataset.inputs[i]) output << value << ',';
        output << dataset.targets[i] << '\n';
    }
    if (!output) throw std::runtime_error("Failed to write CSV stream");
}

void write_csv(const std::filesystem::path& path, const Dataset& dataset) {
    dataset.validate();
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot write CSV: " + path.string());
    write_csv(output, dataset);
    output.close();
    if (!output) throw std::runtime_error("Failed to close CSV: " + path.string());
}

} // namespace sr

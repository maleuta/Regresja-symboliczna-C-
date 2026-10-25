#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace sr {

struct Dataset {
    std::vector<std::string> feature_names;
    std::vector<std::vector<double>> inputs;
    std::vector<double> targets;
    std::string target_name = "y";

    void validate() const;
};

// Numeric, unquoted CSV with a header. The target column can occur anywhere.
Dataset read_csv(std::istream& input, const std::string& target_name = "y");
Dataset read_csv(const std::filesystem::path& path, const std::string& target_name = "y");
void write_csv(std::ostream& output, const Dataset& dataset);
void write_csv(const std::filesystem::path& path, const Dataset& dataset);

} // namespace sr

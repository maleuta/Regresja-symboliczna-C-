#include "sr/benchmarks.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::invalid_argument("Usage: sr_generate OUTPUT_DIRECTORY [SEED]");
        std::uint64_t seed = 20261019;
        if (argc == 3) {
            const std::string text(argv[2]);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), seed);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
                throw std::invalid_argument("SEED must be an unsigned integer");
            }
        }
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        std::ofstream manifest(output / "manifest.csv");
        if (!manifest) throw std::runtime_error("Cannot write dataset manifest");
        manifest << std::setprecision(17)
                 << "problem,variant,split,rows,data_seed,noise_seed,noise_sigma,filename\n";
        const auto problems = sr::physical_problems();
        const std::array<std::string, 3> splits{"train", "validation", "test"};
        const std::array<std::size_t, 3> sizes{256, 128, 256};
        for (std::size_t p = 0; p < problems.size(); ++p) {
            const auto base = seed + 100 * p;
            const auto train = sr::generate_data(problems[p], sizes[0], base);
            const double sigma = 0.01 * sr::target_standard_deviation(train);
            for (std::size_t split = 0; split < splits.size(); ++split) {
                const auto data_seed = base + split;
                const auto noise_seed = base + split + 10000;
                const auto clean = sr::generate_data(problems[p], sizes[split], data_seed);
                for (const bool noisy : {false, true}) {
                    const std::string variant = noisy ? "noisy" : "clean";
                    const std::string filename = problems[p].name + "_" + variant + "_" + splits[split] + ".csv";
                    sr::write_csv(output / filename, noisy ? sr::with_noise(clean, sigma, noise_seed) : clean);
                    manifest << problems[p].name << ',' << variant << ',' << splits[split] << ','
                             << sizes[split] << ',' << data_seed << ',' << noise_seed << ','
                             << (noisy ? sigma : 0) << ',' << filename << '\n';
                }
            }
        }
        manifest.close();
        if (!manifest) throw std::runtime_error("Failed to write dataset manifest");
        std::cout << "Generated 18 CSV files and manifest.csv in " << output << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

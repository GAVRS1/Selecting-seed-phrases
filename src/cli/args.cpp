#include "cli/args.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cli {

namespace {
std::string trim_copy(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim_copy(token);
        if (!token.empty()) {
            out.push_back(std::move(token));
        }
    }
    return out;
}

std::vector<std::string> split_words_csv_or_space(const std::string& input) {
    if (input.find(',') != std::string::npos) {
        return split_csv(input);
    }

    std::vector<std::string> out;
    std::stringstream ss(input);
    std::string token;
    while (ss >> token) {
        out.push_back(std::move(token));
    }
    return out;
}
} // namespace

core::AppConfig parse_args(int argc, char** argv) {
    core::AppConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--template" && i + 1 < argc) {
            cfg.template_words = split_words_csv_or_space(argv[++i]);
        } else if (arg == "--chains" && i + 1 < argc) {
            cfg.chains = split_csv(argv[++i]);
        } else if (arg == "--paths-btc" && i + 1 < argc) {
            cfg.paths_btc = split_csv(argv[++i]);
        } else if (arg == "--paths-eth" && i + 1 < argc) {
            cfg.paths_eth = split_csv(argv[++i]);
        } else if (arg == "--paths-sol" && i + 1 < argc) {
            cfg.paths_sol = split_csv(argv[++i]);
        } else if (arg == "--target-addresses" && i + 1 < argc) {
            cfg.target_addresses_path = argv[++i];
        } else if (arg == "--recovered-wallets" && i + 1 < argc) {
            cfg.recovered_wallets_path = argv[++i];
        } else if (arg == "--manual-wallets" && i + 1 < argc) {
            cfg.manual_wallets_path = argv[++i];
        } else if (arg == "--bip39-passphrase" && i + 1 < argc) {
            cfg.bip39_passphrase = argv[++i];
        } else if (arg == "--shuffle-words") {
            cfg.shuffle_words = true;
        } else if (arg == "--shuffle-seed" && i + 1 < argc) {
            cfg.shuffle_seed = std::stoull(argv[++i]);
            cfg.shuffle_words = true;
        } else if (arg == "--wordlist" && i + 1 < argc) {
            cfg.wordlist_path = argv[++i];
        } else if (arg == "--allow-words" && i + 1 < argc) {
            cfg.allow_words = split_words_csv_or_space(argv[++i]);
        } else if (arg == "--scan-limit" && i + 1 < argc) {
            cfg.scan_limit = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--max-candidates" && i + 1 < argc) {
            cfg.max_candidates = std::stoull(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        }
    }

    if (cfg.template_words.empty() && cfg.manual_wallets_path.empty()) {
        throw std::invalid_argument("either --template or --manual-wallets is required");
    }
    return cfg;
}

} // namespace cli

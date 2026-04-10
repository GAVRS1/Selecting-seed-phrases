#include "cli/args.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

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

std::string strip_optional_quotes(const std::string& value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::unordered_map<std::string, std::string> parse_dotenv_file(const std::string& path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream in(path);
    if (!in) {
        return values;
    }

    std::string line;
    while (std::getline(in, line)) {
        const std::string clean = trim_copy(line);
        if (clean.empty() || clean[0] == '#') {
            continue;
        }

        const auto pos = clean.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }

        const std::string key = trim_copy(clean.substr(0, pos));
        const std::string raw_value = trim_copy(clean.substr(pos + 1));
        if (key.empty()) {
            continue;
        }

        values[key] = strip_optional_quotes(raw_value);
    }

    return values;
}

std::optional<std::string> getenv_copy(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}
} // namespace

core::AppConfig parse_args(int argc, char** argv) {
    core::AppConfig cfg;
    bool postgres_conn_set_by_cli = false;
    bool postgres_table_set_by_cli = false;

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
        } else if (arg == "--paths-ton" && i + 1 < argc) {
            cfg.paths_ton = split_csv(argv[++i]);
        } else if (arg == "--target-addresses" && i + 1 < argc) {
            cfg.target_addresses_path = argv[++i];
        } else if (arg == "--recovered-wallets" && i + 1 < argc) {
            cfg.recovered_wallets_path = argv[++i];
        } else if (arg == "--postgres-conn" && i + 1 < argc) {
            cfg.postgres_conninfo = argv[++i];
            postgres_conn_set_by_cli = true;
        } else if (arg == "--postgres-table" && i + 1 < argc) {
            cfg.postgres_table = argv[++i];
            postgres_table_set_by_cli = true;
        } else if (arg == "--env-file" && i + 1 < argc) {
            cfg.env_file_path = argv[++i];
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

    const auto dotenv_values = parse_dotenv_file(cfg.env_file_path);
    if (!postgres_conn_set_by_cli) {
        const auto dotenv_conn_it = dotenv_values.find("RECOVERY_POSTGRES_CONN");
        if (dotenv_conn_it != dotenv_values.end() && !dotenv_conn_it->second.empty()) {
            cfg.postgres_conninfo = dotenv_conn_it->second;
        } else if (const auto env_conn = getenv_copy("RECOVERY_POSTGRES_CONN"); env_conn.has_value()) {
            cfg.postgres_conninfo = *env_conn;
        }
    }
    if (!postgres_table_set_by_cli) {
        const auto dotenv_table_it = dotenv_values.find("RECOVERY_POSTGRES_TABLE");
        if (dotenv_table_it != dotenv_values.end() && !dotenv_table_it->second.empty()) {
            cfg.postgres_table = dotenv_table_it->second;
        } else if (const auto env_table = getenv_copy("RECOVERY_POSTGRES_TABLE"); env_table.has_value()) {
            cfg.postgres_table = *env_table;
        }
    }

    if (cfg.template_words.empty() && cfg.manual_wallets_path.empty()) {
        throw std::invalid_argument("either --template or --manual-wallets is required");
    }
    return cfg;
}

} // namespace cli

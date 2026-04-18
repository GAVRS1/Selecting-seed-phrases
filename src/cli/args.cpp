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
    bool seed_table_btc_set_by_cli = false;
    bool seed_table_evm_set_by_cli = false;
    bool seed_table_sol_set_by_cli = false;
    bool result_table_btc_set_by_cli = false;
    bool result_table_evm_set_by_cli = false;
    bool result_table_sol_set_by_cli = false;

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
        } else if (arg == "--postgres-conn" && i + 1 < argc) {
            cfg.postgres_conninfo = argv[++i];
            postgres_conn_set_by_cli = true;
        } else if (arg == "--postgres-seed-table-btc" && i + 1 < argc) {
            cfg.postgres_seed_table_btc = argv[++i];
            seed_table_btc_set_by_cli = true;
        } else if (arg == "--postgres-seed-table-evm" && i + 1 < argc) {
            cfg.postgres_seed_table_evm = argv[++i];
            seed_table_evm_set_by_cli = true;
        } else if (arg == "--postgres-seed-table-sol" && i + 1 < argc) {
            cfg.postgres_seed_table_sol = argv[++i];
            seed_table_sol_set_by_cli = true;
        } else if (arg == "--postgres-result-table-btc" && i + 1 < argc) {
            cfg.postgres_result_table_btc = argv[++i];
            result_table_btc_set_by_cli = true;
        } else if (arg == "--postgres-result-table-evm" && i + 1 < argc) {
            cfg.postgres_result_table_evm = argv[++i];
            result_table_evm_set_by_cli = true;
        } else if (arg == "--postgres-result-table-sol" && i + 1 < argc) {
            cfg.postgres_result_table_sol = argv[++i];
            result_table_sol_set_by_cli = true;
        } else if (arg == "--env-file" && i + 1 < argc) {
            cfg.env_file_path = argv[++i];
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
    if (!seed_table_btc_set_by_cli) {
        if (const auto it = dotenv_values.find("RECOVERY_POSTGRES_SEED_TABLE_BTC"); it != dotenv_values.end() && !it->second.empty()) {
            cfg.postgres_seed_table_btc = it->second;
        } else if (const auto value = getenv_copy("RECOVERY_POSTGRES_SEED_TABLE_BTC"); value.has_value()) {
            cfg.postgres_seed_table_btc = *value;
        }
    }
    if (!seed_table_evm_set_by_cli) {
        if (const auto it = dotenv_values.find("RECOVERY_POSTGRES_SEED_TABLE_EVM"); it != dotenv_values.end() && !it->second.empty()) {
            cfg.postgres_seed_table_evm = it->second;
        } else if (const auto value = getenv_copy("RECOVERY_POSTGRES_SEED_TABLE_EVM"); value.has_value()) {
            cfg.postgres_seed_table_evm = *value;
        }
    }
    if (!seed_table_sol_set_by_cli) {
        if (const auto it = dotenv_values.find("RECOVERY_POSTGRES_SEED_TABLE_SOL"); it != dotenv_values.end() && !it->second.empty()) {
            cfg.postgres_seed_table_sol = it->second;
        } else if (const auto value = getenv_copy("RECOVERY_POSTGRES_SEED_TABLE_SOL"); value.has_value()) {
            cfg.postgres_seed_table_sol = *value;
        }
    }
    if (!result_table_btc_set_by_cli) {
        if (const auto it = dotenv_values.find("RECOVERY_POSTGRES_RESULT_TABLE_BTC"); it != dotenv_values.end() && !it->second.empty()) {
            cfg.postgres_result_table_btc = it->second;
        } else if (const auto value = getenv_copy("RECOVERY_POSTGRES_RESULT_TABLE_BTC"); value.has_value()) {
            cfg.postgres_result_table_btc = *value;
        }
    }
    if (!result_table_evm_set_by_cli) {
        if (const auto it = dotenv_values.find("RECOVERY_POSTGRES_RESULT_TABLE_EVM"); it != dotenv_values.end() && !it->second.empty()) {
            cfg.postgres_result_table_evm = it->second;
        } else if (const auto value = getenv_copy("RECOVERY_POSTGRES_RESULT_TABLE_EVM"); value.has_value()) {
            cfg.postgres_result_table_evm = *value;
        }
    }
    if (!result_table_sol_set_by_cli) {
        if (const auto it = dotenv_values.find("RECOVERY_POSTGRES_RESULT_TABLE_SOL"); it != dotenv_values.end() && !it->second.empty()) {
            cfg.postgres_result_table_sol = it->second;
        } else if (const auto value = getenv_copy("RECOVERY_POSTGRES_RESULT_TABLE_SOL"); value.has_value()) {
            cfg.postgres_result_table_sol = *value;
        }
    }

    if (cfg.template_words.empty()) {
        throw std::invalid_argument("--template is required");
    }
    if (cfg.postgres_conninfo.empty()) {
        throw std::invalid_argument(
            "PostgreSQL connection string is required (--postgres-conn or RECOVERY_POSTGRES_CONN in env/.env)");
    }
    return cfg;
}

} // namespace cli

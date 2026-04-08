#include "chains/solana_module.hpp"
#include "common.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace chains {

namespace {
bool is_plain_unsigned_integer(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

std::string shell_escape_single_quote(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string run_command(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run scanner request");
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

std::string query_rpc_best_effort(const std::string& payload) {
    static const std::vector<std::string> rpc_endpoints = {
        "https://api.mainnet-beta.solana.com",
        "https://solana-api.projectserum.com",
        "https://rpc.ankr.com/solana",
    };

    std::string last_response;
    for (const auto& endpoint : rpc_endpoints) {
#ifdef _WIN32
        std::string escaped_payload = payload;
        for (std::size_t pos = 0; (pos = escaped_payload.find('\'', pos)) != std::string::npos; pos += 2) {
            escaped_payload.replace(pos, 1, "''");
        }
        const std::string command =
            "powershell -NoProfile -Command \"try {(Invoke-WebRequest -UseBasicParsing -TimeoutSec 12 -Method Post "
            "-ContentType 'application/json' -Body '" +
            escaped_payload + "' '" + endpoint +
            "').Content} catch {''}\"";
#else
        const std::string command =
            "curl -fsSL --max-time 12 -X POST -H 'Content-Type: application/json' --data '" +
            shell_escape_single_quote(payload) + "' '" + endpoint + "'";
#endif
        const std::string response = run_command(command);
        if (response.find("\"result\"") != std::string::npos && response.find("\"error\"") == std::string::npos) {
            return response;
        }
        if (!response.empty()) {
            last_response = response;
        }
    }

    return last_response;
}

std::optional<std::uint64_t> parse_result_value_u64(const std::string& response) {
    const std::size_t result_pos = response.find("\"result\"");
    if (result_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t value_pos = response.find("\"value\"", result_pos);
    if (value_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t colon_pos = response.find(':', value_pos);
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t i = colon_pos + 1;
    while (i < response.size() && std::isspace(static_cast<unsigned char>(response[i]))) {
        ++i;
    }
    if (i >= response.size()) {
        return std::nullopt;
    }

    if (response[i] == '"') {
        ++i;
    }

    std::size_t j = i;
    while (j < response.size() && std::isdigit(static_cast<unsigned char>(response[j]))) {
        ++j;
    }
    if (j == i) {
        return std::nullopt;
    }

    const std::string value_text = response.substr(i, j - i);
    if (!is_plain_unsigned_integer(value_text)) {
        return std::nullopt;
    }

    const unsigned long long parsed = std::strtoull(value_text.c_str(), nullptr, 10);
    if (parsed == std::numeric_limits<unsigned long long>::max() && value_text != "18446744073709551615") {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(parsed);
}

bool has_positive_token_amount(const std::string& token_accounts_response) {
    const std::regex amount_regex(R"rgx("amount"\s*:\s*"([0-9]+)")rgx");
    for (auto it = std::sregex_iterator(token_accounts_response.begin(), token_accounts_response.end(), amount_regex);
         it != std::sregex_iterator();
         ++it) {
        const std::string amount_text = (*it)[1].str();
        if (!is_plain_unsigned_integer(amount_text)) {
            continue;
        }
        const double amount = std::strtod(amount_text.c_str(), nullptr);
        if (amount > 0.0) {
            return true;
        }
    }
    return false;
}
} // namespace

std::string SolanaModule::name() const { return "sol"; }
std::string SolanaModule::coin_ticker() const { return "SOL"; }

std::vector<std::string> SolanaModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_sol_addresses(seed, derivation_paths, account_scan_limit);
}

double SolanaModule::fetch_balance_coin(const std::string& address) {
    const std::string balance_payload =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getBalance\",\"params\":[\"" + address + "\"]}";
    const std::string token_accounts_payload =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getTokenAccountsByOwner\",\"params\":[\"" + address +
        "\",{\"programId\":\"TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA\"},{\"encoding\":\"jsonParsed\"}]}";
    const std::string balance_response = query_rpc_best_effort(balance_payload);
    const auto lamports = parse_result_value_u64(balance_response);
    if (lamports.has_value() && *lamports > 0) {
        return static_cast<double>(*lamports) / 1000000000.0;
    }

    const std::string token_accounts_response = query_rpc_best_effort(token_accounts_payload);
    if (has_positive_token_amount(token_accounts_response)) {
        return 1e-9;
    }

    return 0.0;
}

} // namespace chains

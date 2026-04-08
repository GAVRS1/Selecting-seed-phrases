#include "chains/solana_module.hpp"
#include "common.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
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

std::vector<std::string> rpc_urls() {
    return {
        "https://api.mainnet-beta.solana.com",
        "https://solana.public-rpc.com",
    };
}

std::string build_rpc_command(const std::string& payload, const std::string& rpc_url) {
#ifdef _WIN32
    std::string ps_payload = payload;
    for (std::size_t pos = 0; (pos = ps_payload.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_payload.replace(pos, 1, "''");
    }
    std::string ps_rpc_url = rpc_url;
    for (std::size_t pos = 0; (pos = ps_rpc_url.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_rpc_url.replace(pos, 1, "''");
    }
    return "powershell -NoProfile -Command \"$b='" + ps_payload + "'; "
           "(Invoke-WebRequest -UseBasicParsing -Method Post -Uri '" + ps_rpc_url +
           "' -ContentType 'application/json' -Body $b).Content\"";
#else
    return "curl -fsSL --retry 2 --retry-delay 1 --max-time 10 "
           "-H 'content-type: application/json' -H 'user-agent: Mozilla/5.0' -d '" +
        shell_escape_single_quote(payload) + "' '" + shell_escape_single_quote(rpc_url) + "'";
#endif
}

std::string query_rpc_best_effort(const std::string& payload) {
    std::string last_response;
    for (const auto& url : rpc_urls()) {
        const std::string response = run_command(build_rpc_command(payload, url));
        if (response.find("\"error\"") == std::string::npos && response.find("\"result\"") != std::string::npos) {
            return response;
        }
        if (!response.empty()) {
            last_response = response;
        }
    }
    return last_response;
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
    std::smatch m;
    if (std::regex_search(balance_response, m, std::regex(R"("value"\s*:\s*([0-9]+))")) && !m[1].str().empty()) {
        const std::string lamports_text = m[1].str();
        if (is_plain_unsigned_integer(lamports_text)) {
            const double lamports = std::strtod(lamports_text.c_str(), nullptr);
            if (lamports > 0.0) {
                return lamports / 1000000000.0;
            }
        }
    }

    const std::string token_accounts_response = query_rpc_best_effort(token_accounts_payload);
    if (has_positive_token_amount(token_accounts_response)) {
        return 1e-9;
    }

    return 0.0;
}

} // namespace chains

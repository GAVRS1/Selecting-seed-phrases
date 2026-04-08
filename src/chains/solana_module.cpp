#include "chains/solana_module.hpp"
#include "common.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <stdexcept>
#include <string>

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
#ifdef _WIN32
    std::string ps_balance_payload = balance_payload;
    for (std::size_t pos = 0; (pos = ps_balance_payload.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_balance_payload.replace(pos, 1, "''");
    }
    std::string ps_token_accounts_payload = token_accounts_payload;
    for (std::size_t pos = 0; (pos = ps_token_accounts_payload.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_token_accounts_payload.replace(pos, 1, "''");
    }
    const std::string balance_command =
        "powershell -NoProfile -Command \"$b='" + ps_balance_payload +
        "'; (Invoke-WebRequest -UseBasicParsing -Method Post -Uri 'https://api.mainnet-beta.solana.com' "
        "-ContentType 'application/json' -Body $b).Content\"";
    const std::string token_accounts_command =
        "powershell -NoProfile -Command \"$b='" + ps_token_accounts_payload +
        "'; (Invoke-WebRequest -UseBasicParsing -Method Post -Uri 'https://api.mainnet-beta.solana.com' "
        "-ContentType 'application/json' -Body $b).Content\"";
#else
    const std::string balance_command =
        "curl -fsSL --max-time 10 -H 'content-type: application/json' -H 'user-agent: Mozilla/5.0' -d '" +
        shell_escape_single_quote(balance_payload) + "' 'https://api.mainnet-beta.solana.com'";
    const std::string token_accounts_command =
        "curl -fsSL --max-time 10 -H 'content-type: application/json' -H 'user-agent: Mozilla/5.0' -d '" +
        shell_escape_single_quote(token_accounts_payload) + "' 'https://api.mainnet-beta.solana.com'";
#endif

    const std::string balance_response = run_command(balance_command);
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

    const std::string token_accounts_response = run_command(token_accounts_command);
    if (std::regex_search(token_accounts_response, m, std::regex(R"("value"\s*:\s*\[\s*\{)"))) {
        return 1e-9;
    }

    return 0.0;
}

} // namespace chains

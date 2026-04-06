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
double parse_number(const std::string& raw) {
    std::string normalized;
    normalized.reserve(raw.size());

    for (const char ch : raw) {
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == ',') {
            normalized.push_back(ch);
        }
    }

    if (normalized.empty()) {
        return 0.0;
    }

    for (char& ch : normalized) {
        if (ch == ',') {
            ch = '.';
        }
    }

    return std::strtod(normalized.c_str(), nullptr);
}

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
    const std::string url = "https://explorer.solana.com/address/" + address;
    const std::string payload =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getBalance\",\"params\":[\"" + address + "\"]}";
#ifdef _WIN32
    std::string ps_url = url;
    for (std::size_t pos = 0; (pos = ps_url.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_url.replace(pos, 1, "''");
    }
    std::string ps_payload = payload;
    for (std::size_t pos = 0; (pos = ps_payload.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_payload.replace(pos, 1, "''");
    }
    const std::string command =
        "powershell -NoProfile -Command \"(Invoke-WebRequest -UseBasicParsing '" + ps_url + "').Content\"";
    const std::string command_rpc =
        "powershell -NoProfile -Command \"$b='" + ps_payload +
        "'; (Invoke-WebRequest -UseBasicParsing -Method Post -Uri 'https://api.mainnet-beta.solana.com' "
        "-ContentType 'application/json' -Body $b).Content\"";
#else
    const std::string command =
        "curl -fsSL --max-time 10 "
        "-H 'accept-language: en-US,en;q=0.9' "
        "-H 'user-agent: Mozilla/5.0' '" +
        shell_escape_single_quote(url) + "'";
    const std::string command_rpc =
        "curl -fsSL --max-time 10 -H 'content-type: application/json' -H 'user-agent: Mozilla/5.0' -d '" +
        shell_escape_single_quote(payload) + "' 'https://api.mainnet-beta.solana.com'";
#endif
    const std::string response = run_command(command);

    std::smatch m;
    const std::regex balance_cell_pattern(
        "Balance\\s*\\(SOL\\)\\s*</td>\\s*<td[^>]*>\\s*<span[^>]*>\\s*\"?([0-9]+(?:[.,][0-9]+)?)\"?",
        std::regex::icase);
    if (std::regex_search(response, m, balance_cell_pattern)) {
        return parse_number(m[1].str());
    }

    std::string rpc_response = response;
    rpc_response = run_command(command_rpc);

    if (!std::regex_search(rpc_response, m, std::regex(R"("value"\s*:\s*([0-9]+))"))) {
        if (!std::regex_search(rpc_response, m, std::regex(R"("lamports"\s*:\s*([0-9]+))"))) {
            return 0.0;
        }
    }

    if (m[1].str().empty()) {
        return 0.0;
    }
    const std::string lamports_text = m[1].str();
    if (!is_plain_unsigned_integer(lamports_text)) {
        return 0.0;
    }
    const double lamports = std::strtod(lamports_text.c_str(), nullptr);
    return lamports / 1000000000.0;
}

} // namespace chains

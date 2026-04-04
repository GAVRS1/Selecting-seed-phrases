#include "chains/ethereum_module.hpp"
#include "common.hpp"

#include <array>
#include <algorithm>
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

double parse_number(const std::string& raw) {
    std::string normalized;
    normalized.reserve(raw.size());

    for (const char ch : raw) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isdigit(uch) != 0 || ch == '.' || ch == ',') {
            normalized.push_back(ch);
        }
    }

    if (normalized.empty()) {
        return 0.0;
    }

    std::replace(normalized.begin(), normalized.end(), ',', '.');

    char* end_ptr = nullptr;
    const double parsed = std::strtod(normalized.c_str(), &end_ptr);
    if (end_ptr == normalized.c_str()) {
        return 0.0;
    }
    return parsed;
}

double parse_etherscan_balance_eth(const std::string& html) {
    double best_value = 0.0;

    const std::regex holdings_eth_pattern(
        R"(id\s*=\s*"HoldingsETH"[^>]*>\s*([^<\s][^<]*)<)",
        std::regex::icase);
    for (std::sregex_iterator it(html.begin(), html.end(), holdings_eth_pattern), end; it != end; ++it) {
        best_value = std::max(best_value, parse_number((*it)[1].str()));
    }

    const std::regex eth_value_pattern(
        R"(<h4[^>]*>\s*Eth\s*Value\s*</h4>\s*([^<\n\r]*<[^>]*>\s*)*([^<]+)<)",
        std::regex::icase);
    for (std::sregex_iterator it(html.begin(), html.end(), eth_value_pattern), end; it != end; ++it) {
        best_value = std::max(best_value, parse_number((*it)[2].str()));
    }

    return best_value;
}
} // namespace

std::string EthereumModule::name() const { return "eth"; }
std::string EthereumModule::coin_ticker() const { return "ETH"; }

std::vector<std::string> EthereumModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_eth_addresses(seed, derivation_paths, account_scan_limit);
}

double EthereumModule::fetch_balance_coin(const std::string& address) {
    const std::string url = "https://etherscan.io/address/" + address;

#ifdef _WIN32
    std::string ps_url = url;
    for (std::size_t pos = 0; (pos = ps_url.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_url.replace(pos, 1, "''");
    }
    const std::string command =
        "powershell -NoProfile -Command \"(Invoke-WebRequest -UseBasicParsing '" + ps_url + "').Content\"";
#else
    const std::string command = "curl -fsSL --max-time 10 '" + shell_escape_single_quote(url) + "'";
#endif
    const std::string response = run_command(command);
    return parse_etherscan_balance_eth(response);
}

} // namespace chains

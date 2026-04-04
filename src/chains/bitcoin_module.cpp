#include "chains/bitcoin_module.hpp"
#include "common.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

double parse_satoshis_response(const std::string& raw_response) {
    std::string numeric;
    numeric.reserve(raw_response.size());

    for (const char ch : raw_response) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isdigit(uch) != 0 || ch == '.' || ch == ',' || ch == '+' || ch == '-') {
            numeric.push_back(ch);
        }
    }

    if (numeric.empty()) {
        return 0.0;
    }

    for (char& ch : numeric) {
        if (ch == ',') {
            ch = '.';
        }
    }

    char* end_ptr = nullptr;
    const double parsed = std::strtod(numeric.c_str(), &end_ptr);
    if (end_ptr == numeric.c_str()) {
        return 0.0;
    }
    return parsed;
}
} // namespace

std::string BitcoinModule::name() const { return "btc"; }
std::string BitcoinModule::coin_ticker() const { return "BTC"; }

std::vector<std::string> BitcoinModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_btc_addresses(seed, derivation_paths, account_scan_limit);
}

double BitcoinModule::fetch_balance_coin(const std::string& address) {
    const std::string url = "https://blockchain.info/q/addressbalance/" + address;
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

    const double satoshis = parse_satoshis_response(response);
    return satoshis / 100000000.0;
}

} // namespace chains

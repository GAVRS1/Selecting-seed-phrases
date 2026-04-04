#include "chains/bitcoin_module.hpp"
#include "common.hpp"

#include <array>
#include <cstdio>
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
    const std::string command = "curl -fsSL --max-time 10 '" + shell_escape_single_quote(url) + "'";
    const std::string response = run_command(command);

    if (response.empty()) {
        return 0.0;
    }

    const double satoshis = std::stod(response);
    return satoshis / 100000000.0;
}

} // namespace chains

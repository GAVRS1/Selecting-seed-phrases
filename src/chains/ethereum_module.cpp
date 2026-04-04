#include "chains/ethereum_module.hpp"
#include "common.hpp"

#include <array>
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
    const char* api_key = std::getenv("ETHERSCAN_API_KEY");
    std::string key = api_key == nullptr ? "" : api_key;
    if (key.empty()) {
        key = "YourApiKeyToken";
    }

    const std::string url =
        "https://api.etherscan.io/api?module=account&action=balance&address=" + address +
        "&tag=latest&apikey=" + key;

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

    std::smatch m;
    if (!std::regex_search(response, m, std::regex("\"result\"\\s*:\\s*\"([0-9]+)\""))) {
        return 0.0;
    }

    const long double wei = std::stold(m[1].str());
    return static_cast<double>(wei / 1000000000000000000.0L);
}

} // namespace chains

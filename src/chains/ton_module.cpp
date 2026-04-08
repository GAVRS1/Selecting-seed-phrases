#include "chains/ton_module.hpp"
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

double parse_nanotons_response(const std::string& raw_response) {
    std::smatch match;
    if (std::regex_search(raw_response, match, std::regex(R"rgx("balance"\s*:\s*"?([0-9]+)"?)rgx"))) {
        return std::strtod(match[1].str().c_str(), nullptr);
    }
    return 0.0;
}
} // namespace

std::string TonModule::name() const { return "ton"; }
std::string TonModule::coin_ticker() const { return "TON"; }

std::vector<std::string> TonModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_ton_addresses(seed, derivation_paths, account_scan_limit);
}

double TonModule::fetch_balance_coin(const std::string& address) {
    const std::string url_primary = "https://toncenter.com/api/v2/getAddressBalance?address=" + address;
    const std::string url_fallback = "https://tonapi.io/v2/blockchain/accounts/" + address;
#ifdef _WIN32
    std::string ps_url = url_primary;
    for (std::size_t pos = 0; (pos = ps_url.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_url.replace(pos, 1, "''");
    }
    const std::string command =
        "powershell -NoProfile -Command \"(Invoke-WebRequest -UseBasicParsing '" + ps_url + "').Content\"";
    const std::string response = run_command(command);
#else
    const std::string command_primary =
        "curl -fsSL --max-time 10 -H 'accept: application/json' -H 'user-agent: Mozilla/5.0' '" +
        shell_escape_single_quote(url_primary) + "'";
    const std::string command_fallback =
        "curl -fsSL --max-time 10 -H 'accept: application/json' -H 'user-agent: Mozilla/5.0' '" +
        shell_escape_single_quote(url_fallback) + "'";

    std::string response = run_command(command_primary);
    if (parse_nanotons_response(response) <= 0.0) {
        response = run_command(command_fallback);
    }
#endif

    const double nanotons = parse_nanotons_response(response);
    return nanotons / 1000000000.0;
}

} // namespace chains

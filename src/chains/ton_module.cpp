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
std::string toncenter_api_key() {
    const char* value = std::getenv("TONCENTER_API_KEY");
    if (value == nullptr) {
        return {};
    }
    return value;
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
    const std::string url =
        "https://toncenter.com/api/v3/accountStates?address=" + address + "&include_boc=false";
    const std::string api_key = toncenter_api_key();
#ifdef _WIN32
    std::string command =
        "curl.exe -fsSL --max-time 10 -H \"accept: application/json\" -H \"origin: https://tonscan.org\" "
        "-H \"referer: https://tonscan.org/\" -H \"user-agent: Mozilla/5.0\" ";
    if (!api_key.empty()) {
        command += "-H \"x-api-key: " + api_key + "\" ";
    }
    command += "\"" + url + "\"";
    const std::string response = run_command(command);
#else
    const std::string command =
        "curl -fsSL --max-time 10 -H 'accept: application/json' -H 'origin: https://tonscan.org' "
        "-H 'referer: https://tonscan.org/' -H 'user-agent: Mozilla/5.0' " +
        (api_key.empty() ? std::string() : ("-H 'x-api-key: " + shell_escape_single_quote(api_key) + "' ")) + "'" +
        shell_escape_single_quote(url) + "'";
    const std::string response = run_command(command);
#endif

    const double nanotons = parse_nanotons_response(response);
    return nanotons / 1000000000.0;
}

} // namespace chains

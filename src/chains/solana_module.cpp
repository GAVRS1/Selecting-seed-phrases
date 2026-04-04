#include "chains/solana_module.hpp"
#include "common.hpp"

#include <array>
#include <cstdio>
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

std::string SolanaModule::name() const { return "sol"; }
std::string SolanaModule::coin_ticker() const { return "SOL"; }

std::vector<std::string> SolanaModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_sol_addresses(seed, derivation_paths, account_scan_limit);
}

double SolanaModule::fetch_balance_coin(const std::string& address) {
    const std::string payload =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getBalance\",\"params\":[\"" + address + "\"]}";
#ifdef _WIN32
    std::string ps_payload = payload;
    for (std::size_t pos = 0; (pos = ps_payload.find('\'', pos)) != std::string::npos; pos += 2) {
        ps_payload.replace(pos, 1, "''");
    }
    const std::string command =
        "powershell -NoProfile -Command \"$b='" + ps_payload +
        "'; (Invoke-WebRequest -UseBasicParsing -Method Post -Uri 'https://api.mainnet-beta.solana.com' "
        "-ContentType 'application/json' -Body $b).Content\"";
#else
    const std::string command =
        "curl -fsSL --max-time 10 -H 'Content-Type: application/json' -d '" +
        shell_escape_single_quote(payload) + "' 'https://api.mainnet-beta.solana.com'";
#endif
    const std::string response = run_command(command);

    std::smatch m;
    if (!std::regex_search(response, m, std::regex(R"("value"\s*:\s*([0-9]+))"))) {
        return 0.0;
    }

    const long double lamports = std::stold(m[1].str());
    return static_cast<double>(lamports / 1000000000.0L);
}

} // namespace chains

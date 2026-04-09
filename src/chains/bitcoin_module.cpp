#include "chains/bitcoin_module.hpp"
#include "common.hpp"

namespace chains {

std::string BitcoinModule::name() const { return "btc"; }

std::vector<std::string> BitcoinModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_btc_addresses(seed, derivation_paths, account_scan_limit);
}

} // namespace chains

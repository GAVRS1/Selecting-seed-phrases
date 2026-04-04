#include "chains/ethereum_module.hpp"
#include "common.hpp"

namespace chains {

std::string EthereumModule::name() const { return "eth"; }

std::vector<std::string> EthereumModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return pseudo_derive("eth", seed, derivation_paths, account_scan_limit);
}

} // namespace chains

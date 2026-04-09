#include "chains/solana_module.hpp"
#include "common.hpp"

namespace chains {

std::string SolanaModule::name() const { return "sol"; }

std::vector<std::string> SolanaModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_sol_addresses(seed, derivation_paths, account_scan_limit);
}

} // namespace chains

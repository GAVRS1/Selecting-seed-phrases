#include "chains/ton_module.hpp"
#include "common.hpp"

namespace chains {

std::string TonModule::name() const { return "ton"; }

std::vector<std::string> TonModule::derive_addresses(
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    return derive_ton_addresses(seed, derivation_paths, account_scan_limit);
}

} // namespace chains

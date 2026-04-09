#pragma once

#include "chains/i_chain_module.hpp"

namespace chains {

class TonModule : public IChainModule {
public:
    std::string name() const override;
    std::vector<std::string> derive_addresses(
        const core::SecureBuffer& seed,
        const std::vector<std::string>& derivation_paths,
        std::uint32_t account_scan_limit) override;
};

} // namespace chains

#pragma once

#include "core/secure_buffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace chains {

class IChainModule {
public:
    virtual ~IChainModule() = default;
    virtual std::string name() const = 0;
    virtual std::string coin_ticker() const = 0;
    virtual std::vector<std::string> derive_addresses(
        const core::SecureBuffer& seed,
        const std::vector<std::string>& derivation_paths,
        std::uint32_t account_scan_limit) = 0;
    virtual double fetch_balance_coin(const std::string& address) = 0;
};

} // namespace chains

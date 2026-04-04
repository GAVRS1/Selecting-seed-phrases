#pragma once

#include "core/secure_buffer.hpp"

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace chains {

inline std::vector<std::string> pseudo_derive(
    const std::string& prefix,
    const core::SecureBuffer& seed,
    const std::vector<std::string>& derivation_paths,
    std::uint32_t account_scan_limit) {
    std::vector<std::string> out;
    std::hash<std::string> hasher;

    for (const auto& path : derivation_paths) {
        for (std::uint32_t i = 0; i < account_scan_limit; ++i) {
            std::ostringstream msg;
            msg << prefix << ":" << path << ":" << i << ":" << seed.size();
            const auto h = hasher(msg.str());
            std::ostringstream address;
            address << prefix << "_" << std::hex << h;
            out.push_back(address.str());
        }
    }

    return out;
}

} // namespace chains

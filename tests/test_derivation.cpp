#include "chains/bitcoin_module.hpp"
#include "chains/ethereum_module.hpp"
#include "core/secure_buffer.hpp"

#include <cassert>
#include <iostream>

int main() {
    chains::BitcoinModule module;
    core::SecureBuffer seed(std::vector<std::uint8_t>{1, 2, 3, 4});
    auto addresses = module.derive_addresses(seed, {"m/84'/0'/0'/0/{i}"}, 3);
    assert(addresses.size() == 3);

    chains::EthereumModule eth_module;
    auto eth_addresses = eth_module.derive_addresses(seed, {"m/44'/60'/0'/0/{i}"}, 2);
    assert(eth_addresses.size() == 2);
    for (const auto& address : eth_addresses) {
        assert(address.rfind("0x", 0) == 0);
        assert(address.size() == 42);
    }

    std::cout << "test_derivation passed\n";
    return 0;
}

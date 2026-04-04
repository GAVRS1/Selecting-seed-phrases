#include "chains/bitcoin_module.hpp"
#include "core/secure_buffer.hpp"

#include <cassert>
#include <iostream>

int main() {
    chains::BitcoinModule module;
    core::SecureBuffer seed(std::vector<std::uint8_t>{1, 2, 3, 4});
    auto addresses = module.derive_addresses(seed, {"m/84'/0'/0'/0/{i}"}, 3);
    assert(addresses.size() == 3);
    std::cout << "test_derivation passed\n";
    return 0;
}

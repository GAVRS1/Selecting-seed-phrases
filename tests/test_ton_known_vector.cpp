#include "chains/ton_module.hpp"
#include "core/secure_buffer.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
core::SecureBuffer ton_private_key_seed_from_mnemonic(const std::vector<std::string>& mnemonic_words) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < mnemonic_words.size(); ++i) {
        joined << mnemonic_words[i];
        if (i + 1 < mnemonic_words.size()) {
            joined << ' ';
        }
    }

    const std::string phrase = joined.str();
    std::vector<std::uint8_t> entropy(64, 0);
    unsigned int entropy_len = static_cast<unsigned int>(entropy.size());
    HMAC(EVP_sha512(),
         phrase.data(),
         static_cast<int>(phrase.size()),
         nullptr,
         0,
         entropy.data(),
         &entropy_len);
    assert(entropy_len == entropy.size());

    std::vector<std::uint8_t> seed(64, 0);
    static constexpr char kTonDefaultSeed[] = "TON default seed";
    const int ok = PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(entropy.data()),
                                     static_cast<int>(entropy.size()),
                                     reinterpret_cast<const unsigned char*>(kTonDefaultSeed),
                                     static_cast<int>(sizeof(kTonDefaultSeed) - 1),
                                     100000,
                                     EVP_sha512(),
                                     static_cast<int>(seed.size()),
                                     seed.data());
    assert(ok == 1);
    return core::SecureBuffer(std::vector<std::uint8_t>(seed.begin(), seed.begin() + 32));
}
} // namespace

int main() {
    const std::vector<std::string> mnemonic = {"swing",   "someone", "work",     "skull",  "silver",  "faculty",
                                               "problem", "asthma",  "sure",     "pitch",  "parent",  "carpet",
                                               "raven",   "hen",     "pyramid",  "skin",   "congress","arctic",
                                               "banana",  "doctor",  "lumber",   "relax",  "that",    "excess"};
    const std::string expected = "UQBSZPbwivhaWqfzml0Qr+gt6SMYj6+IRV5nDkB0xKq+uTZN";

    chains::TonModule ton;
    const auto private_seed = ton_private_key_seed_from_mnemonic(mnemonic);
    const auto addresses = ton.derive_addresses(private_seed, {"m/44'/607'/0'/{i}'"}, 20);

    assert(addresses.size() == 1);
    assert(addresses.front() == expected);
    std::cout << "test_ton_known_vector passed\n";
    return 0;
}

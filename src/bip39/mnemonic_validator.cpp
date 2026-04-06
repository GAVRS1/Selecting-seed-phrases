#include "bip39/mnemonic_validator.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace {

constexpr std::array<std::uint32_t, 64> kSha256Constants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::array<std::uint8_t, 32> sha256(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> message = data;
    const std::uint64_t bit_len = static_cast<std::uint64_t>(message.size()) * 8;

    message.push_back(0x80);
    while ((message.size() % 64) != 56) {
        message.push_back(0x00);
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bit_len >> shift) & 0xff));
    }

    std::uint32_t h0 = 0x6a09e667;
    std::uint32_t h1 = 0xbb67ae85;
    std::uint32_t h2 = 0x3c6ef372;
    std::uint32_t h3 = 0xa54ff53a;
    std::uint32_t h4 = 0x510e527f;
    std::uint32_t h5 = 0x9b05688c;
    std::uint32_t h6 = 0x1f83d9ab;
    std::uint32_t h7 = 0x5be0cd19;

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t j = offset + i * 4;
            w[i] = (static_cast<std::uint32_t>(message[j]) << 24) |
                   (static_cast<std::uint32_t>(message[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(message[j + 2]) << 8) |
                   static_cast<std::uint32_t>(message[j + 3]);
        }

        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        std::uint32_t f = h5;
        std::uint32_t g = h6;
        std::uint32_t h = h7;

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + s1 + ch + kSha256Constants[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
        h5 += f;
        h6 += g;
        h7 += h;
    }

    std::array<std::uint8_t, 32> hash{};
    const std::array<std::uint32_t, 8> digest{h0, h1, h2, h3, h4, h5, h6, h7};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        hash[i * 4] = static_cast<std::uint8_t>((digest[i] >> 24) & 0xff);
        hash[i * 4 + 1] = static_cast<std::uint8_t>((digest[i] >> 16) & 0xff);
        hash[i * 4 + 2] = static_cast<std::uint8_t>((digest[i] >> 8) & 0xff);
        hash[i * 4 + 3] = static_cast<std::uint8_t>(digest[i] & 0xff);
    }

    return hash;
}

bool get_bit(const std::vector<std::uint8_t>& bytes, std::size_t bit_index) {
    const std::size_t byte_index = bit_index / 8;
    const std::size_t bit_in_byte = 7 - (bit_index % 8);
    return ((bytes[byte_index] >> bit_in_byte) & 0x01U) != 0;
}

} // namespace

namespace bip39 {

bool MnemonicValidator::is_valid_length(const core::Mnemonic& mnemonic) const {
    static const std::set<std::size_t> lengths{12, 15, 18, 21, 24};
    return lengths.contains(mnemonic.size());
}

bool MnemonicValidator::all_words_known(const core::Mnemonic& mnemonic) const {
    for (const auto& word : mnemonic) {
        if (!wordlist_.contains(word)) {
            return false;
        }
    }
    return true;
}

bool MnemonicValidator::is_checksum_valid(const core::Mnemonic& mnemonic) const {
    if (!wordlist_.has_full_bip39_english_size()) {
        return true;
    }

    if (!is_valid_length(mnemonic)) {
        return false;
    }

    const std::size_t total_bits = mnemonic.size() * 11;
    const std::size_t checksum_bits = total_bits / 33;
    const std::size_t entropy_bits = total_bits - checksum_bits;

    std::vector<bool> bits;
    bits.reserve(total_bits);

    for (const auto& word : mnemonic) {
        const int index = wordlist_.index_of(word);
        if (index < 0 || index > 2047) {
            return false;
        }

        for (int shift = 10; shift >= 0; --shift) {
            bits.push_back(((index >> shift) & 1) != 0);
        }
    }

    std::vector<std::uint8_t> entropy(entropy_bits / 8, 0);
    for (std::size_t i = 0; i < entropy_bits; ++i) {
        if (bits[i]) {
            entropy[i / 8] |= static_cast<std::uint8_t>(1U << (7 - (i % 8)));
        }
    }

    const auto hash = sha256(entropy);
    const std::vector<std::uint8_t> hash_bytes(hash.begin(), hash.end());

    for (std::size_t i = 0; i < checksum_bits; ++i) {
        if (bits[entropy_bits + i] != get_bit(hash_bytes, i)) {
            return false;
        }
    }

    return true;
}

bool MnemonicValidator::validate(const core::Mnemonic& mnemonic) const {
    return is_valid_length(mnemonic) && all_words_known(mnemonic) && is_checksum_valid(mnemonic);
}

} // namespace bip39

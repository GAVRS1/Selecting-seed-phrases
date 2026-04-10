#include "common.hpp"

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/ripemd.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace chains {
namespace {

constexpr std::uint32_t kHardenedOffset = 0x80000000u;

using Bytes = std::vector<std::uint8_t>;

struct ExtendedPrivateKey {
    std::array<std::uint8_t, 32> key{};
    std::array<std::uint8_t, 32> chain_code{};
};

struct Ed25519Node {
    std::array<std::uint8_t, 32> key{};
    std::array<std::uint8_t, 32> chain_code{};
};

using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using EcGroupPtr = std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)>;
using EcPointPtr = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;
using EcKeyPtr = std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)>;
using CtxPtr = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;
using PKeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

std::string trim(const std::string& s) {
    const auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
    const auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (start >= end) return "";
    return std::string(start, end);
}

std::vector<std::uint32_t> parse_path(const std::string& path_raw, std::uint32_t index) {
    std::string path = path_raw;
    auto pos = path.find("{i}");
    if (pos != std::string::npos) {
        path.replace(pos, 3, std::to_string(index));
    }

    if (path.empty() || path[0] != 'm') {
        throw std::invalid_argument("Invalid derivation path: " + path_raw);
    }

    std::vector<std::uint32_t> result;
    std::stringstream ss(path);
    std::string segment;
    std::getline(ss, segment, '/'); // m

    while (std::getline(ss, segment, '/')) {
        segment = trim(segment);
        bool hardened = false;
        if (!segment.empty() && (segment.back() == '\'' || segment.back() == 'h' || segment.back() == 'H')) {
            hardened = true;
            segment.pop_back();
        }

        if (segment.empty()) {
            throw std::invalid_argument("Invalid derivation path segment: " + path_raw);
        }

        std::uint64_t value = std::stoull(segment);
        if (value > 0x7fffffffULL) {
            throw std::invalid_argument("Path segment too large: " + path_raw);
        }

        auto child = static_cast<std::uint32_t>(value);
        if (hardened) {
            child |= kHardenedOffset;
        }
        result.push_back(child);
    }

    return result;
}

std::array<std::uint8_t, 64> hmac_sha512(const std::uint8_t* key,
                                         std::size_t key_len,
                                         const std::uint8_t* data,
                                         std::size_t data_len) {
    std::array<std::uint8_t, 64> out{};
    unsigned int out_len = out.size();
    HMAC(EVP_sha512(), key, static_cast<int>(key_len), data, data_len, out.data(), &out_len);
    return out;
}

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, 32> out{};
    SHA256(data, len, out.data());
    return out;
}

std::array<std::uint8_t, 20> hash160(const std::uint8_t* data, std::size_t len) {
    auto h1 = sha256(data, len);
    std::array<std::uint8_t, 20> out{};
    RIPEMD160(h1.data(), h1.size(), out.data());
    return out;
}

ExtendedPrivateKey master_secp256k1_from_seed(const core::SecureBuffer& seed) {
    static const std::string key = "Bitcoin seed";
    auto i = hmac_sha512(reinterpret_cast<const std::uint8_t*>(key.data()),
                         key.size(),
                         seed.data(),
                         seed.size());

    ExtendedPrivateKey xprv;
    std::copy_n(i.begin(), 32, xprv.key.begin());
    std::copy_n(i.begin() + 32, 32, xprv.chain_code.begin());
    return xprv;
}

std::array<std::uint8_t, 33> secp256k1_compressed_pubkey(const std::array<std::uint8_t, 32>& privkey) {
    EcGroupPtr group(EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
    if (!group) throw std::runtime_error("Failed to create EC group");

    CtxPtr ctx(BN_CTX_new(), BN_CTX_free);
    EcPointPtr pub(EC_POINT_new(group.get()), EC_POINT_free);
    BnPtr prv(BN_bin2bn(privkey.data(), privkey.size(), nullptr), BN_free);
    if (!ctx || !pub || !prv) throw std::runtime_error("Failed EC allocations");

    if (EC_POINT_mul(group.get(), pub.get(), prv.get(), nullptr, nullptr, ctx.get()) != 1) {
        throw std::runtime_error("Failed point multiplication");
    }

    std::array<std::uint8_t, 33> out{};
    const size_t len = EC_POINT_point2oct(group.get(), pub.get(), POINT_CONVERSION_COMPRESSED, out.data(), out.size(), ctx.get());
    if (len != out.size()) {
        throw std::runtime_error("Unexpected pubkey size");
    }

    return out;
}

std::array<std::uint8_t, 65> secp256k1_uncompressed_pubkey(const std::array<std::uint8_t, 32>& privkey) {
    EcGroupPtr group(EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
    if (!group) throw std::runtime_error("Failed to create EC group");

    CtxPtr ctx(BN_CTX_new(), BN_CTX_free);
    EcPointPtr pub(EC_POINT_new(group.get()), EC_POINT_free);
    BnPtr prv(BN_bin2bn(privkey.data(), privkey.size(), nullptr), BN_free);
    if (!ctx || !pub || !prv) throw std::runtime_error("Failed EC allocations");

    if (EC_POINT_mul(group.get(), pub.get(), prv.get(), nullptr, nullptr, ctx.get()) != 1) {
        throw std::runtime_error("Failed point multiplication");
    }

    std::array<std::uint8_t, 65> out{};
    const size_t len = EC_POINT_point2oct(group.get(), pub.get(), POINT_CONVERSION_UNCOMPRESSED, out.data(), out.size(), ctx.get());
    if (len != out.size()) {
        throw std::runtime_error("Unexpected pubkey size");
    }

    return out;
}

ExtendedPrivateKey derive_secp256k1_child(const ExtendedPrivateKey& parent, std::uint32_t index) {
    Bytes data;
    data.reserve(37);

    if (index & kHardenedOffset) {
        data.push_back(0x00);
        data.insert(data.end(), parent.key.begin(), parent.key.end());
    } else {
        auto pub = secp256k1_compressed_pubkey(parent.key);
        data.insert(data.end(), pub.begin(), pub.end());
    }

    data.push_back(static_cast<std::uint8_t>((index >> 24) & 0xff));
    data.push_back(static_cast<std::uint8_t>((index >> 16) & 0xff));
    data.push_back(static_cast<std::uint8_t>((index >> 8) & 0xff));
    data.push_back(static_cast<std::uint8_t>(index & 0xff));

    auto i = hmac_sha512(parent.chain_code.data(), parent.chain_code.size(), data.data(), data.size());

    EcGroupPtr group(EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
    CtxPtr ctx(BN_CTX_new(), BN_CTX_free);
    BnPtr order(BN_new(), BN_free);
    BnPtr il(BN_bin2bn(i.data(), 32, nullptr), BN_free);
    BnPtr kpar(BN_bin2bn(parent.key.data(), parent.key.size(), nullptr), BN_free);
    BnPtr child(BN_new(), BN_free);

    if (!group || !ctx || !order || !il || !kpar || !child) {
        throw std::runtime_error("BN/EC allocation failure");
    }

    if (EC_GROUP_get_order(group.get(), order.get(), ctx.get()) != 1) {
        throw std::runtime_error("Failed to get curve order");
    }

    if (BN_mod_add(child.get(), il.get(), kpar.get(), order.get(), ctx.get()) != 1 || BN_is_zero(child.get())) {
        throw std::runtime_error("Invalid derived key");
    }

    ExtendedPrivateKey out;
    if (BN_bn2binpad(child.get(), out.key.data(), out.key.size()) != static_cast<int>(out.key.size())) {
        throw std::runtime_error("Failed to serialize key");
    }
    std::copy_n(i.begin() + 32, 32, out.chain_code.begin());
    return out;
}

ExtendedPrivateKey derive_secp256k1(const core::SecureBuffer& seed, const std::vector<std::uint32_t>& path) {
    auto node = master_secp256k1_from_seed(seed);
    for (auto c : path) {
        node = derive_secp256k1_child(node, c);
    }
    return node;
}

std::string base58_encode(const std::vector<std::uint8_t>& input) {
    static const char* alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (input.empty()) return "";

    std::vector<std::uint8_t> digits((input.size() * 138 / 100) + 1, 0);
    std::size_t digit_len = 1;

    for (std::uint8_t byte : input) {
        int carry = byte;
        for (std::size_t j = 0; j < digit_len; ++j) {
            carry += digits[j] << 8;
            digits[j] = carry % 58;
            carry /= 58;
        }
        while (carry > 0) {
            digits[digit_len++] = carry % 58;
            carry /= 58;
        }
    }

    std::string out;
    for (std::uint8_t byte : input) {
        if (byte == 0x00) out.push_back('1');
        else break;
    }

    for (std::size_t i = 0; i < digit_len; ++i) {
        out.push_back(alphabet[digits[digit_len - 1 - i]]);
    }
    return out;
}

std::string btc_p2pkh_address(const std::array<std::uint8_t, 33>& compressed_pubkey) {
    auto h160 = hash160(compressed_pubkey.data(), compressed_pubkey.size());

    std::vector<std::uint8_t> payload;
    payload.reserve(25);
    payload.push_back(0x00); // mainnet P2PKH
    payload.insert(payload.end(), h160.begin(), h160.end());

    auto c1 = sha256(payload.data(), payload.size());
    auto c2 = sha256(c1.data(), c1.size());
    payload.insert(payload.end(), c2.begin(), c2.begin() + 4);

    return base58_encode(payload);
}

inline std::uint64_t rol64(std::uint64_t x, int s) {
    return (x << s) | (x >> (64 - s));
}

std::array<std::uint8_t, 32> keccak256(const std::uint8_t* data, std::size_t len) {
    static constexpr std::array<int, 25> rot = {
        0,  1, 62, 28, 27,
        36, 44, 6, 55, 20,
        3, 10, 43, 25, 39,
        41, 45, 15, 21, 8,
        18, 2, 61, 56, 14
    };
    static constexpr std::array<std::uint64_t, 24> rc = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
    };

    std::array<std::uint64_t, 25> st{};
    constexpr std::size_t rate = 136;

    auto permute = [&]() {
        for (int round = 0; round < 24; ++round) {
            std::array<std::uint64_t, 5> c{};
            for (int x = 0; x < 5; ++x) {
                c[x] = st[x] ^ st[x + 5] ^ st[x + 10] ^ st[x + 15] ^ st[x + 20];
            }
            std::array<std::uint64_t, 5> d{};
            for (int x = 0; x < 5; ++x) {
                d[x] = c[(x + 4) % 5] ^ rol64(c[(x + 1) % 5], 1);
            }
            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 5; ++x) {
                    st[x + 5 * y] ^= d[x];
                }
            }

            std::array<std::uint64_t, 25> b{};
            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 5; ++x) {
                    const int idx = x + 5 * y;
                    const int nx = y;
                    const int ny = (2 * x + 3 * y) % 5;
                    b[nx + 5 * ny] = rol64(st[idx], rot[idx]);
                }
            }

            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 5; ++x) {
                    st[x + 5 * y] = b[x + 5 * y] ^ ((~b[((x + 1) % 5) + 5 * y]) & b[((x + 2) % 5) + 5 * y]);
                }
            }

            st[0] ^= rc[round];
        }
    };

    std::size_t offset = 0;
    while (offset + rate <= len) {
        for (std::size_t i = 0; i < rate / 8; ++i) {
            std::uint64_t lane = 0;
            for (int b = 0; b < 8; ++b) {
                lane |= static_cast<std::uint64_t>(data[offset + i * 8 + b]) << (8 * b);
            }
            st[i] ^= lane;
        }
        permute();
        offset += rate;
    }

    std::array<std::uint8_t, rate> block{};
    const std::size_t rem = len - offset;
    if (rem > 0) {
        std::memcpy(block.data(), data + offset, rem);
    }
    block[rem] = 0x01; // Keccak padding
    block[rate - 1] |= 0x80;

    for (std::size_t i = 0; i < rate / 8; ++i) {
        std::uint64_t lane = 0;
        for (int b = 0; b < 8; ++b) {
            lane |= static_cast<std::uint64_t>(block[i * 8 + b]) << (8 * b);
        }
        st[i] ^= lane;
    }
    permute();

    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (int b = 0; b < 8; ++b) {
            out[i * 8 + b] = static_cast<std::uint8_t>((st[i] >> (8 * b)) & 0xff);
        }
    }
    return out;
}

std::string bytes_to_hex(const std::uint8_t* data, std::size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

std::uint16_t crc16_xmodem(const std::uint8_t* data, std::size_t len) {
    std::uint16_t crc = 0x0000;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000) != 0) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

std::string base64_encode(const std::uint8_t* data, std::size_t len) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3) {
        const std::uint32_t b0 = data[i];
        const std::uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
        const std::uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
        const std::uint32_t chunk = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kAlphabet[(chunk >> 18) & 0x3f]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3f]);
        out.push_back(i + 1 < len ? kAlphabet[(chunk >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < len ? kAlphabet[chunk & 0x3f] : '=');
    }
    return out;
}

std::string eth_checksum_address(const std::array<std::uint8_t, 20>& address_bytes) {
    const std::string lower_hex = bytes_to_hex(address_bytes.data(), address_bytes.size());
    const auto hash = keccak256(reinterpret_cast<const std::uint8_t*>(lower_hex.data()), lower_hex.size());
    const std::string hash_hex = bytes_to_hex(hash.data(), hash.size());

    std::string checksummed;
    checksummed.reserve(42);
    checksummed += "0x";
    for (std::size_t i = 0; i < lower_hex.size(); ++i) {
        const char c = lower_hex[i];
        if (c >= 'a' && c <= 'f') {
            const int nibble = (hash_hex[i] >= '0' && hash_hex[i] <= '9')
                                   ? (hash_hex[i] - '0')
                                   : (10 + (hash_hex[i] - 'a'));
            checksummed.push_back(nibble >= 8 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        } else {
            checksummed.push_back(c);
        }
    }
    return checksummed;
}

Ed25519Node master_ed25519_from_seed(const core::SecureBuffer& seed) {
    static const std::string key = "ed25519 seed";
    auto i = hmac_sha512(reinterpret_cast<const std::uint8_t*>(key.data()),
                         key.size(),
                         seed.data(),
                         seed.size());
    Ed25519Node node;
    std::copy_n(i.begin(), 32, node.key.begin());
    std::copy_n(i.begin() + 32, 32, node.chain_code.begin());
    return node;
}

Ed25519Node derive_ed25519_hardened_child(const Ed25519Node& parent, std::uint32_t index) {
    if ((index & kHardenedOffset) == 0) {
        throw std::invalid_argument("ed25519 only supports hardened derivation in this implementation");
    }

    Bytes data;
    data.reserve(37);
    data.push_back(0x00);
    data.insert(data.end(), parent.key.begin(), parent.key.end());
    data.push_back(static_cast<std::uint8_t>((index >> 24) & 0xff));
    data.push_back(static_cast<std::uint8_t>((index >> 16) & 0xff));
    data.push_back(static_cast<std::uint8_t>((index >> 8) & 0xff));
    data.push_back(static_cast<std::uint8_t>(index & 0xff));

    auto i = hmac_sha512(parent.chain_code.data(), parent.chain_code.size(), data.data(), data.size());

    Ed25519Node out;
    std::copy_n(i.begin(), 32, out.key.begin());
    std::copy_n(i.begin() + 32, 32, out.chain_code.begin());
    return out;
}

Ed25519Node derive_ed25519(const core::SecureBuffer& seed, const std::vector<std::uint32_t>& path) {
    auto node = master_ed25519_from_seed(seed);
    for (auto c : path) {
        node = derive_ed25519_hardened_child(node, c);
    }
    return node;
}

std::string sol_address_from_private(const std::array<std::uint8_t, 32>& private_key) {
    PKeyPtr pkey(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, private_key.data(), private_key.size()), EVP_PKEY_free);
    if (!pkey) {
        throw std::runtime_error("Failed to build ed25519 private key");
    }

    std::array<std::uint8_t, 32> pub{};
    size_t pub_len = pub.size();
    if (EVP_PKEY_get_raw_public_key(pkey.get(), pub.data(), &pub_len) != 1 || pub_len != pub.size()) {
        throw std::runtime_error("Failed to extract ed25519 public key");
    }

    std::vector<std::uint8_t> bytes(pub.begin(), pub.end());
    return base58_encode(bytes);
}

std::string ton_address_from_private(const std::array<std::uint8_t, 32>& private_key) {
    PKeyPtr pkey(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, private_key.data(), private_key.size()), EVP_PKEY_free);
    if (!pkey) {
        throw std::runtime_error("Failed to build ed25519 private key");
    }

    std::array<std::uint8_t, 32> pub{};
    size_t pub_len = pub.size();
    if (EVP_PKEY_get_raw_public_key(pkey.get(), pub.data(), &pub_len) != 1 || pub_len != pub.size()) {
        throw std::runtime_error("Failed to extract ed25519 public key");
    }

    const auto hash = sha256(pub.data(), pub.size());
    std::array<std::uint8_t, 36> payload{};
    payload[0] = 0x11; // bounceable, mainnet
    payload[1] = 0x00; // workchain 0
    std::copy(hash.begin(), hash.end(), payload.begin() + 2);
    const std::uint16_t crc = crc16_xmodem(payload.data(), 34);
    payload[34] = static_cast<std::uint8_t>((crc >> 8) & 0xff);
    payload[35] = static_cast<std::uint8_t>(crc & 0xff);
    return base64_encode(payload.data(), payload.size());
}

} // namespace

std::vector<std::string> derive_btc_addresses(const core::SecureBuffer& seed,
                                              const std::vector<std::string>& derivation_paths,
                                              std::uint32_t account_scan_limit) {
    std::vector<std::string> out;
    for (const auto& raw_path : derivation_paths) {
        for (std::uint32_t i = 0; i < account_scan_limit; ++i) {
            auto path = parse_path(raw_path, i);
            auto node = derive_secp256k1(seed, path);
            auto pub = secp256k1_compressed_pubkey(node.key);
            out.push_back(btc_p2pkh_address(pub));
        }
    }
    return out;
}

std::vector<std::string> derive_eth_addresses(const core::SecureBuffer& seed,
                                              const std::vector<std::string>& derivation_paths,
                                              std::uint32_t account_scan_limit) {
    std::vector<std::string> out;
    for (const auto& raw_path : derivation_paths) {
        for (std::uint32_t i = 0; i < account_scan_limit; ++i) {
            auto path = parse_path(raw_path, i);
            auto node = derive_secp256k1(seed, path);
            auto pub = secp256k1_uncompressed_pubkey(node.key);
            auto h = keccak256(pub.data() + 1, 64);
            std::array<std::uint8_t, 20> address_bytes{};
            std::copy_n(h.begin() + 12, address_bytes.size(), address_bytes.begin());
            out.push_back(eth_checksum_address(address_bytes));
        }
    }
    return out;
}

std::vector<std::string> derive_sol_addresses(const core::SecureBuffer& seed,
                                              const std::vector<std::string>& derivation_paths,
                                              std::uint32_t account_scan_limit) {
    std::vector<std::string> out;
    for (const auto& raw_path : derivation_paths) {
        for (std::uint32_t i = 0; i < account_scan_limit; ++i) {
            auto path = parse_path(raw_path, i);
            auto node = derive_ed25519(seed, path);
            out.push_back(sol_address_from_private(node.key));
        }
    }
    return out;
}

std::vector<std::string> derive_ton_addresses(const core::SecureBuffer& seed,
                                              const std::vector<std::string>& derivation_paths,
                                              std::uint32_t account_scan_limit) {
    std::vector<std::string> out;
    for (const auto& raw_path : derivation_paths) {
        for (std::uint32_t i = 0; i < account_scan_limit; ++i) {
            auto path = parse_path(raw_path, i);
            auto node = derive_ed25519(seed, path);
            out.push_back(ton_address_from_private(node.key));
        }
    }
    return out;
}

} // namespace chains

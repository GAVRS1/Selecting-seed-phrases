#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    SecureBuffer(const SecureBuffer&) = default;
    SecureBuffer& operator=(const SecureBuffer&) = default;

    SecureBuffer(SecureBuffer&& other) noexcept : bytes_(std::move(other.bytes_)) {
        other.secure_wipe();
    }

    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
        if (this != &other) {
            secure_wipe();
            bytes_ = std::move(other.bytes_);
            other.secure_wipe();
        }
        return *this;
    }

    ~SecureBuffer() { secure_wipe(); }

    const std::uint8_t* data() const { return bytes_.data(); }
    std::size_t size() const { return bytes_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    void secure_wipe() {
        volatile std::uint8_t* p = bytes_.data();
        for (std::size_t i = 0; i < bytes_.size(); ++i) {
            p[i] = 0;
        }
        bytes_.clear();
        bytes_.shrink_to_fit();
    }

    std::vector<std::uint8_t> bytes_;
};

} // namespace core

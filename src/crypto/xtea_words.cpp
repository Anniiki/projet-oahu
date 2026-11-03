#include "tdur/crypto/xtea_words.hpp"

namespace tdur::crypto {

XteaWords encrypt_xtea_words(XteaWords words, const XteaKey& key) noexcept {
    auto [left, right] = words;
    std::uint32_t sum{};
    constexpr std::uint32_t delta = 0x9e3779b9U;
    for (unsigned round = 0; round < 32; ++round) {
        left +=
            (((right << 4U) ^ (right >> 5U)) + right) ^ (sum + key[sum & 3U]);
        sum += delta;
        right += (((left << 4U) ^ (left >> 5U)) + left) ^
                 (sum + key[(sum >> 11U) & 3U]);
    }
    return {left, right};
}

XteaWords decrypt_xtea_words(XteaWords words, const XteaKey& key) noexcept {
    auto [left, right] = words;
    std::uint32_t sum = 0xc6ef3720U;
    constexpr std::uint32_t delta = 0x9e3779b9U;
    for (unsigned round = 0; round < 32; ++round) {
        right -= (((left << 4U) ^ (left >> 5U)) + left) ^
                 (sum + key[(sum >> 11U) & 3U]);
        sum -= delta;
        left -=
            (((right << 4U) ^ (right >> 5U)) + right) ^ (sum + key[sum & 3U]);
    }
    return {left, right};
}

} // namespace tdur::crypto

#pragma once

#include <array>
#include <cstdint>

namespace tdur::crypto {

using XteaWords = std::array<std::uint32_t, 2>;
using XteaKey = std::array<std::uint32_t, 4>;

// Native6DDDE0 /6DDD70:32 rounds, unsigned DWORD arithmetic throughout.
// Inputs are WORDS; byte order, CBC/IV and key acquisition belong to callers.
// Both input words are captured before the first key access. No game constant,
// activation data, allocation or native call is embedded in these primitives.
// Keep the supplied key valid and serialize mutation during an invocation.
[[nodiscard]] XteaWords encrypt_xtea_words(XteaWords words,
                                           const XteaKey& key) noexcept;
[[nodiscard]] XteaWords decrypt_xtea_words(XteaWords words,
                                           const XteaKey& key) noexcept;

} // namespace tdur::crypto

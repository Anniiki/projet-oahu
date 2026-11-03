#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace tdur::assets {

inline constexpr std::uint64_t resource_hash_seed = 0x0cd5e44afac432b1ULL;
enum class ResourceHashError {
    count_exceeds_input,
    input_exceeds_signed_count
};

// Independent C++ port of 504980/117 through thunk504920/5. The original
// signed count <= 0 returns the seed without reading input; positive count
// hashes only that prefix. A span-short prefix is explicitly refused by the
// port. No runtime calls, pointer/ABI reuse, final XOR or name normalization.
[[nodiscard]] std::expected<std::uint64_t, ResourceHashError>
hash_resource_bytes(std::span<const std::byte> input,
                    std::int32_t count) noexcept;

// Full-span convenience; refuses a length not representable by the native
// signed count. In particular, no narrowing or integer wrap selects a prefix.
[[nodiscard]] std::expected<std::uint64_t, ResourceHashError>
hash_resource_bytes(std::span<const std::byte> input) noexcept;

// Canonical eight lookup-key bytes: little-endian EAX then EDX. This is not
// a file name, authentication result or security hash.
[[nodiscard]] std::array<std::byte, 8>
resource_hash_key_bytes(std::uint64_t hash) noexcept;

} // namespace tdur::assets

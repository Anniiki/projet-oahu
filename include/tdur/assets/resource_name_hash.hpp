#pragma once

#include "tdur/assets/resource_hash.hpp"

#include <string_view>

namespace tdur::assets {

// 42E1E0: eight byte lanes, ASCII a..z uppercased, additions modulo256.
// Nonpositive signed count returns zero without reading input. Positive
// count consumes exactly that prefix, including embedded NUL and raw high
// bytes. A short span is a modern explicit error, not an out-of-bounds read.
[[nodiscard]] std::expected<std::uint64_t, ResourceHashError>
hash_legacy_resource_bytes(std::span<const std::byte> input,
                           std::int32_t count) noexcept;

// 504930: first-C-NUL name prefix, then legacy42E1E0 if mode!=0, otherwise
// the existing504980 backend. The bounded view end implies NUL; storage must
// be stable/readable throughout. Mode is supplied by value, corresponding
// to the native global-byte read before strlen. No BSS default or current
// game-global binding is invented. Lengths beyond signed count are refused.
// No Unicode/locale/slash normalization. Logical64 result, not native ABI.
[[nodiscard]] std::expected<std::uint64_t, ResourceHashError>
hash_resource_name(std::string_view name, std::uint8_t mode) noexcept;

} // namespace tdur::assets

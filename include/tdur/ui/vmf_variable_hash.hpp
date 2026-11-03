#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace tdur::ui {

enum class VmfVariableHashError { zero_capacity };

// Secondary PC443390..443401, evidence ev_f77c4d11...; see the evidence doc.
// Byte strings, not Unicode: first NUL or view end terminates the name.
// The seed is the scope. Four-byte groups use explicit little-endian packing
// and DF masks on EVERY byte. A partial group retains the preceding masked
// word's unwritten bytes. Addition wraps to32 bits before unsigned modulo.
// An empty name returns0 even if scope/capacity are nonzero/zero respectively.
// Nonempty input at capacity0 is refused instead of the native divide fault.
// Borrowed only for the call; no allocation, locale or floating-point access.
[[nodiscard]] std::expected<std::uint32_t, VmfVariableHashError>
vmf_variable_name_hash(std::uint32_t scope, std::string_view name,
                       std::uint32_t capacity) noexcept;

} // namespace tdur::ui

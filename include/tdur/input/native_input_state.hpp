#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tdur::input {

// 433DF0's slot pointer is represented by an optional ID, not an address.
// A NULL slot ends the search; ID0 and returned index0 are legitimate values.
// The span is the declared valid prefix. Unlike native prefetches, this API
// never reads before an empty prefix or one slot beyond its end. No device
// role, physical native capacity, or virtual-method behavior is inferred.
[[nodiscard]] std::optional<std::size_t>
find_native_input_slot(std::span<const std::optional<std::uint32_t>> slots,
                       std::uint32_t id, std::uint32_t ordinal) noexcept;

struct NativeInputRecord {
    std::array<std::byte, 0x54> bytes{};
};
// 434AB0: mask byte8/9 and overwrite raw bits30 with positive zero. All other
// bytes are preserved; no floating-point load/arithmetic or allocation occurs.
// Only the supplied span changes. Its count is a modern checked extent.
void reset_native_input_records(std::span<NativeInputRecord> records) noexcept;

} // namespace tdur::input

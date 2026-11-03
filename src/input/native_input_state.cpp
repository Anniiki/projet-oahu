#include "tdur/input/native_input_state.hpp"

namespace tdur::input {

std::optional<std::size_t>
find_native_input_slot(std::span<const std::optional<std::uint32_t>> slots,
                       std::uint32_t id, std::uint32_t ordinal) noexcept {
    std::uint32_t matched = 0;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (!slots[index])
            return std::nullopt;
        if (*slots[index] == id) {
            if (matched == ordinal)
                return index;
            ++matched;
        }
    }
    return std::nullopt;
}

void reset_native_input_records(std::span<NativeInputRecord> records) noexcept {
    for (auto &record : records) {
        record.bytes[8] &= std::byte{0x0f};
        record.bytes[9] &= std::byte{0xfe};
        for (std::size_t index = 0x30; index != 0x34; ++index)
            record.bytes[index] = std::byte{};
    }
}

} // namespace tdur::input

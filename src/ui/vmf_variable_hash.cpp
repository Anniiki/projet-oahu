#include "tdur/ui/vmf_variable_hash.hpp"

#include <cstddef>
#include <limits>

namespace tdur::ui {

std::expected<std::uint32_t, VmfVariableHashError>
vmf_variable_name_hash(std::uint32_t scope, std::string_view name,
                       std::uint32_t capacity) noexcept {
    static_assert(std::numeric_limits<unsigned char>::digits == 8);
    if (name.empty() || name.front() == '\0')
        return 0;
    if (capacity == 0)
        return std::unexpected(VmfVariableHashError::zero_capacity);

    auto sum = scope;
    std::uint32_t word = 0;
    std::size_t cursor = 0;
    while (cursor < name.size() && name[cursor] != '\0') {
        for (std::uint32_t byte = 0;
             byte < 4 && cursor < name.size() && name[cursor] != '\0';
             ++byte, ++cursor) {
            const auto shift = byte * 8;
            const auto octet = static_cast<std::uint32_t>(
                static_cast<unsigned char>(name[cursor]));
            word = (word & ~(0xffU << shift)) | (octet << shift);
        }
        word &= 0xdfdfdfdfU;
        sum += word;
    }
    return sum % capacity;
}

} // namespace tdur::ui

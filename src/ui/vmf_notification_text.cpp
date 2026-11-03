#include "tdur/ui/vmf_notification_text.hpp"

#include <algorithm>
#include <limits>

namespace tdur::ui {
namespace {
bool overlap(const void* first, std::size_t first_size, const void* second,
             std::size_t second_size) noexcept {
    if (!first_size || !second_size)
        return false;
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(second);
    return a <= b ? b - a < first_size : a - b < second_size;
}
std::string_view c_prefix(std::string_view text) noexcept {
    const auto end = text.find('\0');
    return text.substr(0, end);
}
} // namespace

std::expected<VmfNotificationByteEncoding, VmfNotificationTextError>
encode_vmf_notification_bytes(std::span<char> destination,
                              std::span<const char> source,
                              std::int32_t initial_guard,
                              std::int32_t source_limit) noexcept {
    if (initial_guard <= 0)
        return VmfNotificationByteEncoding{};
    if (source.empty())
        return std::unexpected(VmfNotificationTextError::source_range);
    const auto limit = source_limit == -1
                           ? std::numeric_limits<std::int32_t>::max()
                           : source_limit;
    VmfNotificationByteEncoding report;
    // Preflight only defines a modern boundary outside valid native storage.
    // It preserves output on insufficient spans instead of overflowing them.
    if (source[0] != '\0') {
        while (static_cast<std::int64_t>(report.consumed) < limit) {
            const auto byte =
                static_cast<unsigned char>(source[report.consumed]);
            const std::size_t width = byte < 0x80 ? 1 : 2;
            if (width > destination.size() - report.written)
                return std::unexpected(VmfNotificationTextError::output_range);
            report.written += width;
            ++report.consumed;
            if (report.consumed == source.size())
                return std::unexpected(VmfNotificationTextError::source_range);
            if (source[report.consumed] == '\0')
                break;
        }
    }
    report.terminated = limit == std::numeric_limits<std::int32_t>::max() ||
                        static_cast<std::int64_t>(report.consumed) < limit;
    if (report.terminated) {
        if (report.written == destination.size())
            return std::unexpected(VmfNotificationTextError::output_range);
        ++report.written;
    }
    if (overlap(destination.data(), report.written, source.data(),
                source.size()))
        return std::unexpected(VmfNotificationTextError::overlapping_storage);
    std::size_t cursor{};
    for (std::size_t i = 0; i < report.consumed; ++i) {
        const auto byte = static_cast<unsigned char>(source[i]);
        if (byte < 0x80)
            destination[cursor++] = source[i];
        else {
            destination[cursor++] = static_cast<char>(0xc0U | (byte >> 6));
            destination[cursor++] = static_cast<char>(0x80U | (byte & 0x3fU));
        }
    }
    if (report.terminated)
        destination[cursor] = '\0';
    return report;
}

std::expected<VmfNotificationTextBindReport, VmfNotificationTextError>
bind_vmf_notification_text(VmfNotificationTextParameters* parameters,
                           std::optional<std::string_view> name,
                           std::optional<std::string_view> value) noexcept {
    using Status = VmfNotificationTextBindStatus;
    if (!name || !value || name->empty() || (*name)[0] == '\0')
        return VmfNotificationTextBindReport{Status::ignored, std::nullopt};
    const std::array<char, 2> raw_suffix{static_cast<char>(0xa7), '\0'};
    std::array<char, 8> suffix;
    const auto encoded = encode_vmf_notification_bytes(suffix, raw_suffix, 8);
    if (!encoded)
        return std::unexpected(encoded.error());
    if (!parameters)
        return std::unexpected(VmfNotificationTextError::invalid_context);
    std::size_t index{};
    while (index < parameters->slots.size() &&
           parameters->slots[index].key[0] != '\0')
        ++index;
    if (index == parameters->slots.size())
        return VmfNotificationTextBindReport{Status::full, std::nullopt};
    const auto key_input = name->substr(0, 16);
    const auto key = c_prefix(key_input);
    if (key.size() > 15)
        return std::unexpected(VmfNotificationTextError::name_limit);
    const auto value_input = value->substr(0, 64);
    const auto text = c_prefix(value_input);
    auto& slot = parameters->slots[index];
    if (overlap(key_input.data(), key_input.size(), &slot, sizeof(slot)) ||
        overlap(value_input.data(),
                std::max<std::size_t>(value_input.size(), 1), &slot,
                sizeof(slot)))
        return std::unexpected(VmfNotificationTextError::overlapping_storage);
    std::copy(key.begin(), key.end(), slot.key.begin());
    std::copy_n(suffix.begin(), encoded->written,
                slot.key.begin() + static_cast<std::ptrdiff_t>(key.size()));
    slot.value.fill('\0');
    std::copy_n(text.begin(), std::min<std::size_t>(text.size(), 63),
                slot.value.begin());
    return VmfNotificationTextBindReport{Status::inserted, index};
}

void clear_vmf_notification_text(
    VmfNotificationTextParameters& parameters) noexcept {
    for (auto& slot : parameters.slots) {
        slot.key[0] = '\0';
        slot.value[0] = '\0';
    }
}

} // namespace tdur::ui

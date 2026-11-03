#include "tdur/ui/vmf_notification_prefix.hpp"

namespace tdur::ui {
namespace {
unsigned byte_at(std::string_view text, std::size_t cursor) noexcept {
    return cursor == text.size() ? 0 : static_cast<unsigned char>(text[cursor]);
}
} // namespace

std::expected<std::int32_t, VmfNotificationPrefixError>
compare_vmf_notification_prefix(std::optional<std::string_view> reference,
                                std::optional<std::string_view> candidate,
                                std::uint32_t count) noexcept {
    using Error = VmfNotificationPrefixErrorCode;
    if (!reference)
        return std::unexpected(
            VmfNotificationPrefixError{Error::missing_reference, 0});
    std::size_t at{};
    auto current = byte_at(*reference, at);
    while (current) {
        if (!candidate)
            return std::unexpected(
                VmfNotificationPrefixError{Error::missing_candidate, at});
        const auto other = byte_at(*candidate, at);
        if (!other || count == 0 || current != other)
            break;
        ++at;
        --count;
        // Native loads this reference byte even after consuming final count.
        current = byte_at(*reference, at);
    }
    return count == 0 ? 0 : current ? 1 : -1;
}

std::expected<std::uint32_t, VmfNotificationPrefixError>
copy_vmf_notification_prefix(std::span<char> destination,
                             std::optional<std::string_view> source,
                             std::uint32_t count) noexcept {
    using Error = VmfNotificationPrefixErrorCode;
    if (count == 0)
        return 0;
    if (!source)
        return std::unexpected(
            VmfNotificationPrefixError{Error::missing_source, 0});
    std::uint32_t copied{};
    do {
        const auto byte = byte_at(*source, copied);
        if (!byte)
            break;
        if (copied == destination.size())
            return std::unexpected(
                VmfNotificationPrefixError{Error::output_range, copied});
        destination[copied] = static_cast<char>(byte);
        ++copied;
    } while (--count);
    return copied;
}

} // namespace tdur::ui

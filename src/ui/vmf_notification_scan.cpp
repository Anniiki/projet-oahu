#include "tdur/ui/vmf_notification_scan.hpp"

#include "tdur/ui/vmf_host_variables.hpp"
#include "tdur/ui/vmf_notification_text.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace tdur::ui {
namespace {
using Error = VmfNotificationScanErrorCode;
unsigned byte_at(std::span<const char> source, std::size_t at) noexcept {
    // All cursors are proved<=size by the advancing operations below.
    return at == source.size() ? 0 : static_cast<unsigned char>(source[at]);
}
std::size_t width(unsigned byte) noexcept {
    if ((byte & 0x80U) == 0)
        return 1;
    if ((byte & 0xe0U) == 0xc0U)
        return 2;
    return (byte & 0xf0U) == 0xe0U ? 3 : 1;
}
bool advance(std::span<const char> source, std::size_t& at,
             std::size_t count) noexcept {
    if (count > source.size() - at)
        return false;
    at += count;
    return true;
}
bool space(unsigned byte) noexcept {
    return byte == 0x20U || (byte >= 9U && byte <= 13U);
}
enum class NumericKind { matched, no_match, eof, overflow };
struct Numeric {
    NumericKind kind;
    std::int32_t value{};
    std::size_t cursor{};
};
Numeric numeric_prefix(std::span<const char> source, std::size_t at) noexcept {
    while (space(byte_at(source, at)))
        ++at; // A whitespace byte is physical, so this stays<=size.
    auto byte = byte_at(source, at);
    if (!byte)
        return {NumericKind::eof, 0, at};
    std::array<char, 12> normalized{};
    std::size_t used{};
    if (byte == '-' || byte == '+') {
        if (byte == '-')
            normalized[used++] = '-';
        ++at;
    }
    bool digits{}, significant{};
    while ((byte = byte_at(source, at)) >= '0' && byte <= '9') {
        digits = true;
        if (byte != '0' || significant) {
            significant = true;
            if (used == normalized.size() - 1)
                return {NumericKind::overflow, 0, at};
            normalized[used++] = static_cast<char>(byte);
        }
        ++at;
    }
    if (!digits) {
        // MSVCR71 returns EOF for a sign followed immediately by NUL,
        // but zero assignments when a non-NUL byte follows that sign.
        return {byte_at(source, at) ? NumericKind::no_match : NumericKind::eof,
                0, at};
    }
    if (!significant)
        normalized[used++] = '0';
    // Share the existing tested int32 ASCII conversion without its31-byte
    // host-buffer limit rejecting a long run of leading zeros/whitespace.
    const auto parsed =
        vmf_host_integer_from_text(std::string_view{normalized.data(), used});
    if (!parsed)
        return {NumericKind::overflow, 0, at};
    return {NumericKind::matched, *parsed, at};
}
} // namespace

std::expected<std::int32_t, VmfNotificationScanError>
scan_vmf_notification_tokens(std::span<const char> source) noexcept {
    const std::array<char, 2> raw{static_cast<char>(0xa7), 0};
    std::array<char, 8> encoded{};
    const auto encoding = encode_vmf_notification_bytes(encoded, raw, 8, -1);
    if (!encoding)
        return std::unexpected(
            VmfNotificationScanError{Error::delimiter_encoding, 0});
    const auto marker = [&](std::size_t at, unsigned byte) {
        // Only C2-prefixed two-byte characters can match this exact marker.
        return width(byte) == 2 &&
               byte == static_cast<unsigned char>(encoded[0]) &&
               byte_at(source, at + 1) ==
                   static_cast<unsigned char>(encoded[1]);
    };
    std::size_t at{};
    std::int32_t count{}, maximum{};
    std::optional<std::int32_t> scanned_temporary;
    while (const auto byte = byte_at(source, at)) {
        const auto step = width(byte);
        if (!marker(at, byte)) {
            if (!advance(source, at, step))
                return std::unexpected(
                    VmfNotificationScanError{Error::source_range, at});
            continue;
        }
        if (!advance(source, at, step))
            return std::unexpected(
                VmfNotificationScanError{Error::source_range, at});
        const auto numeric = numeric_prefix(source, at);
        if (numeric.kind == NumericKind::overflow)
            return std::unexpected(
                VmfNotificationScanError{Error::integer_range, numeric.cursor});
        if (numeric.kind == NumericKind::matched)
            scanned_temporary = numeric.value;
        if (numeric.kind == NumericKind::eof && !scanned_temporary)
            return std::unexpected(VmfNotificationScanError{
                Error::unknown_scan_value, numeric.cursor});
        if (numeric.kind != NumericKind::no_match && *scanned_temporary <= 9) {
            if (*scanned_temporary == count + 1)
                count = *scanned_temporary;
            maximum = std::max(maximum, *scanned_temporary);
        }
        // Native716F60 does not stop this inner loop at NUL. It advances
        // past the closing marker, then the outer loop tests the next byte.
        for (;;) {
            const auto inner = byte_at(source, at);
            const bool closes = marker(at, inner);
            if (!advance(source, at, width(inner)))
                return std::unexpected(
                    VmfNotificationScanError{Error::source_range, at});
            if (closes)
                break;
        }
    }
    return maximum > count ? -1 : std::min(count, 10);
}

} // namespace tdur::ui

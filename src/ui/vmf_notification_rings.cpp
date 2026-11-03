#include "tdur/ui/vmf_notification_rings.hpp"

#include <algorithm>

namespace tdur::ui {
namespace {
bool overlap(const char* first, std::size_t size,
             const VmfNotificationRingText& output) noexcept {
    if (!size)
        return false;
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(output.data());
    return a <= b ? b - a < size : a - b < output.size();
}
std::expected<void, VmfNotificationRingError>
copy(VmfNotificationRingText& output,
     std::optional<std::string_view> input) noexcept {
    if (!input)
        return std::unexpected(VmfNotificationRingError::null_source);
    const auto window = input->substr(0, output.size());
    const auto nul = window.find('\0');
    const auto count = nul == std::string_view::npos ? window.size() : nul;
    const auto reads = count + (count < output.size() ? 1 : 0);
    if (input->data() != output.data() && overlap(input->data(), reads, output))
        return std::unexpected(VmfNotificationRingError::overlapping_storage);
    if (count && input->data() != output.data())
        std::copy_n(input->begin(), count, output.begin());
    std::fill(output.begin() + static_cast<std::ptrdiff_t>(count), output.end(),
              '\0');
    return {};
}
bool valid(std::int32_t index) noexcept { return index >= 0 && index < 8; }
} // namespace

std::expected<std::uint32_t, VmfNotificationRingError>
push_vmf_notification_text_single(
    VmfNotificationSingleRing& ring,
    std::optional<std::string_view> source) noexcept {
    if (!valid(ring.index))
        return std::unexpected(VmfNotificationRingError::invalid_index);
    const auto index = static_cast<std::size_t>(ring.index);
    const auto copied = copy(ring.slots[index], source);
    if (!copied)
        return std::unexpected(copied.error());
    ring.slots[index].back() = '\0';
    ring.index = (ring.index + 1) % 8;
    return static_cast<std::uint32_t>(index * 800);
}

std::expected<void, VmfNotificationRingError> push_vmf_notification_text_double(
    VmfNotificationDoubleRing& ring, std::optional<std::string_view> first,
    std::optional<std::string_view> second) noexcept {
    if (!valid(ring.index))
        return std::unexpected(VmfNotificationRingError::invalid_index);
    auto& destination = ring.slots[static_cast<std::size_t>(ring.index)];
    const auto a = copy(destination.first, first);
    if (!a)
        return a;
    const auto b = copy(destination.second, second);
    if (!b)
        return b;
    destination.first.back() = '\0';
    destination.second.back() = '\0';
    ring.index = (ring.index + 1) % 8;
    return {};
}

} // namespace tdur::ui

#pragma once

#include "tdur/ui/vmf_notification_named.hpp"
#include "tdur/ui/vmf_notification_rings.hpp"

namespace tdur::ui {

// Logical SYSTEMTIME words, not a packed compiler-dependent Windows layout.
struct VmfNotificationTime {
    std::uint16_t year{}, month{}, day_of_week{}, day{};
    std::uint16_t hour{}, minute{}, second{}, milliseconds{};
    // 89DB30 reads DWORDs at offsets2/6, not month/day words. Preserve the
    // observed unsigned wrap, even for non-calendar synthetic input words.
    constexpr std::uint32_t datepack() const noexcept {
        const auto dword2 =
            std::uint32_t{month} | (std::uint32_t{day_of_week} << 16);
        const auto dword6 = std::uint32_t{day} | (std::uint32_t{hour} << 16);
        return ((std::uint32_t{year} % 100U) * 12U + dword2) * 31U + dword6;
    }
};
class VmfNotificationClock {
  public:
    virtual ~VmfNotificationClock() = default;
    virtual std::expected<VmfNotificationTime, VmfNotificationStateError>
    get_local_time() noexcept = 0;
};
// Actual host OS clock; never invokes the original game or its import table.
class VmfNotificationWindowsClock final : public VmfNotificationClock {
  public:
    std::expected<VmfNotificationTime, VmfNotificationStateError>
    get_local_time() noexcept override;
};

// Explicit projections of the SAME child. Unknown fields are not zero IDs.
// Stable metadata/backing outlive every effect frame; the loader owns them.
struct VmfNotificationChildEffectData {
    explicit VmfNotificationChildEffectData(
        VmfNotificationChildState& value) noexcept
        : child(value) {}
    VmfNotificationChildState& child;
    std::optional<std::uint32_t> title_low_20, title_high_24;
    std::optional<std::uint32_t> body_low_28, body_high_2c, key_38;
};
// Logical facets of the one global10E7528 owner, not its x86 object layout.
struct VmfNotificationTextOwner {
    VmfNotificationSingleRing single;
    VmfNotificationDoubleRing pair;
};

// One +30 object exposes the actual queue and the same64-record named view.
// Domain/hooks are supplied explicitly, not inferred from uninitialized BSS.
class VmfNotificationNewsOwner {
  public:
    VmfNotificationNewsOwner(
        VmfNotificationQueueDomain& shared_domain,
        std::array<VmfNotificationQueueHook*, 5> established_hooks) noexcept
        : queue(established_hooks), domain(shared_domain),
          view_{std::span<const VmfNotificationRecord, 64>{queue.records}} {}
    VmfNotificationNewsOwner(const VmfNotificationNewsOwner&) = delete;
    VmfNotificationNewsOwner&
    operator=(const VmfNotificationNewsOwner&) = delete;
    VmfNotificationNewsOwner(VmfNotificationNewsOwner&&) = delete;
    VmfNotificationNewsOwner& operator=(VmfNotificationNewsOwner&&) = delete;
    VmfNotificationNews* named_view() noexcept { return &view_; }
    VmfNotificationQueue queue;
    VmfNotificationQueueDomain& domain;

  private:
    VmfNotificationNews view_;
};
class VmfNotificationNewsHost : public VmfNotificationNamedHost {
  public:
    virtual std::expected<VmfNotificationNewsOwner*, VmfNotificationStateError>
    notification_news_owner() noexcept = 0;
    std::expected<VmfNotificationNews*, VmfNotificationStateError>
    news() noexcept final {
        auto selected = notification_news_owner();
        if (!selected)
            return std::unexpected(selected.error());
        return *selected ? (*selected)->named_view() : nullptr;
    }
};

} // namespace tdur::ui

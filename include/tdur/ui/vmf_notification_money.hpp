#pragma once

#include "tdur/ui/vmf_menu_finalizers.hpp"
#include "tdur/ui/vmf_notification_replace.hpp"

namespace tdur::ui {

struct VmfNotificationCurrencyRequest {
    std::span<char, 32> destination;
    std::uint64_t format_id;
    // The actual numeric argument's live backing, including its NUL/tail.
    std::span<const char> number;
};
struct VmfNotificationCurrencyReport {
    VmfNotificationNamedStatus status;
    std::size_t steps{};
};
class VmfNotificationCurrencyBackend {
  public:
    virtual ~VmfNotificationCurrencyBackend() = default;
    // Required real717280 owner/formatter, not an availability callback.
    // Separate frame per recursive invocation. Missing format leaves the
    // destination's initial NUL intact. No native DWORD varargs in x64.
    virtual std::expected<void, VmfNotificationReplaceError>
    begin(const VmfNotificationCurrencyRequest& request) noexcept = 0;
    virtual std::expected<VmfNotificationCurrencyReport,
                          VmfNotificationReplaceError>
    run(std::size_t step_budget) noexcept = 0;
};
struct VmfNotificationMoneyInputs {
    // Unknown differs from confirmed NULL. Locale is only needed for groups.
    std::optional<VmfMenuLocaleState*> locale;
    VmfMenuFinalizerInputs units;
    std::optional<VmfNotificationCurrencyBackend*> currency;
};
enum class VmfNotificationMoneyPhase {
    initialize,
    iteration,
    locale,
    units,
    group,
    leading,
    currency,
    currency_begin,
    currency_run,
    finished,
};
enum class VmfNotificationMoneyWait {
    none,
    locale,
    units,
    currency,
};
struct VmfNotificationMoneySnapshot {
    VmfNotificationMoneyPhase phase{};
    VmfNotificationMoneyWait wait{};
    std::size_t steps{}, stores{}, groups{}, unit_runs{}, currency_begins{},
        currency_runs{};
    std::int32_t remaining{};
    bool faulted{};
};
struct VmfNotificationMoneyReport {
    VmfNotificationNamedStatus status;
    VmfNotificationMoneySnapshot state;
    std::size_t steps{};
};

// 719AD0: signed DWORD, positive groups only, full cache/898890 effects,
// three-byte us comparison, then optional real717280 currency wrapper.
// Source locale has fixed identity after capture; bindings remain live at
// their native read phases. Unknown dependencies suspend without replay.
// Native unbounded copies are guarded; errors retain previous stores and
// fault this invocation. No allocations here; dependencies may allocate.
// Keep destination/inputs/bound services alive, stable and serialized.
class VmfNotificationMoneyContext {
  public:
    VmfNotificationMoneyContext(std::span<char> destination,
                                std::int32_t amount) noexcept;
    VmfNotificationMoneyContext(const VmfNotificationMoneyContext&) = delete;
    VmfNotificationMoneyContext&
    operator=(const VmfNotificationMoneyContext&) = delete;
    VmfNotificationMoneyContext(VmfNotificationMoneyContext&&) = delete;
    VmfNotificationMoneyContext&
    operator=(VmfNotificationMoneyContext&&) = delete;
    VmfNotificationMoneySnapshot snapshot() const noexcept;
    std::expected<VmfNotificationMoneyReport, VmfNotificationReplaceError>
    run(const VmfNotificationMoneyInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    std::expected<void, VmfNotificationReplaceError>
    copy(std::span<const char> source) noexcept;
    std::expected<void, VmfNotificationReplaceError>
    prepend(std::string_view prefix) noexcept;
    std::span<char> destination_;
    VmfNotificationMoneySnapshot state_;
    VmfMenuLocaleState* locale_{};
    std::optional<VmfMenuFinalizerContext> units_;
    VmfNotificationCurrencyBackend* currency_{};
    std::array<char, 32> currency_text_{};
    bool running_{};
};

} // namespace tdur::ui

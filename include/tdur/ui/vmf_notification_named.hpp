#pragma once

#include "tdur/ui/vmf_catalog.hpp"
#include "tdur/ui/vmf_notification_lookup.hpp"
#include "tdur/ui/vmf_notification_money.hpp"
#include "tdur/ui/vmf_notification_queue.hpp"
#include "tdur/ui/vmf_notification_state.hpp"

namespace tdur::ui {

// Logical owned fields, not the original profile/news ABI. Missing fields
// remain unknown; an engaged empty backing represents a known empty string.
struct VmfNotificationProfile {
    std::optional<VmfNotificationFormatSource> name_58, club_8c;
};
struct VmfNotificationNews {
    std::optional<std::span<const VmfNotificationRecord, 64>> records;
};
class VmfNotificationNamedHost : public VmfNotificationStateHost {
  public:
    // Actual owned counterparts of virtual+1C/+30; inherited+4C remains the
    // real notification-state query. Implementations may mutate globals.
    virtual std::expected<VmfNotificationProfile*, VmfNotificationStateError>
    profile() noexcept = 0;
    virtual std::expected<VmfNotificationNews*, VmfNotificationStateError>
    news() noexcept = 0;
};
struct VmfNotificationNamedInputs {
    // Live global114C15C: read again at EACH native virtual-call site.
    // nullopt is unknown, engaged nullptr an invalid native dereference.
    std::optional<VmfNotificationNamedHost*> host;
    std::optional<VmfNotificationLanguageSource*> language;
    std::optional<std::span<const VmfCatalogBinding>> catalogs;
    std::uint32_t catalog_record_budget = 100'000;
    std::optional<std::uint32_t> driving_mode, reward;
    std::optional<std::uint16_t> x87_control_word;
    // Fixed call argument, not a global or an inferred empty table.
    std::optional<const VmfNotificationTextParameters*> parameters;
    VmfNotificationMoneyInputs money;
};
enum class VmfNotificationNamedToken {
    player_name,
    news,
    achievement_points,
    level,
    completion,
    driving_mode,
    race_luxury,
    club,
    reward,
    parameter,
};
enum class VmfNotificationNamedPhase {
    idle,
    dispatch,
    profile_query,
    profile_text,
    news_query,
    news_count,
    state_first,
    state_query,
    state_run,
    completion,
    driving_mode,
    catalog,
    reward,
    money,
    parameter,
    finished,
};
enum class VmfNotificationNamedWait {
    none,
    host,
    profile_text,
    news_records,
    language,
    state,
    control_word,
    driving_mode,
    catalogs,
    reward,
    money,
    parameters,
};
struct VmfNotificationNamedSnapshot {
    VmfNotificationNamedToken token = VmfNotificationNamedToken::parameter;
    VmfNotificationNamedPhase phase{};
    VmfNotificationNamedWait wait{};
    std::size_t steps{}, profile_calls{}, news_calls{}, state_calls{},
        state_runs{}, catalog_calls{};
    std::optional<std::uint32_t> state_result;
    bool faulted{};
};

// Concrete7178C0 over the existing real C++ queries/calculators/catalogs and
// 719AD0. Nine case-sensitive prefix branches plus716C40 default. begin copies
// the full body/suffix with strncpy512 padding and force-NUL511. The captured
// key survives waits; returned profile/news/state owners are captured once.
// COMPL_PCT's first+4C result is ignored, including NULL, then the live global
// is read again. All real host implementations and currency717280 are still
// required: unknown is a wait, never successful placeholder data.
// Keep inputs/backings/services stable and serialized for the backend lifetime.
// Separate backend/frame for recursive formatting. No allocation here except
// through dependencies. Errors preserve effects and fault the invocation.
class VmfNotificationNamedResolver final : public VmfNotificationNamedBackend {
  public:
    explicit VmfNotificationNamedResolver(
        const VmfNotificationNamedInputs& inputs) noexcept;
    VmfNotificationNamedResolver(const VmfNotificationNamedResolver&) = delete;
    VmfNotificationNamedResolver&
    operator=(const VmfNotificationNamedResolver&) = delete;
    VmfNotificationNamedResolver(VmfNotificationNamedResolver&&) = delete;
    VmfNotificationNamedResolver&
    operator=(VmfNotificationNamedResolver&&) = delete;
    VmfNotificationNamedSnapshot snapshot() const noexcept;
    std::expected<void, VmfNotificationReplaceError>
    begin(const VmfNotificationNamedRequest& request) noexcept override;
    std::expected<VmfNotificationNamedReport, VmfNotificationReplaceError>
    run(std::size_t step_budget) noexcept override;

  private:
    std::expected<void, VmfNotificationReplaceError>
    text(std::span<const char> source, bool padded = false,
         bool implied_nul = true) noexcept;
    std::expected<void, VmfNotificationReplaceError>
    literal(std::string_view value) noexcept;
    std::expected<void, VmfNotificationReplaceError>
    integer(std::int32_t value) noexcept;
    const VmfNotificationNamedInputs& inputs_;
    VmfNotificationNamedSnapshot state_;
    std::array<char, 512> key_{}, output_{};
    std::size_t initialized_{};
    VmfNotificationProfile* profile_{};
    VmfNotificationNews* news_{};
    std::optional<VmfNotificationStateContext> computation_;
    std::optional<VmfNotificationMoneyContext> money_;
    std::uint64_t catalog_id_{};
    std::uint32_t catalog_selector_{};
    std::string_view catalog_fallback_;
    bool running_{};
};

} // namespace tdur::ui

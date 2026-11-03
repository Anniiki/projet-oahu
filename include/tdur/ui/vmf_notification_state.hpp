#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

// Logical owned fields, not the native ABI or a claim about their units.
struct VmfNotificationChildState {
    std::optional<std::uint8_t> active_08;
    std::optional<std::uint32_t> weight_14;
    std::optional<std::uint32_t> mask_0c;
    std::optional<std::uint32_t> limit_18;
    std::optional<std::uint32_t> id_low_30;
    std::optional<std::uint32_t> id_high_34;
};
using VmfNotificationChildSlot = std::optional<VmfNotificationChildState*>;
struct VmfNotificationState {
    std::optional<std::int32_t> count_0c;
    std::optional<std::span<VmfNotificationChildSlot>> children_08;
    // Native353C,3540,3544,3548,354C. Read from index4 down to0; NOT sorted.
    std::array<std::optional<std::uint32_t>, 5> thresholds;
    // Native 3550 and 3551 flags stored by 89E330 for selectors 5912A0 and
    // 5A9940.
    std::optional<std::uint8_t> flag_3550;
    std::optional<std::uint8_t> flag_3551;
};
enum class VmfNotificationStateRoutine { sum_89bd90, level_89c210 };
enum class VmfNotificationStatePhase {
    count,
    children,
    child,
    active,
    weight,
    next,
    threshold,
    finished,
};
enum class VmfNotificationStateStatus {
    finished,
    step_limit,
    needs_count,
    needs_children,
    needs_child,
    needs_active_flag,
    needs_weight,
    needs_threshold,
};
enum class VmfNotificationStateErrorCode {
    invalid_context,
    row_out_of_range,
    invalid_native_binding,
    host_failure,
};
struct VmfNotificationStateError {
    VmfNotificationStateErrorCode code;
    std::string_view detail;
};
struct VmfNotificationStateSnapshot {
    VmfNotificationStateRoutine routine;
    VmfNotificationStatePhase phase;
    std::optional<std::int32_t> captured_count;
    std::size_t steps{}, row{}, slots_read{}, weights_read{}, thresholds_read{};
    std::size_t threshold_index = 4;
    std::uint32_t sum{};
    std::optional<std::uint32_t> result;
    bool faulted{};
};
struct VmfNotificationStateReport {
    VmfNotificationStateStatus status;
    VmfNotificationStateSnapshot state;
    std::size_t steps;
};

// 89BD90 captures signed count AND array base ONCE. Slots/child fields are
// read sequentially, ignoring NULL slots and nonzero active bytes. Addition
// wraps uint32; the level path then performs unsigned descending-index tests.
// Unknown fields are modern binding waits, never zero-filled observations.
// All owned storage must live without relocation; accesses are serialized.
// Do not move during run. Budgets/retries preserve values already consumed.
class VmfNotificationStateContext {
  public:
    [[nodiscard]] static VmfNotificationStateContext
    sum(VmfNotificationState& owner) noexcept;
    [[nodiscard]] static VmfNotificationStateContext
    level(VmfNotificationState& owner) noexcept;
    VmfNotificationStateContext(const VmfNotificationStateContext&) = delete;
    VmfNotificationStateContext&
    operator=(const VmfNotificationStateContext&) = delete;
    VmfNotificationStateContext(VmfNotificationStateContext&& other) noexcept;
    VmfNotificationStateContext&
    operator=(VmfNotificationStateContext&& other) noexcept;
    [[nodiscard]] VmfNotificationStateSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfNotificationStateReport,
                                VmfNotificationStateError>
    run(std::size_t step_budget = 4096) noexcept;

  private:
    VmfNotificationStateContext(VmfNotificationState& owner,
                                VmfNotificationStateRoutine routine) noexcept;
    void finish_sum() noexcept;
    VmfNotificationState* owner_{};
    VmfNotificationStateSnapshot state_;
    std::span<VmfNotificationChildSlot> children_;
    VmfNotificationChildState* child_{};
    bool running_{};
};

struct VmfNotificationLevelDestination {
    std::optional<std::uint32_t> level_f028;
};
class VmfNotificationStateHost {
  public:
    virtual ~VmfNotificationStateHost() = default;
    // Real owned C++ implementation of global114C15C's virtual+4C. May mutate
    // state. nullptr is an invalid native dereference, not "unknown service".
    [[nodiscard]] virtual std::expected<VmfNotificationState*,
                                        VmfNotificationStateError>
    notification_state() noexcept = 0;
};
enum class VmfNotificationStorePhase { host, query, level, store, finished };
enum class VmfNotificationStoreStatus {
    finished,
    step_limit,
    needs_host,
    state_pending,
};
struct VmfNotificationStoreSnapshot {
    VmfNotificationStorePhase phase{};
    std::size_t steps{}, completed_host_calls{}, stores{};
    bool faulted{};
    std::optional<VmfNotificationStateSnapshot> computation;
};
struct VmfNotificationStoreReport {
    VmfNotificationStoreStatus status;
    VmfNotificationStoreSnapshot state;
    std::size_t steps;
    std::optional<VmfNotificationStateStatus> computation_status;
};

// 889230: bind/capture host, actually invoke+4C, compute the returned owner's
// level with89C210/89BD90, then store at the fixed valid argument target+F028.
// The host and returned state remain selected across waits; no replay of the
// virtual call. A fault preserves its prior effects and forbids another run.
// Lifetime/serialization/no-move-during-run contract is the same as above.
class VmfNotificationStoreContext {
  public:
    [[nodiscard]] static VmfNotificationStoreContext
    create(VmfNotificationLevelDestination& target) noexcept;
    VmfNotificationStoreContext(const VmfNotificationStoreContext&) = delete;
    VmfNotificationStoreContext&
    operator=(const VmfNotificationStoreContext&) = delete;
    VmfNotificationStoreContext(VmfNotificationStoreContext&& other) noexcept;
    VmfNotificationStoreContext&
    operator=(VmfNotificationStoreContext&& other) noexcept;
    [[nodiscard]] VmfNotificationStoreSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfNotificationStoreReport,
                                VmfNotificationStateError>
    run(std::optional<VmfNotificationStateHost*> host,
        std::size_t step_budget = 4096) noexcept;

  private:
    explicit VmfNotificationStoreContext(
        VmfNotificationLevelDestination& target) noexcept;
    VmfNotificationLevelDestination* target_{};
    VmfNotificationStateHost* host_{};
    VmfNotificationStoreSnapshot state_;
    std::optional<VmfNotificationStateContext> computation_;
    bool running_{};
};

} // namespace tdur::ui

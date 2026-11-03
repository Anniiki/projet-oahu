#pragma once

#include "tdur/ui/vmf_notification_state.hpp"

namespace tdur::ui {

class VmfNotificationChildEvaluator {
  public:
    virtual ~VmfNotificationChildEvaluator() = default;
    // Child virtual+4. 89DD60 passes a NULL output pointer. Other callers may
    // pass a valid output location. This is a real C++ service, not an x86
    // address or a promise of availability. May mutate the child's fields.
    [[nodiscard]] virtual std::expected<std::uint32_t,
                                        VmfNotificationStateError>
    evaluate(VmfNotificationChildState& child,
             std::uint32_t* output_argument) noexcept = 0;
};

enum class VmfNotificationChildEffect {
    call_89e330,
    call_89bf80,
    call_89db30,
};

enum class VmfNotificationEffectStatus {
    finished,
    pending,
};

// Modern continuation adapter, not a claim of native asynchronous execution.
// Borrowed from a callee pool, leased exclusively by one child context.
// Frame/module/backings stay alive and nonrelocated until release completes.
// run() advances by up to step_budget steps without blocking. A pending poll
// yields to the caller even if it makes no internal progress.
// release() returns the frame to its owner without game effects or rollback;
// it is called exactly once on finish/fault/abandonment. It may recycle self.
// Serialize access; this protocol does not lock or allocate the callee pool.
class VmfNotificationChildEffectFrame {
  public:
    virtual ~VmfNotificationChildEffectFrame() = default;
    [[nodiscard]] virtual std::expected<VmfNotificationEffectStatus,
                                        VmfNotificationStateError>
    run(std::size_t step_budget) noexcept = 0;
    virtual void release() noexcept = 0;
};

class VmfNotificationChildEffects {
  public:
    virtual ~VmfNotificationChildEffects() = default;
    // Concrete owned implementations of the named native callees. Their
    // return registers are ignored by 89DD60. They must execute actual effects,
    // not acknowledge a request with a dummy success. The module is retained
    // for this call frame, like fixed callee implementations in one image.
    [[nodiscard]] virtual std::expected<void, VmfNotificationStateError>
    invoke(VmfNotificationChildEffect effect,
           VmfNotificationChildState& child) noexcept;

    // Asynchronous effect continuation entry.
    // Default calls actual invoke() once, returning nullptr on success.
    // nullptr means the effect is FINISHED, never an unknown binding.
    // An asynchronous implementation borrows a stable frame from its pool.
    [[nodiscard]] virtual std::expected<VmfNotificationChildEffectFrame*,
                                        VmfNotificationStateError>
    begin_effect(VmfNotificationChildEffect effect,
                 VmfNotificationChildState& child) noexcept;
};

struct VmfNotificationChildInputs {
    std::optional<VmfNotificationChildEvaluator*> evaluator;
    std::optional<std::uint8_t> flag_16a2d2f;
    std::optional<VmfNotificationChildEffects*> effects;
};

enum class VmfNotificationChildPhase {
    active,
    mask,
    evaluator_binding,
    evaluate,
    limit,
    write_active,
    global_flag,
    id_low,
    id_high,
    effects_binding,
    effect,
    final_active,
    finished,
};

enum class VmfNotificationChildStatus {
    finished,
    step_limit,
    needs_active_flag,
    needs_mask,
    needs_evaluator,
    needs_limit,
    needs_global_flag,
    needs_id_low,
    needs_id_high,
    needs_effects,
    effect_pending,
};

struct VmfNotificationChildSnapshot {
    VmfNotificationChildPhase phase{};
    std::size_t steps{}, completed_evaluations{}, completed_effects{};
    std::size_t active_stores{};
    std::optional<VmfNotificationChildEffect> pending_effect;
    std::optional<std::uint8_t> result;
    bool faulted{};
};

struct VmfNotificationChildReport {
    VmfNotificationChildStatus status;
    VmfNotificationChildSnapshot state;
    std::size_t steps;
};

// 89DD60: byte guard, full DWORD mask inclusion, actual virtual evaluation,
// unsigned comparison, active store BEFORE dispatch/global reads, sentinel
// routing and conditional final active reread. Incoming mode/byte are fixed
// call arguments. Evaluator, result, comparison, IDs and each selected effect
// survive modern waits/moves. A failed service preserves the prefix and makes
// the frame terminal. Missing services are requests, never successful no-ops.
// Asynchronous effects yield effect_pending without replaying begin/invoke.
// A move constructor attempted during run creates an invalid destination;
// assignment involving a running source/target leaves both unchanged.
// Destruction/assignment abandons a pending lease without undoing its effects.
// No ordinary allocation in this context; modules/frame methods may allocate.
// All storage/module implementations must live without relocation, with
// serialized accesses. Do not move or reenter the same frame during run.
// A recursive native call requires its own independent continuation frame.
class VmfNotificationChildContext {
  public:
    [[nodiscard]] static VmfNotificationChildContext
    create(VmfNotificationChildState& child, std::uint32_t mode,
           std::uint8_t incoming_flag) noexcept;
    ~VmfNotificationChildContext() noexcept;
    VmfNotificationChildContext(const VmfNotificationChildContext&) = delete;
    VmfNotificationChildContext&
    operator=(const VmfNotificationChildContext&) = delete;
    VmfNotificationChildContext(VmfNotificationChildContext&& other) noexcept;
    VmfNotificationChildContext&
    operator=(VmfNotificationChildContext&& other) noexcept;
    [[nodiscard]] VmfNotificationChildSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfNotificationChildReport,
                                VmfNotificationStateError>
    run(const VmfNotificationChildInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    VmfNotificationChildContext(VmfNotificationChildState& child,
                                std::uint32_t mode,
                                std::uint8_t incoming_flag) noexcept;
    void finish(std::uint8_t result) noexcept;
    void select_effect(VmfNotificationChildEffect effect) noexcept;
    void release_active_frame() noexcept;

    VmfNotificationChildState* child_{};
    VmfNotificationChildEvaluator* evaluator_{};
    VmfNotificationChildEffects* effects_{};
    VmfNotificationChildEffectFrame* active_frame_{};
    VmfNotificationChildSnapshot state_;
    std::uint32_t mode_{}, evaluation_{}, id_low_{};
    std::uint8_t incoming_flag_{}, computed_active_{};
    bool running_{};
};

} // namespace tdur::ui

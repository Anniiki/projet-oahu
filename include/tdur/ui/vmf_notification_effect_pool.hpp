#pragma once

#include "tdur/ui/vmf_notification_child.hpp"
#include "tdur/ui/vmf_notification_dispatch.hpp"
#include "tdur/ui/vmf_notification_effect_bf80.hpp"
#include "tdur/ui/vmf_notification_effect_db30.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace tdur::ui {

// Configuration ports for a single preallocated effect slot.
// Slots retain the input OBJECTS, not the configuration span. The caller
// may supply distinct formatters for overlapping invocations; this class
// does not manufacture or infer their independence.
struct VmfNotificationEffectsSlotInputs {
    const VmfNotificationDispatchInputs* dispatch{};
    const VmfNotificationBF80Inputs* bf80{};
    const VmfNotificationDB30Inputs* db30{};
};

// Authoritative mapping between a child state and its effect metadata.
struct VmfNotificationEffectsBinding {
    VmfNotificationChildState* child{};
    VmfNotificationChildEffectData* metadata{};
};

// Concrete pool routing child continuation effects (89E330, 89BF80, 89DB30)
// across preconstructed slots with dedicated, independent input ports.
// Non-copyable, non-movable. This pool allocates only at construction;
// delegated services retain their own allocation contracts. Registry entries
// are copied, but child/metadata/inputs/owners/services/backings are borrowed,
// stable and nonrelocated. Keep them alive through pool/lease destruction.
// Serialize all access. Do not destroy the pool during run or a callback.
// Preserve selected callees' thread affinity, including retained parent locks.
// A finished/faulted lease stays reserved until release. Discard the returned
// pointer after release; it is not a generation-checked handle for later use.
// Normal release/cancellation preserves the completed game-effect prefix.
class VmfNotificationEffectsPool final : public VmfNotificationChildEffects {
  public:
    using SlotInputs = VmfNotificationEffectsSlotInputs;
    using Binding = VmfNotificationEffectsBinding;

    explicit VmfNotificationEffectsPool(std::span<const SlotInputs> slots,
                                        std::span<const Binding> bindings = {});
    ~VmfNotificationEffectsPool() override;

    VmfNotificationEffectsPool(const VmfNotificationEffectsPool&) = delete;
    VmfNotificationEffectsPool&
    operator=(const VmfNotificationEffectsPool&) = delete;
    VmfNotificationEffectsPool(VmfNotificationEffectsPool&&) = delete;
    VmfNotificationEffectsPool&
    operator=(VmfNotificationEffectsPool&&) = delete;

    [[nodiscard]] std::expected<void, VmfNotificationStateError>
    invoke(VmfNotificationChildEffect effect,
           VmfNotificationChildState& child) noexcept override;

    [[nodiscard]] std::expected<VmfNotificationChildEffectFrame*,
                                VmfNotificationStateError>
    begin_effect(VmfNotificationChildEffect effect,
                 VmfNotificationChildState& child) noexcept override;

    [[nodiscard]] std::size_t active_leases() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

  private:
    class Slot;
    std::vector<Binding> bindings_;
    std::vector<std::unique_ptr<Slot>> slots_;
    bool closing_{};
};

} // namespace tdur::ui

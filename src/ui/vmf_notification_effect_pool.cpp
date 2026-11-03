#include "tdur/ui/vmf_notification_effect_pool.hpp"

#include <stdexcept>
#include <utility>

namespace tdur::ui {

class VmfNotificationEffectsPool::Slot {
  public:
    explicit Slot(const SlotInputs& inputs)
        : inputs_(inputs), dispatch_frame_(*inputs.dispatch),
          bf80_frame_(*inputs.bf80), db30_frame_(*inputs.db30),
          adapter_(*this) {}

    ~Slot() { release_adapter(); }

    [[nodiscard]] bool is_busy() const noexcept {
        return leased_ || running_ || releasing_;
    }

    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin(VmfNotificationChildEffect effect, VmfNotificationChildState& child,
          VmfNotificationChildEffectData* metadata) noexcept {
        if (is_busy()) {
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::invalid_context,
                "Slot is already leased or active"});
        }

        switch (effect) {
        case VmfNotificationChildEffect::call_89e330: {
            auto res = dispatch_frame_.begin(child);
            if (!res) {
                return std::unexpected(res.error());
            }
            if (!*res) {
                dispatch_frame_.release();
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_context,
                    "Concrete dispatch frame returned NULL"});
            }
            active_frame_ = *res;
            leased_ = true;
            return &adapter_;
        }
        case VmfNotificationChildEffect::call_89bf80: {
            if (!metadata) {
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_native_binding,
                    "BF80 requires metadata binding"});
            }
            auto res = bf80_frame_.begin(*metadata);
            if (!res) {
                return std::unexpected(res.error());
            }
            active_frame_ = &bf80_frame_;
            leased_ = true;
            return &adapter_;
        }
        case VmfNotificationChildEffect::call_89db30: {
            if (!metadata) {
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_native_binding,
                    "DB30 requires metadata binding"});
            }
            auto res = db30_frame_.begin(child, *metadata);
            if (!res) {
                return std::unexpected(res.error());
            }
            if (!*res) {
                db30_frame_.release();
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_context,
                    "Concrete DB30 frame returned NULL"});
            }
            active_frame_ = *res;
            leased_ = true;
            return &adapter_;
        }
        default:
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::invalid_native_binding,
                "Invalid child effect enum"});
        }
    }

    std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
    run_adapter(std::size_t step_budget) noexcept {
        if (running_ || releasing_ || !leased_ || !active_frame_) {
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::invalid_context,
                "Slot is already running, releasing or not leased"});
        }
        running_ = true;
        struct RunGuard {
            bool& flag;
            ~RunGuard() { flag = false; }
        } guard{running_};

        return active_frame_->run(step_budget);
    }

    void release_adapter() noexcept {
        if (running_ || releasing_ || !leased_ || !active_frame_) {
            return;
        }
        releasing_ = true;
        struct ReleaseGuard {
            Slot& slot;
            ~ReleaseGuard() {
                slot.releasing_ = false;
                slot.leased_ = false;
                slot.active_frame_ = nullptr;
            }
        } guard{*this};

        active_frame_->release();
    }

  private:
    class Adapter final : public VmfNotificationChildEffectFrame {
      public:
        explicit Adapter(Slot& slot) noexcept : slot_(slot) {}
        ~Adapter() override = default;

        std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
        run(std::size_t step_budget) noexcept override {
            return slot_.run_adapter(step_budget);
        }

        void release() noexcept override { slot_.release_adapter(); }

      private:
        Slot& slot_;
    };

    SlotInputs inputs_;
    VmfNotificationDispatchFrame dispatch_frame_;
    VmfNotificationEffectBF80Frame bf80_frame_;
    VmfNotificationDB30Frame db30_frame_;
    Adapter adapter_;
    VmfNotificationChildEffectFrame* active_frame_{nullptr};
    bool leased_{false};
    bool running_{false};
    bool releasing_{false};
};

VmfNotificationEffectsPool::VmfNotificationEffectsPool(
    std::span<const SlotInputs> slots, std::span<const Binding> bindings) {
    if (slots.size() < 1 || slots.size() > 32) {
        throw std::invalid_argument(
            "Effects pool capacity must be in range [1, 32]");
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].dispatch || !slots[i].bf80 || !slots[i].db30) {
            throw std::invalid_argument(
                "SlotInputs dispatch, bf80 and db30 pointers cannot be null");
        }
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        auto* c = bindings[i].child;
        auto* m = bindings[i].metadata;
        if (!c || !m) {
            throw std::invalid_argument(
                "Binding child or metadata cannot be null");
        }
        if (&m->child != c) {
            throw std::invalid_argument(
                "Binding metadata child identity mismatch");
        }
        for (std::size_t j = i + 1; j < bindings.size(); ++j) {
            if (bindings[j].child == c) {
                throw std::invalid_argument(
                    "Duplicate child in bindings registry");
            }
        }
    }

    bindings_.assign(bindings.begin(), bindings.end());
    slots_.reserve(slots.size());
    for (const auto& s : slots) {
        slots_.push_back(std::make_unique<Slot>(s));
    }
}

VmfNotificationEffectsPool::~VmfNotificationEffectsPool() {
    // Close before releasing while every slot is still alive. A cancellation
    // cannot admit a new borrower into storage that is being destroyed.
    closing_ = true;
    for (auto& slot : slots_)
        slot->release_adapter();
}

std::expected<void, VmfNotificationStateError>
VmfNotificationEffectsPool::invoke(
    VmfNotificationChildEffect /*effect*/,
    VmfNotificationChildState& /*child*/) noexcept {
    return std::unexpected(VmfNotificationStateError{
        VmfNotificationStateErrorCode::invalid_context,
        "Synchronous invoke is not supported by VmfNotificationEffectsPool; "
        "use begin_effect"});
}

std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationEffectsPool::begin_effect(
    VmfNotificationChildEffect effect,
    VmfNotificationChildState& child) noexcept {
    if (closing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Child effects pool is closing"});
    if (effect != VmfNotificationChildEffect::call_89e330 &&
        effect != VmfNotificationChildEffect::call_89bf80 &&
        effect != VmfNotificationChildEffect::call_89db30) {
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_native_binding,
            "Invalid child effect enum"});
    }

    VmfNotificationChildEffectData* metadata = nullptr;
    if (effect == VmfNotificationChildEffect::call_89bf80 ||
        effect == VmfNotificationChildEffect::call_89db30) {
        for (const auto& b : bindings_) {
            if (b.child == &child) {
                metadata = b.metadata;
                break;
            }
        }
        if (!metadata || &metadata->child != &child) {
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::invalid_native_binding,
                "Child state is not registered in effect pool bindings"});
        }
    }

    for (auto& slot : slots_) {
        if (!slot->is_busy()) {
            return slot->begin(effect, child, metadata);
        }
    }

    return std::unexpected(
        VmfNotificationStateError{VmfNotificationStateErrorCode::host_failure,
                                  "Child effects pool exhausted"});
}

std::size_t VmfNotificationEffectsPool::active_leases() const noexcept {
    std::size_t active = 0;
    for (const auto& slot : slots_) {
        if (slot && slot->is_busy()) {
            ++active;
        }
    }
    return active;
}

std::size_t VmfNotificationEffectsPool::capacity() const noexcept {
    return slots_.size();
}

} // namespace tdur::ui

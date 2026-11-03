#include "tdur/ui/vmf_notification_child.hpp"

#include <utility>

namespace tdur::ui {

std::expected<void, VmfNotificationStateError>
VmfNotificationChildEffects::invoke(
    VmfNotificationChildEffect /*effect*/,
    VmfNotificationChildState& /*child*/) noexcept {
    return std::unexpected(VmfNotificationStateError{
        VmfNotificationStateErrorCode::invalid_context,
        "Asynchronous child effect module requires begin_effect"});
}

std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationChildEffects::begin_effect(
    VmfNotificationChildEffect effect,
    VmfNotificationChildState& child) noexcept {
    const auto sync = invoke(effect, child);
    if (!sync)
        return std::unexpected(sync.error());
    return nullptr;
}

VmfNotificationChildContext::VmfNotificationChildContext(
    VmfNotificationChildState& child, std::uint32_t mode,
    std::uint8_t incoming_flag) noexcept
    : child_(&child), mode_(mode), incoming_flag_(incoming_flag) {}

VmfNotificationChildContext::~VmfNotificationChildContext() noexcept {
    running_ = true;
    child_ = nullptr;
    release_active_frame();
}

VmfNotificationChildContext
VmfNotificationChildContext::create(VmfNotificationChildState& child,
                                    std::uint32_t mode,
                                    std::uint8_t incoming_flag) noexcept {
    return VmfNotificationChildContext{child, mode, incoming_flag};
}

VmfNotificationChildContext::VmfNotificationChildContext(
    VmfNotificationChildContext&& other) noexcept {
    if (other.running_)
        return;
    child_ = std::exchange(other.child_, nullptr);
    evaluator_ = other.evaluator_;
    effects_ = other.effects_;
    active_frame_ = std::exchange(other.active_frame_, nullptr);
    state_ = other.state_;
    mode_ = other.mode_;
    evaluation_ = other.evaluation_;
    id_low_ = other.id_low_;
    incoming_flag_ = other.incoming_flag_;
    computed_active_ = other.computed_active_;
}

VmfNotificationChildContext& VmfNotificationChildContext::operator=(
    VmfNotificationChildContext&& other) noexcept {
    if (this != &other && !running_ && !other.running_) {
        struct Reset {
            bool& target;
            bool& source;
            ~Reset() { target = source = false; }
        } reset{running_, other.running_};
        running_ = other.running_ = true;
        release_active_frame();
        child_ = std::exchange(other.child_, nullptr);
        evaluator_ = other.evaluator_;
        effects_ = other.effects_;
        active_frame_ = std::exchange(other.active_frame_, nullptr);
        state_ = other.state_;
        mode_ = other.mode_;
        evaluation_ = other.evaluation_;
        id_low_ = other.id_low_;
        incoming_flag_ = other.incoming_flag_;
        computed_active_ = other.computed_active_;
    }
    return *this;
}

VmfNotificationChildSnapshot
VmfNotificationChildContext::snapshot() const noexcept {
    return state_;
}

void VmfNotificationChildContext::finish(std::uint8_t result) noexcept {
    state_.result = result;
    state_.phase = VmfNotificationChildPhase::finished;
}

void VmfNotificationChildContext::select_effect(
    VmfNotificationChildEffect effect) noexcept {
    state_.pending_effect = effect;
    state_.phase = VmfNotificationChildPhase::effects_binding;
}

void VmfNotificationChildContext::release_active_frame() noexcept {
    if (auto* frame = std::exchange(active_frame_, nullptr))
        frame->release();
}

std::expected<VmfNotificationChildReport, VmfNotificationStateError>
VmfNotificationChildContext::run(const VmfNotificationChildInputs& inputs,
                                 std::size_t step_budget) noexcept {
    if (!child_ || running_ || state_.faulted)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Child notification frame is invalid/faulted/running"});

    if (step_budget == 0) {
        return VmfNotificationChildReport{
            state_.phase == VmfNotificationChildPhase::finished
                ? VmfNotificationChildStatus::finished
                : VmfNotificationChildStatus::step_limit,
            state_, 0};
    }

    struct Guard {
        bool& running;
        explicit Guard(bool& flag) noexcept : running(flag) { running = true; }
        ~Guard() { running = false; }
    } guard(running_);

    using Phase = VmfNotificationChildPhase;
    using Status = VmfNotificationChildStatus;
    using Effect = VmfNotificationChildEffect;
    std::size_t steps = 0;
    const auto report = [&](Status status) {
        return VmfNotificationChildReport{status, state_, steps};
    };
    const auto fault = [&](VmfNotificationStateError error)
        -> std::expected<VmfNotificationChildReport,
                         VmfNotificationStateError> {
        release_active_frame();
        state_.faulted = true;
        return std::unexpected(error);
    };

    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::active:
            if (!child_->active_08)
                return report(Status::needs_active_flag);
            if (*child_->active_08 == 0)
                finish(0);
            else
                state_.phase = Phase::mask;
            break;
        case Phase::mask: {
            if (!child_->mask_0c)
                return report(Status::needs_mask);
            const auto mask = *child_->mask_0c;
            if ((mask & mode_) != mask)
                finish(0);
            else
                state_.phase = Phase::evaluator_binding;
            break;
        }
        case Phase::evaluator_binding:
            if (!inputs.evaluator)
                return report(Status::needs_evaluator);
            if (!*inputs.evaluator)
                return fault(
                    {VmfNotificationStateErrorCode::invalid_native_binding,
                     "Child evaluator binding is NULL"});
            evaluator_ = *inputs.evaluator;
            state_.phase = Phase::evaluate;
            break;
        case Phase::evaluate: {
            const auto result = evaluator_->evaluate(*child_, nullptr);
            if (!result)
                return fault(result.error());
            ++state_.completed_evaluations;
            evaluation_ = *result;
            state_.phase = Phase::limit;
            break;
        }
        case Phase::limit:
            if (!child_->limit_18)
                return report(Status::needs_limit);
            computed_active_ = evaluation_ < *child_->limit_18 ? 1 : 0;
            state_.phase = Phase::write_active;
            break;
        case Phase::write_active:
            child_->active_08 = computed_active_;
            ++state_.active_stores;
            state_.phase =
                computed_active_ == 0 ? Phase::id_low : Phase::global_flag;
            break;
        case Phase::global_flag:
            if (!inputs.flag_16a2d2f)
                return report(Status::needs_global_flag);
            state_.phase =
                *inputs.flag_16a2d2f == 0 ? Phase::final_active : Phase::id_low;
            break;
        case Phase::id_low:
            if (!child_->id_low_30)
                return report(Status::needs_id_low);
            id_low_ = *child_->id_low_30;
            state_.phase = Phase::id_high;
            break;
        case Phase::id_high: {
            if (!child_->id_high_34)
                return report(Status::needs_id_high);
            const auto high = *child_->id_high_34;
            if ((id_low_ == 0xffffffff && high == 0xffffffff) ||
                (id_low_ == 0x517180 && high == 0)) {
                if (incoming_flag_ != 0)
                    state_.phase = Phase::final_active;
                else
                    select_effect(Effect::call_89db30);
            } else {
                select_effect(Effect::call_89e330);
            }
            break;
        }
        case Phase::effects_binding:
            if (!effects_) {
                if (!inputs.effects)
                    return report(Status::needs_effects);
                if (!*inputs.effects)
                    return fault(
                        {VmfNotificationStateErrorCode::invalid_native_binding,
                         "Child effects module binding is NULL"});
                effects_ = *inputs.effects;
            }
            state_.phase = Phase::effect;
            break;
        case Phase::effect: {
            const auto effect = *state_.pending_effect;
            if (!active_frame_) {
                const auto begin_res = effects_->begin_effect(effect, *child_);
                if (!begin_res)
                    return fault(begin_res.error());
                active_frame_ = *begin_res;
            }

            if (active_frame_) {
                const auto poll_res = active_frame_->run(1);
                if (!poll_res)
                    return fault(poll_res.error());
                if (*poll_res == VmfNotificationEffectStatus::pending) {
                    ++steps;
                    ++state_.steps;
                    return report(Status::effect_pending);
                }
                release_active_frame();
            }

            ++state_.completed_effects;
            state_.pending_effect.reset();
            if (effect == Effect::call_89e330) {
                if (incoming_flag_ != 0)
                    finish(0);
                else
                    select_effect(Effect::call_89bf80);
            } else if (effect == Effect::call_89bf80) {
                finish(0);
            } else {
                state_.phase = Phase::final_active;
            }
            break;
        }
        case Phase::final_active:
            if (!child_->active_08)
                return report(Status::needs_active_flag);
            finish(*child_->active_08 == 0 ? 1 : 0);
            break;
        case Phase::finished:
            break;
        }
        ++steps;
        ++state_.steps;
    }
    return report(state_.phase == Phase::finished ? Status::finished
                                                  : Status::step_limit);
}

} // namespace tdur::ui

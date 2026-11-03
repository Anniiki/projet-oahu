#include "tdur/ui/vmf_help_message.hpp"

#include <algorithm>
#include <utility>

namespace tdur::ui {
namespace {
VmfHelpMessageError error(VmfHelpMessageErrorCode code,
                          std::string_view detail) noexcept {
    return {code, detail, {}, {}};
}
} // namespace

VmfHelpMessageContext::VmfHelpMessageContext(
    VmfHelpMessageState& manager, VmfHelpMessageRequest request,
    VmfTargetLookupLimits target_limits) noexcept
    : manager_(&manager), request_(std::move(request)),
      target_limits_(target_limits) {
    state_.normalized_flag_10 = request_.flag_10;
}
VmfHelpMessageContext
VmfHelpMessageContext::create(VmfHelpMessageState& manager,
                              VmfHelpMessageRequest request,
                              VmfTargetLookupLimits target_limits) noexcept {
    return VmfHelpMessageContext{manager, std::move(request), target_limits};
}
VmfHelpMessageContext::VmfHelpMessageContext(
    VmfHelpMessageContext&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)),
      request_(std::move(other.request_)), target_limits_(other.target_limits_),
      state_(other.state_), captured_display_(other.captured_display_),
      target_(other.target_), pending_callback_(other.pending_callback_) {}
VmfHelpMessageContext&
VmfHelpMessageContext::operator=(VmfHelpMessageContext&& other) noexcept {
    if (this != &other) {
        manager_ = std::exchange(other.manager_, nullptr);
        request_ = std::move(other.request_);
        target_limits_ = other.target_limits_;
        state_ = other.state_;
        captured_display_ = other.captured_display_;
        target_ = other.target_;
        pending_callback_ = other.pending_callback_;
    }
    return *this;
}
VmfHelpMessageSnapshot VmfHelpMessageContext::snapshot() const noexcept {
    return state_;
}
std::optional<VmfHelpCallbackInvocation>
VmfHelpMessageContext::pending_callback() const noexcept {
    return manager_ ? pending_callback_ : std::nullopt;
}

std::expected<VmfHelpMessageReport, VmfHelpMessageError>
VmfHelpMessageContext::run(const VmfHelpMessageInputs& inputs,
                           std::size_t step_budget) noexcept {
    if (!manager_ || state_.faulted || running_)
        return std::unexpected(
            error(VmfHelpMessageErrorCode::invalid_context,
                  "Help message context is invalid/faulted"));
    struct RunningGuard {
        bool& active;
        explicit RunningGuard(bool& value) : active(value) { active = true; }
        ~RunningGuard() { active = false; }
    } running(running_);
    auto& manager = *manager_;
    std::size_t steps = 0;
    const auto report = [&](VmfHelpMessageStatus status) {
        return VmfHelpMessageReport{status, state_, steps};
    };
    const auto fault = [&](VmfHelpMessageError failure)
        -> std::expected<VmfHelpMessageReport, VmfHelpMessageError> {
        state_.faulted = true;
        return std::unexpected(failure);
    };
    using Phase = VmfHelpMessagePhase;
    using Status = VmfHelpMessageStatus;
    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::prior_guard:
            state_.phase = manager.active != 0 ? Phase::prior_function
                                               : Phase::store_references;
            break;
        case Phase::prior_function:
        case Phase::prior_object: {
            const auto kind = state_.phase == Phase::prior_function
                                  ? VmfHelpCallbackKind::function
                                  : VmfHelpCallbackKind::object;
            const auto key = kind == VmfHelpCallbackKind::function
                                 ? manager.function_key
                                 : manager.object_key;
            if (!pending_callback_ && key != 0)
                pending_callback_ = VmfHelpCallbackInvocation{
                    kind, key, 2, manager.userdata0, manager.userdata1};
            if (pending_callback_) {
                const auto& invocation = *pending_callback_;
                const auto found = std::ranges::find_if(
                    inputs.callbacks, [&](const auto& binding) {
                        return binding.kind == invocation.kind &&
                               binding.key == invocation.key &&
                               binding.implementation;
                    });
                if (found == inputs.callbacks.end())
                    return report(kind == VmfHelpCallbackKind::function
                                      ? Status::needs_function_callback
                                      : Status::needs_object_callback);
                const auto invoked =
                    found->implementation->invoke(invocation, manager);
                if (!invoked)
                    return fault(invoked.error());
                ++state_.callbacks;
                pending_callback_.reset();
            }
            state_.phase = kind == VmfHelpCallbackKind::function
                               ? Phase::prior_object
                               : Phase::store_references;
            break;
        }
        case Phase::store_references:
            if (state_.normalized_flag_10 == 0 && request_.flag_1 == 0)
                state_.normalized_flag_10 = 1;
            // 9A0433..9A0452 precedes the read of manager+8. Stop at that
            //  unresolved read with precisely this already committed prefix.
            manager.id_low = request_.id_low;
            manager.topic = request_.topic;
            manager.id_high = request_.id_high;
            manager.object_key = request_.object_key;
            state_.phase = Phase::read_display;
            break;
        case Phase::read_display:
            if (!manager.display)
                return report(Status::needs_display_binding);
            captured_display_ = *manager.display;
            state_.phase = Phase::store_remaining;
            break;
        case Phase::store_remaining:
            manager.flag_14 = request_.flag_1;
            manager.function_key = request_.function_key;
            manager.active = 0;
            manager.type = 2;
            manager.text = request_.text;
            manager.userdata0 = request_.userdata0;
            manager.userdata1 = request_.mode;
            manager.stage_38 = 2;
            state_.phase =
                captured_display_ ? Phase::resolve_target : Phase::finished;
            break;
        case Phase::resolve_target: {
            const auto resolved = captured_display_->resolve_target(
                captured_display_->root(), "/tutorial", target_limits_);
            if (!resolved)
                return fault({VmfHelpMessageErrorCode::display,
                              "Help message target rejected",
                              resolved.error(),
                              {}});
            state_.target_found = resolved->found;
            target_ = resolved->target;
            state_.phase =
                resolved->found ? Phase::clear_world : Phase::finished;
            break;
        }
        case Phase::clear_world:
            if (!inputs.world)
                return report(Status::needs_world_binding);
            if (*inputs.world)
                (**inputs.world).flag_1c = 0;
            state_.phase = Phase::activate;
            break;
        case Phase::activate:
            manager.active = 1;
            state_.phase = Phase::label;
            break;
        case Phase::label: {
            if (manager.type != 2)
                return fault(error(
                    VmfHelpMessageErrorCode::unsupported_type_change,
                    "Message type changed into an unported presentation path"));
            const auto label = request_.mode == 1   ? "on_group"
                               : request_.mode == 2 ? "on_group_moto"
                                                    : "on";
            const auto applied =
                captured_display_->apply_host_goto_label(*target_, label, 1);
            if (!applied)
                return fault({VmfHelpMessageErrorCode::display,
                              "Help message label rejected",
                              applied.error(),
                              {}});
            state_.phase = Phase::button_text;
            break;
        }
        case Phase::button_text:
        case Phase::title_text:
        case Phase::body_text: {
            if (!manager.display)
                return report(Status::needs_display_binding);
            if (!*manager.display)
                return fault(error(VmfHelpMessageErrorCode::missing_display,
                                   "Help display disappeared before a write"));
            auto& display = **manager.display;
            if (state_.phase == Phase::body_text) {
                if (manager.type != 2)
                    return fault(error(
                        VmfHelpMessageErrorCode::unsupported_type_change,
                        "Message type changed before the linked text write"));
                if (!request_.text || request_.text->extent_bytes() == 0)
                    return fault(
                        error(VmfHelpMessageErrorCode::missing_text,
                              "Help body lacks a valid owned reference"));
                const auto written = write_vmf_host_linked_text(
                    display, "/:TutorialText", *request_.text, target_limits_);
                if (!written)
                    return fault({VmfHelpMessageErrorCode::host_variables,
                                  "Help linked text rejected",
                                  {},
                                  written.error()});
                state_.phase = Phase::finished;
            } else {
                const bool button = state_.phase == Phase::button_text;
                const auto written = write_vmf_host_text(
                    display, button ? "/:WhatButton" : "/:TutorialTitle",
                    button ? "empty" : "", target_limits_);
                if (!written)
                    return fault({VmfHelpMessageErrorCode::host_variables,
                                  "Help copied text rejected",
                                  {},
                                  written.error()});
                state_.phase = button ? Phase::title_text : Phase::body_text;
            }
            ++state_.host_writes;
            break;
        }
        case Phase::finished:
            break;
        }
        ++steps;
        ++state_.steps;
    }
    return report(state_.phase == Phase::finished ? Status::message_finished
                                                  : Status::step_limit);
}

} // namespace tdur::ui

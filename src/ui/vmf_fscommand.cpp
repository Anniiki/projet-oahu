#include "tdur/ui/vmf_fscommand.hpp"

#include <bit>
#include <utility>

namespace tdur::ui {
namespace {
using Phase = VmfFsCommandPhase;
using Status = VmfFsCommandStatus;
using Code = VmfFsCommandErrorCode;
using Error = VmfFsCommandError;
struct Guard {
    bool& active;
    explicit Guard(bool& flag) noexcept : active(flag) { active = true; }
    ~Guard() { active = false; }
};
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}};
}
} // namespace
VmfFsCommandContext::VmfFsCommandContext(std::string_view url) noexcept
    : url_(url) {}
VmfFsCommandContext::~VmfFsCommandContext() noexcept {
    running_ = true;
    release_callback();
}
void VmfFsCommandContext::release_callback() noexcept {
    if (auto* frame = std::exchange(frame_, nullptr)) {
        ++state_.callback_releases;
        frame->release();
    }
}
VmfFsCommandSnapshot VmfFsCommandContext::snapshot() const noexcept {
    return state_;
}
std::optional<Error> VmfFsCommandContext::last_error() const noexcept {
    return error_;
}
std::expected<void, Error> VmfFsCommandContext::cancel() noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "FSCommand is running"));
    if (state_.phase == Phase::finished || state_.faulted)
        return {};
    Guard guard(running_);
    release_callback();
    error_ =
        error(Code::abandoned, "FSCommand was abandoned without completion");
    state_.faulted = true;
    return {};
}
std::expected<VmfFsCommandReport, Error>
VmfFsCommandContext::run(const VmfFsCommandInputs& inputs,
                         std::size_t budget) noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "FSCommand is running"));
    if (state_.faulted)
        return std::unexpected(*error_);
    Guard guard(running_);
    std::size_t steps{};
    const auto report = [&](Status status) {
        return VmfFsCommandReport{status, state_, steps};
    };
    const auto fail = [&](Error reason) {
        release_callback();
        error_ = reason;
        state_.faulted = true;
        return std::unexpected(reason);
    };
    const auto nested_error = [&](Code code, const Error& cause) {
        return Error{code, cause.detail, cause.code};
    };
    while (steps < budget && state_.phase != Phase::finished) {
        switch (state_.phase) {
        case Phase::prefix:
            if (!url_.starts_with("FSCommand:")) {
                state_.ignored_prefix = true;
                state_.phase = Phase::finished;
            } else {
                command_ = url_.substr(10);
                state_.phase = Phase::first_owner;
            }
            break;
        case Phase::first_owner:
        case Phase::after_log:
        case Phase::after_leave:
        case Phase::post_owner:
            if (!inputs.globals.current_10e8078)
                return report(Status::needs_owner);
            owner_ = *inputs.globals.current_10e8078;
            if (!owner_)
                return fail(error(Code::invalid_binding,
                                  "Current FSCommand owner is NULL"));
            state_.phase =
                state_.phase == Phase::first_owner   ? Phase::log_level
                : state_.phase == Phase::after_log   ? Phase::initial_flag
                : state_.phase == Phase::after_leave ? Phase::userdata
                                                     : Phase::post_flag;
            break;
        case Phase::log_level:
            if (!owner_->log_level_30)
                return report(Status::needs_log_level);
            state_.phase =
                std::bit_cast<std::int32_t>(*owner_->log_level_30) > 0
                    ? Phase::log
                    : Phase::initial_flag;
            break;
        case Phase::log: {
            if (!inputs.logger)
                return report(Status::needs_logger);
            if (!*inputs.logger)
                return fail(error(Code::invalid_binding, "Logger is NULL"));
            ++state_.logger_calls;
            const auto result = (*inputs.logger)->write(command_);
            if (!result)
                return fail(nested_error(Code::logger, result.error()));
            state_.phase = Phase::after_log;
            break;
        }
        case Phase::initial_flag:
            if (!owner_->flag_35)
                return report(Status::needs_flag);
            saved_ = owner_;
            state_.phase = *owner_->flag_35 ? Phase::leave : Phase::userdata;
            break;
        case Phase::leave:
        case Phase::enter: {
            if (!gate_) {
                if (!inputs.gate)
                    return report(Status::needs_gate);
                gate_ = *inputs.gate;
                if (!gate_)
                    return fail(error(Code::invalid_binding, "Gate is NULL"));
            }
            const bool leave = state_.phase == Phase::leave;
            if (leave)
                ++state_.leave_calls;
            else
                ++state_.enter_calls;
            const auto result = leave ? gate_->leave() : gate_->enter();
            if (!result)
                return fail(nested_error(Code::gate, result.error()));
            state_.phase = leave ? Phase::after_leave : Phase::restore;
            break;
        }
        case Phase::userdata:
            if (!owner_->userdata_9cc)
                return report(Status::needs_userdata);
            userdata_ = *owner_->userdata_9cc;
            state_.phase = Phase::callback_binding;
            break;
        case Phase::callback_binding:
            if (!owner_->callback_9c8)
                return report(Status::needs_callback);
            callback_ = *owner_->callback_9c8;
            if (!callback_)
                return fail(
                    error(Code::invalid_binding, "Callback9C8 is NULL"));
            state_.phase = Phase::callback_begin;
            break;
        case Phase::callback_begin: {
            ++state_.callback_begins;
            const auto begun = callback_->begin(command_, userdata_);
            if (!begun)
                return fail(nested_error(Code::callback, begun.error()));
            frame_ = *begun;
            state_.phase = frame_ ? Phase::callback_run : Phase::post_owner;
            break;
        }
        case Phase::callback_run: {
            ++state_.callback_polls;
            const auto result = frame_->run(1);
            if (!result)
                return fail(nested_error(Code::callback, result.error()));
            if (result->steps > 1)
                return fail(error(Code::invalid_context,
                                  "Callback exceeded its budget"));
            if (result->status != VmfHostCallStatus::finished) {
                if (result->steps) {
                    ++steps;
                    ++state_.steps;
                    if (result->status == VmfHostCallStatus::step_limit)
                        continue;
                }
                return report(Status::callback_pending);
            }
            release_callback();
            state_.phase = Phase::post_owner;
            break;
        }
        case Phase::post_flag:
            if (!owner_->flag_35)
                return report(Status::needs_flag);
            state_.phase = *owner_->flag_35 ? Phase::enter : Phase::restore;
            break;
        case Phase::restore:
            inputs.globals.current_10e8078 = saved_;
            inputs.globals.secondary_10e807c = saved_;
            state_.global_stores += 2;
            state_.phase = Phase::finished;
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

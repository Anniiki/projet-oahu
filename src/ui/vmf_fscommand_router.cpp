#include "tdur/ui/vmf_fscommand_router.hpp"

#include <bit>
#include <stdexcept>
#include <utility>

namespace tdur::ui {
namespace {
using Phase = VmfFsRouterPhase;
using Status = VmfFsRouterStatus;
using Error = VmfFsCommandError;
using Code = VmfFsCommandErrorCode;
struct Guard {
    bool& active;
    explicit Guard(bool& flag) noexcept : active(flag) { active = true; }
    ~Guard() { active = false; }
};
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}};
}
} // namespace
VmfFsRouterContext::VmfFsRouterContext(std::string_view command,
                                       const void* userdata) noexcept
    : command_(command), userdata_(userdata) {}
VmfFsRouterContext::~VmfFsRouterContext() noexcept {
    running_ = true;
    release_handler();
}
void VmfFsRouterContext::release_handler() noexcept {
    if (auto* frame = std::exchange(frame_, nullptr)) {
        ++state_.handler_releases;
        frame->release();
    }
}
VmfFsRouterSnapshot VmfFsRouterContext::snapshot() const noexcept {
    return state_;
}
std::optional<Error> VmfFsRouterContext::last_error() const noexcept {
    return error_;
}
std::expected<void, Error> VmfFsRouterContext::cancel() noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "Router is running"));
    if (state_.faulted || state_.phase == Phase::finished)
        return {};
    Guard guard(running_);
    release_handler();
    state_.faulted = true;
    error_ = error(Code::abandoned, "Router was abandoned without completion");
    return {};
}
std::expected<VmfFsRouterReport, Error>
VmfFsRouterContext::run(const VmfFsRouterInputs& inputs,
                        std::size_t budget) noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "Router is running"));
    if (state_.faulted)
        return std::unexpected(*error_);
    Guard guard(running_);
    std::size_t steps{};
    const auto report = [&](Status status) {
        return VmfFsRouterReport{status, state_, steps};
    };
    const auto fail = [&](Error reason) {
        release_handler();
        error_ = reason;
        state_.faulted = true;
        return std::unexpected(reason);
    };
    const auto nested = [&](Code code, const Error& cause) {
        return Error{code, cause.detail, cause.code};
    };
    while (steps < budget && state_.phase != Phase::finished) {
        switch (state_.phase) {
        case Phase::manager:
            if (!inputs.manager_10e7284)
                return report(Status::needs_manager);
            manager_ = *inputs.manager_10e7284;
            if (!manager_)
                return fail(
                    error(Code::invalid_binding, "Manager10E7284 is NULL"));
            state_.phase = Phase::initial_movie;
            break;
        case Phase::initial_movie:
        case Phase::post_movie: {
            if (!manager_->movie_e8)
                return report(Status::needs_movie);
            movie_ = *manager_->movie_e8;
            if (state_.phase == Phase::initial_movie) {
                state_.initial_no_movie = !movie_;
                state_.phase =
                    movie_ ? Phase::diagnostic_enabled : Phase::finished;
            } else {
                if (!movie_)
                    return fail(error(Code::invalid_binding,
                                      "Post-global movieE8 is NULL"));
                state_.phase = Phase::movie_handler;
            }
            break;
        }
        case Phase::diagnostic_enabled:
            if (!inputs.enabled_1699170)
                return report(Status::needs_diagnostic_flag);
            state_.phase = *inputs.enabled_1699170 ? Phase::diagnostic_owner
                                                   : Phase::global_handler;
            break;
        case Phase::diagnostic_owner:
            if (!inputs.diagnostic_owner_169919c)
                return report(Status::needs_diagnostic_owner);
            diagnostic_owner_ = *inputs.diagnostic_owner_169919c;
            if (!diagnostic_owner_)
                return fail(
                    error(Code::invalid_binding, "Diagnostic owner is NULL"));
            state_.phase = Phase::diagnostic_level;
            break;
        case Phase::diagnostic_level:
            if (!diagnostic_owner_->level_30)
                return report(Status::needs_diagnostic_level);
            state_.phase =
                std::bit_cast<std::int32_t>(*diagnostic_owner_->level_30) >= 3
                    ? Phase::diagnostic_allowed
                    : Phase::global_handler;
            break;
        case Phase::diagnostic_allowed:
            if (!inputs.allowed_faa719)
                return report(Status::needs_diagnostic_flag);
            state_.phase = *inputs.allowed_faa719 ? Phase::diagnostic_sink
                                                  : Phase::global_handler;
            break;
        case Phase::diagnostic_sink:
            if (!inputs.sink_fa8fd4)
                return report(Status::needs_diagnostic_sink);
            sink_ = *inputs.sink_fa8fd4;
            state_.phase =
                sink_ ? Phase::diagnostic_write : Phase::global_handler;
            break;
        case Phase::diagnostic_write: {
            ++state_.diagnostic_calls;
            const auto result = sink_->write(command_);
            if (!result)
                return fail(nested(Code::logger, result.error()));
            state_.phase =
                *result ? Phase::diagnostic_probe : Phase::global_handler;
            break;
        }
        case Phase::diagnostic_probe: {
            if (!inputs.probe_9ec154)
                return report(Status::needs_probe);
            if (!*inputs.probe_9ec154)
                return fail(
                    error(Code::invalid_binding, "Diagnostic probe is NULL"));
            ++state_.probe_calls;
            const auto result = (*inputs.probe_9ec154)->query();
            if (!result)
                return fail(nested(Code::callback, result.error()));
            state_.phase =
                *result ? Phase::diagnostic_interrupt : Phase::global_handler;
            break;
        }
        case Phase::diagnostic_interrupt: {
            if (!inputs.breakpoint)
                return report(Status::needs_breakpoint);
            if (!*inputs.breakpoint)
                return fail(error(Code::invalid_binding,
                                  "Diagnostic breakpoint is NULL"));
            ++state_.breakpoint_calls;
            const auto result = (*inputs.breakpoint)->interrupt();
            if (!result)
                return fail(nested(Code::callback, result.error()));
            state_.phase = Phase::global_handler;
            break;
        }
        case Phase::global_handler:
        case Phase::movie_handler: {
            const bool global = state_.phase == Phase::global_handler;
            const auto& binding =
                global ? manager_->global_f7c : movie_->receiver_ac0;
            if (!binding)
                return report(global ? Status::needs_global_handler
                                     : Status::needs_movie_handler);
            handler_ = *binding;
            if (!handler_) {
                if (global)
                    return fail(error(Code::invalid_binding,
                                      "Global handlerF7C is NULL"));
                state_.movie_no_handler = true;
                state_.phase = Phase::finished;
            } else {
                state_.phase =
                    global ? Phase::global_begin : Phase::movie_begin;
            }
            break;
        }
        case Phase::global_begin:
        case Phase::movie_begin: {
            const bool global = state_.phase == Phase::global_begin;
            if (global)
                ++state_.global_begins;
            else
                ++state_.movie_begins;
            const auto begun = handler_->begin(command_, userdata_);
            if (!begun)
                return fail(nested(Code::callback, begun.error()));
            frame_ = begun->frame;
            if (global) {
                if (!frame_)
                    state_.global_result = begun->result;
                state_.phase =
                    frame_ ? Phase::global_run : Phase::global_decide;
            } else {
                state_.phase = frame_ ? Phase::movie_run : Phase::finished;
            }
            break;
        }
        case Phase::global_run:
        case Phase::movie_run: {
            ++state_.handler_polls;
            const auto result = frame_->run(1);
            if (!result)
                return fail(nested(Code::callback, result.error()));
            if (result->steps > 1)
                return fail(error(Code::invalid_context,
                                  "Handler exceeded its budget"));
            if (result->status != VmfHostCallStatus::finished) {
                if (result->steps) {
                    ++steps;
                    ++state_.steps;
                    if (result->status == VmfHostCallStatus::step_limit)
                        continue;
                }
                return report(Status::handler_pending);
            }
            if (state_.phase == Phase::global_run) {
                state_.phase = Phase::global_result;
            } else {
                release_handler();
                state_.phase = Phase::finished;
            }
            break;
        }
        case Phase::global_result: {
            ++state_.result_reads;
            const auto result = frame_->result();
            if (!result)
                return fail(nested(Code::callback, result.error()));
            state_.global_result = *result;
            release_handler();
            state_.phase = Phase::global_decide;
            break;
        }
        case Phase::global_decide:
            state_.phase =
                *state_.global_result ? Phase::finished : Phase::post_movie;
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

struct VmfFsDefaultCallback::Slot final : VmfHostCallFrame {
    const VmfFsRouterInputs* inputs{};
    std::optional<VmfFsRouterContext> context;
    bool leased{}, running{};
    std::expected<VmfHostCallReport, Error>
    run(std::size_t budget) noexcept override {
        if (!leased || running)
            return std::unexpected(
                error(Code::invalid_context, "Router frame is not runnable"));
        Guard guard(running);
        const auto result = context->run(*inputs, budget);
        if (!result)
            return std::unexpected(result.error());
        const auto status = result->status == Status::finished
                                ? VmfHostCallStatus::finished
                            : result->status == Status::step_limit
                                ? VmfHostCallStatus::step_limit
                                : VmfHostCallStatus::pending;
        return VmfHostCallReport{status, result->steps};
    }
    void release() noexcept override {
        if (!leased || running)
            return;
        Guard guard(running);
        (void)context->cancel();
        context.reset();
        leased = false;
    }
};
VmfFsDefaultCallback::VmfFsDefaultCallback(const VmfFsRouterInputs& inputs,
                                           std::size_t capacity) {
    if (capacity == 0 || capacity > 32)
        throw std::invalid_argument("FSCommand router capacity must be1..32");
    slots_ = std::make_unique<Slot[]>(capacity);
    capacity_ = capacity;
    for (std::size_t i = 0; i < capacity_; ++i)
        slots_[i].inputs = &inputs;
}
VmfFsDefaultCallback::~VmfFsDefaultCallback() noexcept {
    closing_ = true;
    for (std::size_t i = 0; i < capacity_; ++i)
        slots_[i].release();
}
std::expected<VmfHostCallFrame*, Error>
VmfFsDefaultCallback::begin(std::string_view command,
                            const void* userdata) noexcept {
    if (closing_)
        return std::unexpected(
            error(Code::invalid_context, "Router pool is closing"));
    for (std::size_t i = 0; i < capacity_; ++i) {
        auto& slot = slots_[i];
        if (slot.leased)
            continue;
        slot.leased = true;
        slot.context.emplace(command, userdata);
        const auto first = slot.context->run(*slot.inputs, 1);
        if (!first) {
            slot.release();
            return std::unexpected(first.error());
        }
        return &slot;
    }
    return std::unexpected(error(Code::invalid_context, "Router pool is full"));
}
std::size_t VmfFsDefaultCallback::capacity() const noexcept {
    return capacity_;
}
std::size_t VmfFsDefaultCallback::leased() const noexcept {
    std::size_t count{};
    for (std::size_t i = 0; i < capacity_; ++i)
        count += static_cast<std::size_t>(slots_[i].leased);
    return count;
}
} // namespace tdur::ui

#include "tdur/ui/vmf_fscommand.hpp"
#include "tdur/ui/vmf_fscommand_host.hpp"

#include <atomic>
#include <bit>
#include <stdexcept>
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

// Modern adapter, no native counterpart: hosts the envelope above for the
// executor's GetURL83/9A calls (443AF0 -> 445CB0/445D40). It never calls an
// out-of-line VmfTextReference/VmfProgram function (copies and destruction
// only), so this library gains no link dependency.
struct VmfFsHostActions::Slot {
    enum class State { free, reserved, in_flight };
    State state{};
    std::uint16_t opcode{};
    std::uint32_t generation = 1;
    bool running{};
    VmfFsCommandStatus last_status{};
    VmfProgramPtr program;
    std::optional<VmfTextReference> url_backing;
    std::optional<VmfFsCommandContext> command; // emplace/reset, never moved
};
namespace {
using HostCode = VmfHostActionErrorCode;
std::unexpected<VmfHostActionError>
host_error(HostCode code, std::string_view detail,
           std::optional<VmfFsCommandError> command = {}) noexcept {
    return std::unexpected(VmfHostActionError{code, detail, command});
}
// Process-unique adapter serial, never zero, without allocation.
std::uint32_t next_host_serial() noexcept {
    static std::atomic<std::uint32_t> counter{0};
    for (;;) {
        const auto value = counter.fetch_add(1, std::memory_order_relaxed) + 1U;
        if (value != 0)
            return value;
    }
}
} // namespace
VmfFsHostActions::VmfFsHostActions(const VmfFsCommandInputs& inputs,
                                   std::size_t capacity)
    : inputs_(&inputs) {
    if (capacity == 0 || capacity > 32)
        throw std::invalid_argument("FSCommand host capacity must be 1..32");
    slots_ = std::make_unique<Slot[]>(capacity);
    capacity_ = capacity;
    serial_ = next_host_serial();
}
VmfFsHostActions::~VmfFsHostActions() noexcept {
    closing_ = true;
    for (std::size_t i = 0; i < capacity_; ++i)
        if (slots_[i].state != Slot::State::free)
            release(slots_[i]);
}
VmfFsHostActions::Slot*
VmfFsHostActions::find(VmfHostTicket ticket) const noexcept {
    if (ticket.host != serial_ || ticket.slot >= capacity_)
        return nullptr;
    auto& slot = slots_[ticket.slot];
    return slot.state != Slot::State::free &&
                   slot.generation == ticket.generation
               ? &slot
               : nullptr;
}
void VmfFsHostActions::release(Slot& slot) noexcept {
    // Stale before any foreign release runs; reentry refused meanwhile.
    ++slot.generation;
    slot.running = true;
    slot.command.reset(); // A frame still held is released exactly once.
    slot.url_backing.reset();
    slot.program.reset();
    slot.state = Slot::State::free;
    slot.running = false;
}
std::expected<VmfHostActionReport, VmfHostActionError>
VmfFsHostActions::run(Slot& slot, std::size_t budget) noexcept {
    slot.running = true;
    const auto result = slot.command->run(*inputs_, budget);
    slot.running = false;
    if (!result) {
        release(slot); // Prefix retained; no completion, no restore.
        return host_error(HostCode::command, "FSCommand envelope failed",
                          result.error());
    }
    slot.last_status = result->status;
    switch (result->status) {
    case Status::finished:
        release(slot);
        return VmfHostActionReport{VmfHostActionStatus::finished,
                                   result->steps};
    case Status::step_limit:
        return VmfHostActionReport{VmfHostActionStatus::step_limit,
                                   result->steps};
    default: // needs_* and callback_pending: unknown inputs wait.
        return VmfHostActionReport{VmfHostActionStatus::waiting, result->steps};
    }
}
bool VmfFsHostActions::accepts(std::uint16_t opcode) const noexcept {
    return opcode == 0x83 || opcode == 0x9a;
}
std::expected<VmfHostTicket, VmfHostActionError>
VmfFsHostActions::reserve(std::uint16_t opcode) noexcept {
    if (closing_)
        return host_error(HostCode::closing, "FSCommand host is closing");
    if (!accepts(opcode))
        return host_error(HostCode::invalid_request,
                          "FSCommand host serves GetURL actions only");
    for (std::size_t i = 0; i < capacity_; ++i) {
        auto& slot = slots_[i];
        if (slot.state != Slot::State::free)
            continue;
        slot.state = Slot::State::reserved;
        slot.opcode = opcode;
        slot.last_status = {};
        return VmfHostTicket{serial_, static_cast<std::uint32_t>(i),
                             slot.generation};
    }
    return host_error(HostCode::capacity, "FSCommand host capacity reached");
}
std::expected<VmfHostActionReport, VmfHostActionError>
VmfFsHostActions::begin_url(VmfHostTicket ticket,
                            const VmfHostUrlRequest& request,
                            std::size_t budget) noexcept {
    if (closing_)
        return host_error(HostCode::closing, "FSCommand host is closing");
    auto* slot = find(ticket);
    if (!slot)
        return host_error(HostCode::invalid_ticket,
                          "FSCommand host ticket is stale or foreign");
    if (slot->running)
        return host_error(HostCode::reentry, "FSCommand host slot is running");
    if (slot->state != Slot::State::reserved)
        return host_error(HostCode::invalid_request,
                          "FSCommand host ticket is already in flight");
    const bool literal = request.opcode == 0x83;
    const bool coherent =
        request.opcode == slot->opcode && request.url.data() &&
        (literal ? request.program && !request.url_backing
                 : request.url_backing && !request.url.empty() &&
                       request.url.back() == '\0');
    if (!coherent) {
        release(*slot);
        return host_error(HostCode::invalid_request,
                          "FSCommand host request is incoherent");
    }
    slot->program = request.program;
    slot->url_backing = request.url_backing;
    slot->command.emplace(request.url);
    slot->state = Slot::State::in_flight;
    return run(*slot, budget);
}
std::expected<VmfHostActionReport, VmfHostActionError>
VmfFsHostActions::resume(VmfHostTicket ticket, std::size_t budget) noexcept {
    if (closing_)
        return host_error(HostCode::closing, "FSCommand host is closing");
    auto* slot = find(ticket);
    if (!slot)
        return host_error(HostCode::invalid_ticket,
                          "FSCommand host ticket is stale or foreign");
    if (slot->running)
        return host_error(HostCode::reentry, "FSCommand host slot is running");
    if (slot->state != Slot::State::in_flight)
        return host_error(HostCode::invalid_request,
                          "FSCommand host ticket has not begun");
    return run(*slot, budget);
}
std::expected<void, VmfHostActionError>
VmfFsHostActions::cancel(VmfHostTicket ticket) noexcept {
    if (closing_)
        return host_error(HostCode::closing, "FSCommand host is closing");
    auto* slot = find(ticket);
    if (!slot)
        return host_error(HostCode::invalid_ticket,
                          "FSCommand host ticket is stale or foreign");
    if (slot->running)
        return host_error(HostCode::reentry, "FSCommand host slot is running");
    if (slot->state == Slot::State::in_flight) {
        slot->running = true;
        const auto cancelled = slot->command->cancel();
        slot->running = false;
        if (!cancelled)
            return host_error(HostCode::command, "FSCommand cancel refused",
                              cancelled.error());
    }
    release(*slot);
    return {};
}
std::expected<VmfFsHostSnapshot, VmfHostActionError>
VmfFsHostActions::inspect(VmfHostTicket ticket) const noexcept {
    if (closing_)
        return host_error(HostCode::closing, "FSCommand host is closing");
    const auto* slot = find(ticket);
    if (!slot)
        return host_error(HostCode::invalid_ticket,
                          "FSCommand host ticket is stale or foreign");
    if (slot->state != Slot::State::in_flight)
        return host_error(HostCode::invalid_request,
                          "FSCommand host ticket has not begun");
    return VmfFsHostSnapshot{slot->opcode, slot->last_status,
                             slot->command->snapshot()};
}
std::size_t VmfFsHostActions::capacity() const noexcept { return capacity_; }
std::size_t VmfFsHostActions::leased() const noexcept {
    std::size_t count{};
    for (std::size_t i = 0; i < capacity_; ++i)
        count += static_cast<std::size_t>(slots_[i].state != Slot::State::free);
    return count;
}
} // namespace tdur::ui

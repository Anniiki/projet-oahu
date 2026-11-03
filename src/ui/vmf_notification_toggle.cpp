#include "tdur/ui/vmf_notification_toggle.hpp"

namespace tdur::ui {

VmfNotificationToggleContext::VmfNotificationToggleContext(
    VmfNotificationToggleOwner& owner, std::uint32_t argument,
    VmfTargetLookupLimits limits) noexcept
    : owner_(owner), limits_(limits) {
    state_.argument_byte = static_cast<std::uint8_t>(argument);
}
VmfNotificationToggleSnapshot
VmfNotificationToggleContext::snapshot() const noexcept {
    return state_;
}
std::expected<VmfNotificationToggleReport, VmfNotificationToggleError>
VmfNotificationToggleContext::run(const VmfNotificationToggleInputs& inputs,
                                  std::size_t step_budget) noexcept {
    using Phase = VmfNotificationTogglePhase;
    using Status = VmfNotificationToggleStatus;
    using Code = VmfNotificationToggleErrorCode;
    if (running_ || state_.faulted)
        return std::unexpected(VmfNotificationToggleError{
            Code::invalid_context, "Toggle context is active/faulted", {}});
    struct Reset {
        bool& value;
        ~Reset() { value = false; }
    } reset{running_};
    running_ = true;
    std::size_t steps{};
    const auto report = [&](Status status) {
        return VmfNotificationToggleReport{status, state_, steps};
    };
    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::store:
            owner_.flag_fa = state_.argument_byte;
            ++state_.flag_stores;
            state_.phase = Phase::movie;
            break;
        case Phase::movie: {
            if (!inputs.movies)
                return report(Status::needs_movies);
            ++state_.movie_lookups;
            const auto* movie = find_vmf_active_menu(*inputs.movies, 1, 0);
            if (!movie || movie->kind != 1 || !movie->display) {
                state_.phase = Phase::finished;
                break;
            }
            display_ = movie->display;
            state_.phase = Phase::name;
            break;
        }
        case Phase::name:
            if (!inputs.expression_f0a8f8)
                return report(Status::needs_name);
            name_ = *inputs.expression_f0a8f8;
            state_.phase = Phase::write;
            break;
        case Phase::write: {
            ++state_.setter_calls;
            const auto written = write_vmf_host_integer(
                *display_, name_, state_.argument_byte != 0 ? 1 : 0, limits_);
            if (!written) {
                state_.faulted = true;
                return std::unexpected(VmfNotificationToggleError{
                    Code::setter, written.error().detail, written.error()});
            }
            state_.phase = Phase::finished;
            break;
        }
        case Phase::finished:
            break;
        }
        ++steps;
        ++state_.steps;
    }
    return report(state_.phase == Phase::finished ? Status::finished
                                                  : Status::step_limit);
}

VmfNotificationToggleFrame::VmfNotificationToggleFrame(
    VmfNotificationToggleOwner& owner,
    const VmfNotificationToggleInputs& inputs,
    VmfTargetLookupLimits limits) noexcept
    : owner_(owner), inputs_(inputs), limits_(limits) {}
VmfNotificationToggleFrame::~VmfNotificationToggleFrame() { release(); }
std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationToggleFrame::begin(std::uint32_t argument) noexcept {
    if (invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Toggle frame is leased/running"});
    error_.reset();
    invocation_.emplace(owner_, argument, limits_);
    return this;
}
std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
VmfNotificationToggleFrame::run(std::size_t step_budget) noexcept {
    if (!invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Toggle frame is not runnable"});
    struct Reset {
        bool& value;
        ~Reset() { value = false; }
    } reset{running_};
    running_ = true;
    const auto r = invocation_->run(inputs_, step_budget);
    if (!r) {
        error_ = r.error();
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::host_failure, r.error().detail});
    }
    return r->status == VmfNotificationToggleStatus::finished
               ? VmfNotificationEffectStatus::finished
               : VmfNotificationEffectStatus::pending;
}
void VmfNotificationToggleFrame::release() noexcept {
    if (running_ || releasing_)
        return;
    releasing_ = true;
    invocation_.reset();
    releasing_ = false;
}
std::optional<VmfNotificationToggleSnapshot>
VmfNotificationToggleFrame::snapshot() const noexcept {
    return invocation_ ? std::optional{invocation_->snapshot()} : std::nullopt;
}
std::optional<VmfNotificationToggleError>
VmfNotificationToggleFrame::last_error() const noexcept {
    return error_;
}
} // namespace tdur::ui

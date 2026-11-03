#include "tdur/ui/vmf_notification_flag.hpp"

#include <utility>

namespace tdur::ui {

VmfNotificationFlagContext::VmfNotificationFlagContext(
    VmfNotificationFlagOwner& owner, std::uint32_t argument,
    VmfTargetLookupLimits limits) noexcept
    : owner_(&owner), argument_(static_cast<std::uint8_t>(argument)),
      limits_(limits) {}

VmfNotificationFlagContext::VmfNotificationFlagContext(
    VmfNotificationFlagContext&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      selected_(std::exchange(other.selected_, nullptr)),
      argument_(other.argument_), limits_(other.limits_), phase_(other.phase_),
      faulted_(other.faulted_),
      host_call_completed_(other.host_call_completed_) {}

VmfNotificationFlagContext& VmfNotificationFlagContext::operator=(
    VmfNotificationFlagContext&& other) noexcept {
    if (this != &other) {
        owner_ = std::exchange(other.owner_, nullptr);
        selected_ = std::exchange(other.selected_, nullptr);
        argument_ = other.argument_;
        limits_ = other.limits_;
        phase_ = other.phase_;
        faulted_ = other.faulted_;
        host_call_completed_ = other.host_call_completed_;
    }
    return *this;
}

VmfNotificationFlagSnapshot
VmfNotificationFlagContext::snapshot() const noexcept {
    return {phase_, faulted_, host_call_completed_};
}

std::expected<VmfNotificationFlagReport, VmfNotificationFlagError>
VmfNotificationFlagContext::run(const VmfNotificationFlagInputs& inputs,
                                std::size_t step_budget) noexcept {
    using Phase = VmfNotificationFlagPhase;
    using Status = VmfNotificationFlagStatus;
    using Error = VmfNotificationFlagErrorCode;
    if (!owner_ || faulted_)
        return std::unexpected(
            VmfNotificationFlagError{Error::invalid_context, {}});
    std::size_t steps{};
    const auto report = [&](Status status) {
        return VmfNotificationFlagReport{status, snapshot(), steps};
    };
    while (phase_ != Phase::finished) {
        if (steps == step_budget)
            return report(Status::step_limit);
        switch (phase_) {
        case Phase::store_flag:
            owner_->flag_fa = argument_;
            phase_ = Phase::select_menu;
            break;
        case Phase::select_menu: {
            if (!inputs.movies)
                return report(Status::needs_movies);
            const auto* movie = find_vmf_active_menu(*inputs.movies, 1, 0);
            if (!movie || movie->kind != 1 || !movie->display) {
                phase_ = Phase::finished;
            } else {
                selected_ = movie->display;
                phase_ = Phase::write_text;
            }
            break;
        }
        case Phase::write_text: {
            if (!inputs.expression)
                return report(Status::needs_expression);
            if (!inputs.expression->extent_bytes()) {
                faulted_ = true;
                return std::unexpected(
                    VmfNotificationFlagError{Error::invalid_expression, {}});
            }
            const auto written =
                write_vmf_host_integer(*selected_, inputs.expression->text(),
                                       argument_ != 0 ? 1 : 0, limits_);
            if (!written) {
                faulted_ = true;
                return std::unexpected(VmfNotificationFlagError{
                    Error::host_variables, written.error()});
            }
            host_call_completed_ = true;
            phase_ = Phase::finished;
            break;
        }
        case Phase::finished:
            break;
        }
        ++steps;
    }
    return report(Status::finished);
}

} // namespace tdur::ui

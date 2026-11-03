#include "tdur/ui/vmf_notification_state.hpp"

#include <utility>

namespace tdur::ui {
namespace {
struct RunGuard {
    bool& value;
    explicit RunGuard(bool& flag) noexcept : value(flag) { value = true; }
    ~RunGuard() { value = false; }
};
VmfNotificationStateError invalid_context() noexcept {
    return {VmfNotificationStateErrorCode::invalid_context,
            "Notification context is invalid/faulted/running"};
}
} // namespace

VmfNotificationStateContext::VmfNotificationStateContext(
    VmfNotificationState& owner, VmfNotificationStateRoutine routine) noexcept
    : owner_(&owner), state_{routine, VmfNotificationStatePhase::count, {}} {}
VmfNotificationStateContext
VmfNotificationStateContext::sum(VmfNotificationState& owner) noexcept {
    return {owner, VmfNotificationStateRoutine::sum_89bd90};
}
VmfNotificationStateContext
VmfNotificationStateContext::level(VmfNotificationState& owner) noexcept {
    return {owner, VmfNotificationStateRoutine::level_89c210};
}
VmfNotificationStateContext::VmfNotificationStateContext(
    VmfNotificationStateContext&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), state_(other.state_),
      children_(other.children_), child_(other.child_) {}
VmfNotificationStateContext& VmfNotificationStateContext::operator=(
    VmfNotificationStateContext&& other) noexcept {
    if (this != &other) {
        owner_ = std::exchange(other.owner_, nullptr);
        state_ = other.state_;
        children_ = other.children_;
        child_ = other.child_;
    }
    return *this;
}
VmfNotificationStateSnapshot
VmfNotificationStateContext::snapshot() const noexcept {
    return state_;
}
void VmfNotificationStateContext::finish_sum() noexcept {
    if (state_.routine == VmfNotificationStateRoutine::sum_89bd90) {
        state_.result = state_.sum;
        state_.phase = VmfNotificationStatePhase::finished;
    } else
        state_.phase = VmfNotificationStatePhase::threshold;
}
std::expected<VmfNotificationStateReport, VmfNotificationStateError>
VmfNotificationStateContext::run(std::size_t step_budget) noexcept {
    if (!owner_ || running_ || state_.faulted)
        return std::unexpected(invalid_context());
    RunGuard guard(running_);
    std::size_t steps = 0;
    using Phase = VmfNotificationStatePhase;
    using Status = VmfNotificationStateStatus;
    const auto report = [&](Status status) {
        return VmfNotificationStateReport{status, state_, steps};
    };
    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::count:
            if (!owner_->count_0c)
                return report(Status::needs_count);
            state_.captured_count = *owner_->count_0c;
            if (*state_.captured_count <= 0)
                finish_sum();
            else
                state_.phase = Phase::children;
            break;
        case Phase::children:
            if (!owner_->children_08)
                return report(Status::needs_children);
            children_ = *owner_->children_08;
            state_.phase = Phase::child;
            break;
        case Phase::child:
            if (state_.row >= children_.size()) {
                state_.faulted = true;
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::row_out_of_range,
                    "Native child access exceeds owned array storage"});
            }
            if (!children_[state_.row])
                return report(Status::needs_child);
            child_ = *children_[state_.row];
            ++state_.slots_read;
            state_.phase = child_ ? Phase::active : Phase::next;
            break;
        case Phase::active:
            if (!child_->active_08)
                return report(Status::needs_active_flag);
            state_.phase =
                *child_->active_08 == 0 ? Phase::weight : Phase::next;
            break;
        case Phase::weight:
            if (!child_->weight_14)
                return report(Status::needs_weight);
            state_.sum += *child_->weight_14;
            ++state_.weights_read;
            state_.phase = Phase::next;
            break;
        case Phase::next:
            ++state_.row;
            if (state_.row == static_cast<std::size_t>(*state_.captured_count))
                finish_sum();
            else
                state_.phase = Phase::child;
            break;
        case Phase::threshold:
            if (!owner_->thresholds[state_.threshold_index])
                return report(Status::needs_threshold);
            ++state_.thresholds_read;
            if (state_.sum >= *owner_->thresholds[state_.threshold_index]) {
                state_.result =
                    static_cast<std::uint32_t>(state_.threshold_index + 1);
                state_.phase = Phase::finished;
            } else if (state_.threshold_index == 0) {
                state_.result = 0;
                state_.phase = Phase::finished;
            } else
                --state_.threshold_index;
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

VmfNotificationStoreContext::VmfNotificationStoreContext(
    VmfNotificationLevelDestination& target) noexcept
    : target_(&target) {}
VmfNotificationStoreContext VmfNotificationStoreContext::create(
    VmfNotificationLevelDestination& target) noexcept {
    return VmfNotificationStoreContext{target};
}
VmfNotificationStoreContext::VmfNotificationStoreContext(
    VmfNotificationStoreContext&& other) noexcept
    : target_(std::exchange(other.target_, nullptr)), host_(other.host_),
      state_(other.state_), computation_(std::move(other.computation_)) {}
VmfNotificationStoreContext& VmfNotificationStoreContext::operator=(
    VmfNotificationStoreContext&& other) noexcept {
    if (this != &other) {
        target_ = std::exchange(other.target_, nullptr);
        host_ = other.host_;
        state_ = other.state_;
        computation_ = std::move(other.computation_);
    }
    return *this;
}
VmfNotificationStoreSnapshot
VmfNotificationStoreContext::snapshot() const noexcept {
    auto result = state_;
    if (computation_)
        result.computation = computation_->snapshot();
    return result;
}
std::expected<VmfNotificationStoreReport, VmfNotificationStateError>
VmfNotificationStoreContext::run(std::optional<VmfNotificationStateHost*> host,
                                 std::size_t step_budget) noexcept {
    if (!target_ || running_ || state_.faulted)
        return std::unexpected(invalid_context());
    RunGuard guard(running_);
    std::size_t steps = 0;
    using Phase = VmfNotificationStorePhase;
    using Status = VmfNotificationStoreStatus;
    const auto report =
        [&](Status status,
            std::optional<VmfNotificationStateStatus> nested = {}) {
            return VmfNotificationStoreReport{status, snapshot(), steps,
                                              nested};
        };
    const auto fault = [&](VmfNotificationStateError error)
        -> std::expected<VmfNotificationStoreReport,
                         VmfNotificationStateError> {
        state_.faulted = true;
        return std::unexpected(error);
    };
    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::host:
            if (!host)
                return report(Status::needs_host);
            if (!*host)
                return fault(
                    {VmfNotificationStateErrorCode::invalid_native_binding,
                     "Native global host would be NULL"});
            host_ = *host;
            state_.phase = Phase::query;
            break;
        case Phase::query: {
            const auto owner = host_->notification_state();
            if (!owner)
                return fault(owner.error());
            ++state_.completed_host_calls;
            if (!*owner)
                return fault(
                    {VmfNotificationStateErrorCode::invalid_native_binding,
                     "Native virtual+4C result would be NULL"});
            computation_.emplace(VmfNotificationStateContext::level(**owner));
            state_.phase = Phase::level;
            break;
        }
        case Phase::level: {
            const auto before = computation_->snapshot().steps;
            const auto nested = computation_->run(step_budget - steps);
            const auto consumed = computation_->snapshot().steps - before;
            steps += consumed;
            state_.steps += consumed;
            if (!nested)
                return fault(nested.error());
            if (nested->status != VmfNotificationStateStatus::finished)
                return report(nested->status ==
                                      VmfNotificationStateStatus::step_limit
                                  ? Status::step_limit
                                  : Status::state_pending,
                              nested->status);
            state_.phase = Phase::store;
            continue; // Nested steps already consumed the shared budget.
        }
        case Phase::store:
            target_->level_f028 = *computation_->snapshot().result;
            ++state_.stores;
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

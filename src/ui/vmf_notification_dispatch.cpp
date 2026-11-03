#include "tdur/ui/vmf_notification_dispatch.hpp"

#include <utility>

namespace tdur::ui {
namespace {

using Code = VmfNotificationDispatchErrorCode;
using Error = VmfNotificationDispatchError;
using Phase = VmfNotificationDispatchPhase;
using Status = VmfNotificationDispatchStatus;

Error error(Code code, std::string_view detail) noexcept {
    return Error{code, detail, {}, {}, {}, {}, {}};
}

} // namespace

VmfNotificationDispatchContext::VmfNotificationDispatchContext(
    VmfNotificationChildState& child, VmfTargetLookupLimits limits) noexcept
    : child_(child), limits_(limits) {}

VmfNotificationDispatchContext::~VmfNotificationDispatchContext() noexcept {
    running_ = true;
    release_parent_frame();
}

VmfNotificationDispatchSnapshot
VmfNotificationDispatchContext::snapshot() const noexcept {
    return state_;
}

void VmfNotificationDispatchContext::finish(std::uint8_t result) noexcept {
    state_.result = result;
    state_.phase = Phase::finished;
}

void VmfNotificationDispatchContext::release_parent_frame() noexcept {
    if (auto* frame = std::exchange(parent_frame_, nullptr))
        frame->release();
}

std::expected<VmfNotificationDispatchReport, VmfNotificationDispatchError>
VmfNotificationDispatchContext::run(const VmfNotificationDispatchInputs& inputs,
                                    std::size_t step_budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(
            error(Code::invalid_context, "Dispatch context is active/faulted"));

    if (step_budget == 0) {
        return VmfNotificationDispatchReport{state_.phase == Phase::finished
                                                 ? Status::finished
                                                 : Status::step_limit,
                                             state_, 0};
    }

    struct Guard {
        bool& flag;
        explicit Guard(bool& f) noexcept : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(running_);

    std::size_t steps = 0;
    const auto report = [&](Status status) {
        return VmfNotificationDispatchReport{status, state_, steps};
    };
    const auto fault =
        [&](Error reason) -> std::expected<VmfNotificationDispatchReport,
                                           VmfNotificationDispatchError> {
        release_parent_frame();
        state_.faulted = true;
        return std::unexpected(reason);
    };

    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::id_low:
            if (!child_.id_low_30)
                return report(Status::needs_id_low);
            id_low_ = *child_.id_low_30;
            state_.captured_id_low = id_low_;
            state_.phase = Phase::id_high;
            break;

        case Phase::id_high:
            if (!child_.id_high_34)
                return report(Status::needs_id_high);
            id_high_ = *child_.id_high_34;
            state_.captured_id_high = id_high_;
            state_.phase = Phase::routing;
            break;

        case Phase::routing:
            // Sentinel 1: FFFFFFFF/FFFFFFFF -> AL=0, no service
            if (id_low_ == 0xffffffffU && id_high_ == 0xffffffffU) {
                finish(0);
                break;
            }
            // Sentinel 2: 517180/0 -> AL=0, no service
            if (id_low_ == 0x00517180U && id_high_ == 0U) {
                finish(0);
                break;
            }
            // Other non-zero id_high -> AL=1, no service
            if (id_high_ != 0U) {
                finish(1);
                break;
            }

            // Only 6 specific id_low values have effects when id_high == 0:
            if (id_low_ == 0x0052f820U || id_low_ == 0x00578c00U ||
                id_low_ == 0x00560560U) {
                state_.phase = Phase::ui_display_first;
            } else if (id_low_ == 0x005912a0U || id_low_ == 0x005a9940U) {
                state_.phase = Phase::flag_capture_host;
            } else if (id_low_ == 0x005c1fe0U) {
                state_.phase = Phase::identity_first_host;
            } else {
                finish(1);
            }
            break;

        // --- UI Branches (52F820, 578C00, 560560) ---
        case Phase::ui_display_first: {
            if (!inputs.globals.movies)
                return report(Status::needs_movies);
            ++state_.lookup_first_calls;
            auto* display1 =
                find_vmf_active_display(*inputs.globals.movies, 1, 0);
            if (!display1) {
                // First lookup NULL -> AL1, no second lookup or setter
                finish(1);
                break;
            }
            state_.phase = Phase::ui_name;
            break;
        }

        case Phase::ui_name: {
            std::optional<std::string_view> chosen_name;
            if (id_low_ == 0x0052f820U)
                chosen_name = inputs.globals.name_f16204;
            else if (id_low_ == 0x00578c00U)
                chosen_name = inputs.globals.name_f161e0;
            else
                chosen_name = inputs.globals.name_f161f0;

            if (!chosen_name)
                return report(Status::needs_name);
            ui_name_ = *chosen_name;
            state_.phase = Phase::ui_display_second;
            break;
        }

        case Phase::ui_display_second: {
            if (!inputs.globals.movies)
                return report(Status::needs_movies);
            ++state_.lookup_second_calls;
            auto* display2 =
                find_vmf_active_display(*inputs.globals.movies, 1, 0);
            if (!display2) {
                return fault(error(Code::missing_display,
                                   "Second UI display lookup is NULL"));
            }
            selected_display_ = display2;
            state_.phase = Phase::ui_write;
            break;
        }

        case Phase::ui_write: {
            ++state_.host_setter_calls;
            auto write_res = write_vmf_host_integer(*selected_display_,
                                                    ui_name_, 1, limits_);
            if (!write_res) {
                auto err = error(Code::display_error, write_res.error().detail);
                err.display_error = write_res.error();
                return fault(err);
            }
            finish(1);
            break;
        }

        // --- Flags Branches (5912A0, 5A9940) ---
        case Phase::flag_capture_host:
            if (!inputs.globals.host)
                return report(Status::needs_host);
            captured_host_ = *inputs.globals.host;
            state_.phase = Phase::flag_store_global;
            break;

        case Phase::flag_store_global: {
            const auto flag = id_low_ == 0x005912a0U
                                  ? inputs.globals.flag_10e7d34
                                  : inputs.globals.flag_10e7d35;
            if (!flag)
                return report(Status::needs_global_flag);
            if (!*flag)
                return fault(
                    error(Code::invalid_binding, "Global flag cell is NULL"));
            **flag = std::uint8_t{1};
            if (id_low_ == 0x005912a0U)
                ++state_.flag_10e7d34_stores;
            else
                ++state_.flag_10e7d35_stores;
            state_.phase = Phase::flag_query_state;
            break;
        }

        case Phase::flag_query_state: {
            if (!captured_host_)
                return fault(error(Code::invalid_binding,
                                   "Captured global host is NULL"));
            ++state_.host_state_calls;
            auto state_res = captured_host_->notification_state();
            if (!state_res) {
                auto err = error(Code::state_error, state_res.error().detail);
                err.state_error = state_res.error();
                return fault(err);
            }
            auto* st = *state_res;
            if (!st)
                return fault(error(Code::invalid_binding,
                                   "Returned notification state is NULL"));

            if (id_low_ == 0x005912a0U) {
                st->flag_3550 = std::uint8_t{1};
                ++state_.flag_3550_stores;
            } else {
                st->flag_3551 = std::uint8_t{1};
                ++state_.flag_3551_stores;
            }
            finish(1);
            break;
        }

        // --- Identity Branch (5C1FE0) ---
        case Phase::identity_first_host: {
            if (!inputs.globals.host)
                return report(Status::needs_host);
            auto* host = *inputs.globals.host;
            if (!host)
                return fault(
                    error(Code::invalid_binding, "Global host is NULL"));
            ++state_.host_identities_first_calls;
            auto id_res = host->notification_identities();
            if (!id_res) {
                auto err = error(Code::state_error, id_res.error().detail);
                err.state_error = id_res.error();
                return fault(err);
            }
            identities_ = *id_res;
            state_.phase = Phase::identity_first_name;
            break;
        }

        case Phase::identity_first_name:
            if (!inputs.globals.name_f161d0)
                return report(Status::needs_name);
            identity_name_ = *inputs.globals.name_f161d0;
            state_.phase = Phase::identity_first_hash;
            break;

        case Phase::identity_first_hash: {
            if (!inputs.globals.hash_mode)
                return report(Status::needs_hash_mode);
            ++state_.hash_calls;
            auto h_res = assets::hash_resource_name(identity_name_,
                                                    *inputs.globals.hash_mode);
            if (!h_res) {
                auto err =
                    error(Code::hash_error, "Failed to hash resource name");
                err.hash_error = h_res.error();
                return fault(err);
            }
            hash_words_ =
                crypto::XteaWords{static_cast<std::uint32_t>(*h_res),
                                  static_cast<std::uint32_t>(*h_res >> 32)};
            state_.phase = Phase::identity_first_lookup;
            break;
        }

        case Phase::identity_first_lookup: {
            if (!identities_)
                return fault(error(Code::invalid_binding,
                                   "Notification identity owner is NULL"));
            auto find_res = find_vmf_notification_record_identity(
                identities_->records, identities_->key, hash_words_);
            if (!find_res) {
                auto err = error(Code::identity_error,
                                 "Identity record lookup range error");
                err.identity_error = find_res.error();
                return fault(err);
            }
            if (find_res->result != 0) {
                // Identity found -> AL1 immediately, no further calls
                finish(1);
                break;
            }
            state_.phase = Phase::identity_second_host;
            break;
        }

        case Phase::identity_second_host: {
            if (!inputs.globals.host)
                return report(Status::needs_host);
            auto* host2 = *inputs.globals.host;
            if (!host2)
                return fault(
                    error(Code::invalid_binding, "Second global host is NULL"));
            ++state_.host_identities_second_calls;
            auto id2_res = host2->notification_identities();
            if (!id2_res) {
                auto err = error(Code::state_error, id2_res.error().detail);
                err.state_error = id2_res.error();
                return fault(err);
            }
            identities2_ = *id2_res;
            state_.phase = Phase::identity_second_name;
            break;
        }

        case Phase::identity_second_name:
            state_.phase = Phase::identity_second_hash;
            break;

        case Phase::identity_second_hash: {
            if (!inputs.globals.hash_mode)
                return report(Status::needs_hash_mode);
            ++state_.hash_calls;
            auto h2_res = assets::hash_resource_name(identity_name_,
                                                     *inputs.globals.hash_mode);
            if (!h2_res) {
                auto err = error(Code::hash_error,
                                 "Failed to hash resource name second time");
                err.hash_error = h2_res.error();
                return fault(err);
            }
            hash_words_ =
                crypto::XteaWords{static_cast<std::uint32_t>(*h2_res),
                                  static_cast<std::uint32_t>(*h2_res >> 32)};
            state_.phase = Phase::assignment_begin;
            break;
        }

        case Phase::assignment_begin:
            if (!identities2_)
                return fault(error(Code::invalid_binding,
                                   "Second identity owner is NULL"));
            assignment_context_.emplace(identities2_->records,
                                        identities2_->key, hash_words_);
            ++state_.assignment_begins;
            state_.phase = Phase::assignment_run;
            break;

        case Phase::assignment_run: {
            ++state_.assignment_runs;
            std::optional<VmfNotificationAssignmentHost*> cur_host;
            if (inputs.globals.host)
                cur_host = *inputs.globals.host;
            VmfNotificationAssignmentInputs assign_inputs{
                .catalog = inputs.catalog,
                .hash_mode = inputs.globals.hash_mode,
                .host = cur_host,
            };
            auto assign_res = assignment_context_->run(assign_inputs, 1);
            if (!assign_res) {
                auto err =
                    error(Code::assignment_error, assign_res.error().detail);
                err.assignment_error = assign_res.error();
                return fault(err);
            }
            state_.assignment = assign_res->state;
            state_.assignment_status = assign_res->status;
            ++steps;
            ++state_.steps;
            if (assign_res->status ==
                VmfNotificationAssignmentStatus::finished) {
                // Both AL0 and AL1 results of assignment are ignored by 89E330
                state_.phase = Phase::parent_query_host;
            } else if (assign_res->status !=
                           VmfNotificationAssignmentStatus::step_limit ||
                       assign_res->steps == 0) {
                return report(Status::assignment_pending);
            }
            continue;
        }

        case Phase::parent_query_host: {
            if (!inputs.globals.host)
                return report(Status::needs_host);
            auto* host3 = *inputs.globals.host;
            if (!host3)
                return fault(
                    error(Code::invalid_binding, "Third global host is NULL"));
            ++state_.host_state_calls;
            auto st_res = host3->notification_state();
            if (!st_res) {
                auto err = error(Code::state_error, st_res.error().detail);
                err.state_error = st_res.error();
                return fault(err);
            }
            parent_state_ = *st_res;
            if (!parent_state_)
                return fault(error(Code::invalid_binding,
                                   "Parent notification state is NULL"));
            state_.phase = Phase::parent_binding;
            break;
        }

        case Phase::parent_binding:
            if (!inputs.parent)
                return report(Status::needs_parent);
            if (!*inputs.parent)
                return fault(
                    error(Code::invalid_binding, "Parent service is NULL"));
            parent_service_ = *inputs.parent;
            state_.phase = Phase::parent_begin;
            break;

        case Phase::parent_begin: {
            ++state_.parent_begins;
            auto parent_res = parent_service_->begin_parent(*parent_state_, 7U,
                                                            std::uint8_t{0});
            if (!parent_res) {
                auto err = error(Code::parent_error, parent_res.error().detail);
                err.state_error = parent_res.error();
                return fault(err);
            }
            if (*parent_res == nullptr) {
                finish(1);
                break;
            }
            parent_frame_ = *parent_res;
            state_.phase = Phase::parent_run;
            break;
        }

        case Phase::parent_run: {
            ++state_.parent_polls;
            auto poll_res = parent_frame_->run(1);
            if (!poll_res) {
                auto err = error(Code::parent_error, poll_res.error().detail);
                err.state_error = poll_res.error();
                return fault(err);
            }
            if (*poll_res == VmfNotificationEffectStatus::pending) {
                ++steps;
                ++state_.steps;
                return report(Status::parent_pending);
            }
            release_parent_frame();
            finish(1);
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

VmfNotificationDispatchFrame::VmfNotificationDispatchFrame(
    const VmfNotificationDispatchInputs& inputs,
    VmfTargetLookupLimits limits) noexcept
    : inputs_(inputs), limits_(limits) {}
VmfNotificationDispatchFrame::~VmfNotificationDispatchFrame() { release(); }
std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationDispatchFrame::begin(VmfNotificationChildState& child) noexcept {
    if (invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Dispatch frame is leased/running"});
    error_.reset();
    invocation_.emplace(child, limits_);
    return this;
}
std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
VmfNotificationDispatchFrame::run(std::size_t step_budget) noexcept {
    if (!invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Dispatch frame is not runnable"});
    struct Reset {
        bool& value;
        ~Reset() { value = false; }
    } reset{running_};
    running_ = true;
    const auto result = invocation_->run(inputs_, step_budget);
    if (!result) {
        error_ = result.error();
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::host_failure,
            result.error().detail});
    }
    return result->status == VmfNotificationDispatchStatus::finished
               ? VmfNotificationEffectStatus::finished
               : VmfNotificationEffectStatus::pending;
}
void VmfNotificationDispatchFrame::release() noexcept {
    if (running_ || releasing_)
        return;
    releasing_ = true;
    invocation_.reset();
    releasing_ = false;
}
std::optional<VmfNotificationDispatchSnapshot>
VmfNotificationDispatchFrame::snapshot() const noexcept {
    return invocation_ ? std::optional{invocation_->snapshot()} : std::nullopt;
}
std::optional<VmfNotificationDispatchError>
VmfNotificationDispatchFrame::last_error() const noexcept {
    return error_;
}

} // namespace tdur::ui

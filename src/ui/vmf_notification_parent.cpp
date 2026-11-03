#include "tdur/ui/vmf_notification_parent.hpp"

#include <stdexcept>
#include <system_error>
#include <utility>

namespace tdur::ui {
namespace {

using Code = VmfNotificationParentErrorCode;
using Error = VmfNotificationParentError;
using Phase = VmfNotificationParentPhase;
using Status = VmfNotificationParentStatus;

Error error(Code code, std::string_view detail) noexcept {
    return Error{code, detail, {}, {}, {}};
}

} // namespace

VmfNotificationParentContext::VmfNotificationParentContext(
    VmfNotificationParentOwner& owner, std::uint32_t mode,
    std::uint8_t incoming_flag, VmfTargetLookupLimits limits) noexcept
    : owner_(owner), mode_(mode), incoming_flag_(incoming_flag),
      limits_(limits) {}

VmfNotificationParentContext::~VmfNotificationParentContext() noexcept {
    running_ = true;
    unlock_owner();
    release_nested_frames();
}

std::expected<void, VmfNotificationParentError>
VmfNotificationParentContext::lock_owner() noexcept {
    if (!locked_1a8_) {
        try {
            owner_.mutex_1a8.lock();
            locked_1a8_ = true;
            lock_thread_ = std::this_thread::get_id();
        } catch (const std::system_error&) {
            return std::unexpected(error(
                Code::lock_failed, "Could not acquire parent owner lock"));
        }
    }
    return {};
}

void VmfNotificationParentContext::unlock_owner() noexcept {
    if (locked_1a8_) {
        if (lock_thread_ != std::this_thread::get_id())
            std::terminate();
        owner_.mutex_1a8.unlock();
        locked_1a8_ = false;
        lock_thread_ = {};
    }
}

void VmfNotificationParentContext::release_nested_frames() noexcept {
    if (auto* frame = std::exchange(service_8986b0_frame_, nullptr))
        frame->release();
    active_child_context_.reset();
    active_store_context_.reset();
    active_catalog_display_.reset();
    level_computation_.reset();
}

void VmfNotificationParentContext::finish(std::uint8_t result) noexcept {
    state_.result = result;
    state_.phase = Phase::finished;
}

VmfNotificationParentSnapshot
VmfNotificationParentContext::snapshot() const noexcept {
    return state_;
}

std::expected<VmfNotificationParentReport, VmfNotificationParentError>
VmfNotificationParentContext::run(const VmfNotificationParentInputs& inputs,
                                  std::size_t step_budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(error(Code::invalid_context,
                                     "Parent context is active or faulted"));
    if (locked_1a8_ && lock_thread_ != std::this_thread::get_id())
        return std::unexpected(
            error(Code::invalid_context,
                  "Resume deferred parent on its lock thread"));

    if (step_budget == 0) {
        return VmfNotificationParentReport{state_.phase == Phase::finished
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
        return VmfNotificationParentReport{status, state_, steps};
    };
    const auto fault =
        [&](Error reason) -> std::expected<VmfNotificationParentReport,
                                           VmfNotificationParentError> {
        unlock_owner();
        release_nested_frames();
        state_.faulted = true;
        return std::unexpected(reason);
    };

    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::guard_read_flag_204: {
            if (!owner_.flag_204)
                return report(Status::needs_flag_204);
            captured_flag_204_ = *owner_.flag_204;
            state_.phase = Phase::guard_capture_manager;
            break;
        }

        case Phase::guard_capture_manager: {
            if (!inputs.globals.manager_10e72f0)
                return report(Status::needs_manager);
            captured_manager_ = *inputs.globals.manager_10e72f0;
            state_.phase = Phase::guard_eval_flag_204;
            break;
        }

        case Phase::guard_eval_flag_204: {
            if (captured_flag_204_ == 0U) {
                finish(0);
                break;
            }
            state_.phase = Phase::guard_read_flag_205;
            break;
        }

        case Phase::guard_read_flag_205: {
            if (!owner_.flag_205)
                return report(Status::needs_flag_205);
            if (*owner_.flag_205 == 0U) {
                finish(0);
                break;
            }
            state_.phase = Phase::guard_eval_manager_null;
            break;
        }

        case Phase::guard_eval_manager_null: {
            if (!captured_manager_) {
                finish(0);
                break;
            }
            state_.phase = Phase::guard_read_manager_state;
            break;
        }

        case Phase::guard_read_manager_state: {
            if (!captured_manager_->state_44)
                return report(Status::needs_manager_state);
            const auto st = *captured_manager_->state_44;
            if (st == 0x18U || st == 0U || st == 0x12U) {
                finish(0);
                break;
            }

            if (incoming_flag_ != 0U) {
                state_.phase = Phase::deferred_lock;
            } else {
                state_.phase = Phase::sync_initial_level;
            }
            break;
        }

        // --- Deferred Path (incoming_flag != 0) ---
        case Phase::deferred_lock: {
            auto locked = lock_owner();
            if (!locked)
                return fault(locked.error());
            state_.phase = Phase::deferred_read_pending;
            break;
        }

        case Phase::deferred_read_pending: {
            if (!owner_.pending_208)
                return report(Status::needs_pending_208);
            captured_pending_208_ = *owner_.pending_208;
            if (captured_pending_208_ != 0x80000000U) {
                unlock_owner();
                finish(0);
                break;
            }
            state_.phase = Phase::deferred_capture_queue;
            break;
        }

        case Phase::deferred_capture_queue: {
            if (!inputs.globals.queue_10e7238)
                return report(Status::needs_queue);
            if (*inputs.globals.queue_10e7238 == nullptr) {
                owner_.pending_208 = mode_;
                unlock_owner();
                return fault(
                    error(Code::invalid_binding, "Captured queue is NULL"));
            }
            captured_queue_ = *inputs.globals.queue_10e7238;
            state_.phase = Phase::deferred_enqueue;
            break;
        }

        case Phase::deferred_enqueue: {
            owner_.pending_208 = mode_;
            auto q_res = enqueue_vmf_notification_deferred(
                *captured_queue_, &owner_.deferred_token);
            unlock_owner();
            if (!q_res) {
                auto err = error(Code::queue_error, q_res.error().detail);
                err.queue_error = q_res.error();
                return fault(err);
            }
            // AL0 and AL1 enqueue results are both ignored natively
            finish(0);
            break;
        }

        // --- Synchronous Path (incoming_flag == 0) ---
        case Phase::sync_initial_level: {
            if (!level_computation_)
                level_computation_.emplace(
                    VmfNotificationStateContext::level(owner_.state));
            auto rep = level_computation_->run(1);
            if (!rep) {
                auto err = error(Code::level_error, rep.error().detail);
                err.state_error = rep.error();
                return fault(err);
            }
            if (rep->status != VmfNotificationStateStatus::finished) {
                if (rep->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (rep->status == VmfNotificationStateStatus::step_limit)
                        continue;
                }
                return report(Status::level_pending);
            }
            if (!rep->state.result.has_value()) {
                return fault(
                    error(Code::level_error,
                          "Invariant error: finished level no result"));
            }

            state_.initial_level = *rep->state.result;
            level_computation_.reset();
            ++state_.level_evaluations;
            state_.phase = Phase::sync_flag_2d4;
            break;
        }

        case Phase::sync_flag_2d4: {
            if ((mode_ & 2U) != 0U)
                owner_.field_2d4 = 0xffffffffU;
            state_.phase = Phase::first_pass_init;
            break;
        }

        case Phase::first_pass_init: {
            if (!owner_.state.count_0c)
                return report(Status::needs_count);
            captured_loop_count_ = *owner_.state.count_0c;
            state_.captured_loop_count = captured_loop_count_;
            child_index_ = 0;
            state_.child_index = 0;
            if (captured_loop_count_ <= 0) {
                state_.phase = Phase::recalc_level;
                break;
            }
            state_.phase = Phase::first_pass_slot_base;
            break;
        }

        case Phase::first_pass_slot_base: {
            if (!owner_.state.children_08)
                return report(Status::needs_children);
            auto children_span = *owner_.state.children_08;
            if (child_index_ < 0 || static_cast<std::size_t>(child_index_) >=
                                        children_span.size()) {
                return fault(
                    error(Code::row_out_of_range, "Child slot out of range"));
            }
            auto slot = children_span[static_cast<std::size_t>(child_index_)];
            if (!slot.has_value())
                return report(Status::needs_child);
            ++state_.children_visited;
            if (*slot == nullptr) {
                state_.phase = Phase::first_pass_slot_advance;
                break;
            }
            current_child_ptr_ = *slot;
            state_.phase = Phase::first_pass_capture_flag;
            break;
        }

        case Phase::first_pass_capture_flag: {
            if (!owner_.flag_3539)
                return report(Status::needs_flag_3539);
            captured_child_flag_3539_ = *owner_.flag_3539;
            active_child_context_.emplace(VmfNotificationChildContext::create(
                *current_child_ptr_, mode_, captured_child_flag_3539_));
            state_.phase = Phase::first_pass_child_run;
            break;
        }

        case Phase::first_pass_child_run: {
            VmfNotificationChildInputs c_inputs{
                .evaluator = inputs.child_evaluator,
                .flag_16a2d2f = inputs.flag_16a2d2f,
                .effects = inputs.child_effects};
            auto c_rep = active_child_context_->run(c_inputs, 1);
            if (!c_rep) {
                auto err = error(Code::child_error, c_rep.error().detail);
                err.state_error = c_rep.error();
                return fault(err);
            }
            if (c_rep->status != VmfNotificationChildStatus::finished) {
                if (c_rep->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (c_rep->status == VmfNotificationChildStatus::step_limit)
                        continue;
                }
                return report(Status::child_pending);
            }
            if (!c_rep->state.result.has_value()) {
                return fault(
                    error(Code::child_error,
                          "Invariant error: finished child no result"));
            }

            state_.or_accumulator |= *c_rep->state.result;
            active_child_context_.reset();
            ++state_.children_invoked;
            state_.phase = Phase::first_pass_slot_advance;
            break;
        }

        case Phase::first_pass_slot_advance: {
            if (!owner_.state.count_0c)
                return report(Status::needs_count);
            captured_loop_count_ = *owner_.state.count_0c;
            state_.captured_loop_count = captured_loop_count_;
            child_index_ = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(child_index_) + 1U);
            state_.child_index = child_index_;
            ++state_.first_pass_iterations;
            if (child_index_ < captured_loop_count_) {
                state_.phase = Phase::first_pass_slot_base;
            } else {
                state_.phase = Phase::recalc_level;
            }
            break;
        }

        case Phase::recalc_level: {
            if (!level_computation_)
                level_computation_.emplace(
                    VmfNotificationStateContext::level(owner_.state));
            auto rep = level_computation_->run(1);
            if (!rep) {
                auto err = error(Code::level_error, rep.error().detail);
                err.state_error = rep.error();
                return fault(err);
            }
            if (rep->status != VmfNotificationStateStatus::finished) {
                if (rep->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (rep->status == VmfNotificationStateStatus::step_limit)
                        continue;
                }
                return report(Status::level_pending);
            }
            if (!rep->state.result.has_value()) {
                return fault(error(Code::level_error,
                                   "Invariant error: recalc level no result"));
            }

            state_.recalculated_level = *rep->state.result;
            level_computation_.reset();
            ++state_.level_evaluations;
            state_.phase = Phase::level_changed_decision;
            break;
        }

        case Phase::level_changed_decision: {
            const auto old_lvl = *state_.initial_level;
            const auto new_lvl = *state_.recalculated_level;
            if (old_lvl == new_lvl) {
                state_.phase = Phase::final_ui_lookup;
                break;
            }
            if (old_lvl == 0U) {
                state_.phase = Phase::call_8986b0_begin;
            } else {
                state_.phase = Phase::store_progression_query;
            }
            break;
        }

        case Phase::call_8986b0_begin: {
            if (!inputs.service_8986b0)
                return report(Status::needs_service_8986b0);
            if (!*inputs.service_8986b0)
                return fault(error(Code::invalid_binding,
                                   "Service 8986B0 interface is NULL"));
            active_service_8986b0_ = *inputs.service_8986b0;
            ++state_.service_8986b0_calls;
            auto svc_res = active_service_8986b0_->begin_8986b0(0U);
            if (!svc_res) {
                auto err = error(Code::service_error, svc_res.error().detail);
                err.state_error = svc_res.error();
                return fault(err);
            }
            if (*svc_res == nullptr) {
                state_.phase = Phase::store_progression_query;
                break;
            }
            service_8986b0_frame_ = *svc_res;
            state_.phase = Phase::call_8986b0_run;
            break;
        }

        case Phase::call_8986b0_run: {
            auto p_res = service_8986b0_frame_->run(1);
            if (!p_res) {
                auto err = error(Code::service_error, p_res.error().detail);
                err.state_error = p_res.error();
                return fault(err);
            }
            if (*p_res == VmfNotificationEffectStatus::pending) {
                ++steps;
                ++state_.steps;
                return report(Status::service_8986b0_pending);
            }

            if (auto* frame = std::exchange(service_8986b0_frame_, nullptr))
                frame->release();
            state_.phase = Phase::store_progression_query;
            break;
        }

        case Phase::store_progression_query: {
            if (!inputs.globals.host)
                return report(Status::needs_host);
            if (!*inputs.globals.host)
                return fault(
                    error(Code::invalid_binding, "Global host is NULL"));
            auto* host = *inputs.globals.host;
            auto prog_res = host->notification_progression();
            if (!prog_res) {
                auto err = error(Code::store_error, prog_res.error().detail);
                err.state_error = prog_res.error();
                return fault(err);
            }
            if (!*prog_res)
                return fault(
                    error(Code::invalid_binding, "Progression object is NULL"));

            active_store_context_.emplace(
                VmfNotificationStoreContext::create((*prog_res)->destination));
            ++state_.store_calls;
            state_.phase = Phase::store_progression_run;
            break;
        }

        case Phase::store_progression_run: {
            auto st_res = active_store_context_->run(
                inputs.globals.host ? std::optional{*inputs.globals.host}
                                    : std::nullopt,
                1);
            if (!st_res) {
                auto err = error(Code::store_error, st_res.error().detail);
                err.state_error = st_res.error();
                return fault(err);
            }
            if (st_res->status != VmfNotificationStoreStatus::finished) {
                if (st_res->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (st_res->status ==
                        VmfNotificationStoreStatus::step_limit)
                        continue;
                }
                return report(Status::store_pending);
            }

            active_store_context_.reset();
            state_.phase = Phase::check_flag_3539_store_field10;
            break;
        }

        case Phase::check_flag_3539_store_field10: {
            if (!owner_.flag_3539)
                return report(Status::needs_flag_3539);
            if (*owner_.flag_3539 == 0U)
                owner_.field_10 = std::uint8_t{1};
            state_.phase = Phase::second_pass_init;
            break;
        }

        case Phase::second_pass_init: {
            if (!owner_.state.count_0c)
                return report(Status::needs_count);
            captured_loop_count_ = *owner_.state.count_0c;
            state_.captured_loop_count = captured_loop_count_;
            child_index_ = 0;
            state_.child_index = 0;
            if (captured_loop_count_ <= 0) {
                state_.phase = Phase::final_ui_lookup;
                break;
            }
            state_.phase = Phase::second_pass_slot_base;
            break;
        }

        case Phase::second_pass_slot_base: {
            if (!owner_.state.children_08)
                return report(Status::needs_children);
            auto children_span = *owner_.state.children_08;
            if (child_index_ < 0 || static_cast<std::size_t>(child_index_) >=
                                        children_span.size()) {
                return fault(error(Code::row_out_of_range,
                                   "Second pass child slot out of range"));
            }
            auto slot = children_span[static_cast<std::size_t>(child_index_)];
            if (!slot.has_value())
                return report(Status::needs_child);
            ++state_.children_visited;
            if (*slot == nullptr) {
                state_.phase = Phase::second_pass_slot_advance;
                break;
            }
            current_child_ptr_ = *slot;
            state_.phase = Phase::second_pass_capture_flag;
            break;
        }

        case Phase::second_pass_capture_flag: {
            if (!owner_.flag_3539)
                return report(Status::needs_flag_3539);
            captured_child_flag_3539_ = *owner_.flag_3539;
            active_child_context_.emplace(VmfNotificationChildContext::create(
                *current_child_ptr_, 8U, captured_child_flag_3539_));
            state_.phase = Phase::second_pass_child_run;
            break;
        }

        case Phase::second_pass_child_run: {
            VmfNotificationChildInputs c_inputs{
                .evaluator = inputs.child_evaluator,
                .flag_16a2d2f = inputs.flag_16a2d2f,
                .effects = inputs.child_effects};
            auto c_rep = active_child_context_->run(c_inputs, 1);
            if (!c_rep) {
                auto err = error(Code::child_error, c_rep.error().detail);
                err.state_error = c_rep.error();
                return fault(err);
            }
            if (c_rep->status != VmfNotificationChildStatus::finished) {
                if (c_rep->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (c_rep->status == VmfNotificationChildStatus::step_limit)
                        continue;
                }
                return report(Status::child_pending);
            }
            if (!c_rep->state.result.has_value()) {
                return fault(
                    error(Code::child_error,
                          "Invariant error: finished second child no result"));
            }

            state_.or_accumulator |= *c_rep->state.result;
            active_child_context_.reset();
            ++state_.children_invoked;
            state_.phase = Phase::second_pass_slot_advance;
            break;
        }

        case Phase::second_pass_slot_advance: {
            if (!owner_.state.count_0c)
                return report(Status::needs_count);
            captured_loop_count_ = *owner_.state.count_0c;
            state_.captured_loop_count = captured_loop_count_;
            child_index_ = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(child_index_) + 1U);
            state_.child_index = child_index_;
            ++state_.second_pass_iterations;
            if (child_index_ < captured_loop_count_) {
                state_.phase = Phase::second_pass_slot_base;
            } else {
                state_.phase = Phase::final_ui_lookup;
            }
            break;
        }

        // --- Final UI Path ---
        case Phase::final_ui_lookup: {
            if (!inputs.globals.movies)
                return report(Status::needs_movies);
            auto* movie = find_vmf_active_menu(*inputs.globals.movies, 1U, 0U);
            if (!movie || movie->kind != 1U || !movie->display) {
                finish(state_.or_accumulator);
                break;
            }
            captured_display_ = movie->display;
            state_.phase = Phase::final_ui_owner;
            break;
        }

        case Phase::final_ui_owner: {
            if (!inputs.globals.owner_10e7eec)
                return report(Status::needs_wrapper_owner);
            auto* wrp = *inputs.globals.owner_10e7eec;
            if (!wrp) {
                finish(state_.or_accumulator);
                break;
            }
            captured_wrapper_owner_ = wrp;
            state_.phase = Phase::final_ui_catalog_prepare;
            break;
        }

        case Phase::final_ui_catalog_prepare: {
            if (!inputs.globals.name_f16338)
                return report(Status::needs_name);
            if (!inputs.globals.named_inputs ||
                !inputs.globals.catalog_arguments)
                return report(Status::needs_catalog_inputs);

            active_catalog_display_.emplace(
                *inputs.globals.named_inputs, captured_wrapper_owner_,
                captured_display_,
                VmfNotificationFormatSource::borrowed(
                    std::span<const char>{*inputs.globals.name_f16338}),
                0x00000000037BF702ULL, *inputs.globals.catalog_arguments,
                limits_);
            ++state_.catalog_display_calls;
            state_.phase = Phase::final_ui_catalog_run;
            break;
        }

        case Phase::final_ui_catalog_run: {
            if (!inputs.globals.catalog_inputs)
                return report(Status::needs_catalog_inputs);
            auto cat_res =
                active_catalog_display_->run(*inputs.globals.catalog_inputs, 1);
            if (!cat_res) {
                auto err =
                    error(Code::catalog_display_error, cat_res.error().detail);
                err.catalog_display_error = cat_res.error();
                return fault(err);
            }
            if (cat_res->status !=
                VmfNotificationCatalogDisplayStatus::finished) {
                if (cat_res->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (cat_res->status ==
                        VmfNotificationCatalogDisplayStatus::step_limit)
                        continue;
                }
                return report(Status::catalog_display_pending);
            }

            active_catalog_display_.reset();
            finish(state_.or_accumulator);
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

VmfNotificationParentFrame::VmfNotificationParentFrame(
    const VmfNotificationParentInputs& inputs,
    VmfTargetLookupLimits limits) noexcept
    : inputs_(inputs), limits_(limits) {}

VmfNotificationParentFrame::~VmfNotificationParentFrame() { release(); }

bool VmfNotificationParentFrame::is_leased() const noexcept {
    return invocation_.has_value() || running_ || releasing_;
}

std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationParentFrame::begin(VmfNotificationParentOwner& owner,
                                  std::uint32_t mode,
                                  std::uint8_t incoming_flag) noexcept {
    if (invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Parent frame is leased or currently running"});
    error_.reset();
    invocation_.emplace(owner, mode, incoming_flag, limits_);
    return this;
}

std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
VmfNotificationParentFrame::run(std::size_t step_budget) noexcept {
    if (!invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "Parent frame is not runnable"});

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

    return result->status == VmfNotificationParentStatus::finished
               ? VmfNotificationEffectStatus::finished
               : VmfNotificationEffectStatus::pending;
}

void VmfNotificationParentFrame::release() noexcept {
    if (running_ || releasing_)
        return;
    releasing_ = true;
    invocation_.reset();
    releasing_ = false;
}

std::optional<VmfNotificationParentSnapshot>
VmfNotificationParentFrame::snapshot() const noexcept {
    return invocation_ ? std::optional{invocation_->snapshot()} : std::nullopt;
}

std::optional<VmfNotificationParentError>
VmfNotificationParentFrame::last_error() const noexcept {
    return error_;
}

VmfNotificationParentServicePool::VmfNotificationParentServicePool(
    const VmfNotificationParentInputs& inputs,
    std::span<const Binding> bindings, std::size_t pool_capacity,
    VmfTargetLookupLimits limits)
    : inputs_(inputs), bindings_(bindings.begin(), bindings.end()) {
    if (pool_capacity < 1 || pool_capacity > 32) {
        throw std::invalid_argument(
            "Parent service pool capacity must be in range [1, 32]");
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        auto* s = bindings[i].state;
        auto* o = bindings[i].owner;
        if (!s || !o)
            throw std::invalid_argument(
                "Binding state or owner cannot be null");
        if (&o->state != s)
            throw std::invalid_argument("Binding owner state mismatch");
        for (std::size_t j = i + 1; j < bindings.size(); ++j) {
            if (bindings[j].state == s)
                throw std::invalid_argument(
                    "Duplicate state binding in pool registry");
        }
    }
    frames_.reserve(pool_capacity);
    for (std::size_t i = 0; i < pool_capacity; ++i) {
        frames_.push_back(
            std::make_unique<VmfNotificationParentFrame>(inputs_, limits));
    }
}

std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationParentServicePool::begin_parent(
    VmfNotificationState& state, std::uint32_t mode,
    std::uint8_t incoming_flag) noexcept {
    VmfNotificationParentOwner* target_owner = nullptr;
    for (const auto& binding : bindings_) {
        if (binding.state == &state) {
            target_owner = binding.owner;
            break;
        }
    }
    if (!target_owner || &target_owner->state != &state) {
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_native_binding,
            "State is not registered to an authoritative ParentOwner"});
    }

    for (auto& frame : frames_) {
        if (!frame->is_leased()) {
            return frame->begin(*target_owner, mode, incoming_flag);
        }
    }

    return std::unexpected(
        VmfNotificationStateError{VmfNotificationStateErrorCode::host_failure,
                                  "Parent frame pool exhausted"});
}

std::size_t VmfNotificationParentServicePool::active_leases() const noexcept {
    std::size_t active = 0;
    for (const auto& frame : frames_) {
        if (frame->is_leased())
            ++active;
    }
    return active;
}

std::size_t VmfNotificationParentServicePool::capacity() const noexcept {
    return frames_.size();
}

} // namespace tdur::ui

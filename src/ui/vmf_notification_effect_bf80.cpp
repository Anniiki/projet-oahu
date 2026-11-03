#include "tdur/ui/vmf_notification_effect_bf80.hpp"

#include <exception>
#include <utility>

namespace tdur::ui {
namespace {
using Phase = VmfNotificationEffectBF80Phase;
using Status = VmfNotificationEffectBF80Status;
using Code = VmfNotificationEffectBF80ErrorCode;
using Error = VmfNotificationEffectBF80Error;
Error error(Code code, std::string_view detail) noexcept {
    return {.code = code, .detail = detail};
}
struct Guard {
    bool& active;
    explicit Guard(bool& flag) noexcept : active(flag) { active = true; }
    ~Guard() { active = false; }
};
} // namespace

VmfNotificationEffectBF80Context::VmfNotificationEffectBF80Context(
    VmfNotificationChildEffectData& data) noexcept
    : data_(data) {}
VmfNotificationEffectBF80Context::~VmfNotificationEffectBF80Context() noexcept {
    running_ = true;
    cancel_format();
}
void VmfNotificationEffectBF80Context::cancel_format() noexcept {
    if (std::exchange(formatting_, false) && !formatter_->cancel())
        std::terminate(); // Exclusive stable lease is a caller contract.
}
void VmfNotificationEffectBF80Context::cancel_active_formatter() noexcept {
    if (running_ || state_.phase == Phase::finished || state_.faulted)
        return;
    Guard guard(running_);
    cancel_format();
    error_ = error(Code::invalid_context, "BF80 invocation was abandoned");
    state_.faulted = true;
}
VmfNotificationEffectBF80Snapshot
VmfNotificationEffectBF80Context::snapshot() const noexcept {
    return state_;
}
std::optional<Error>
VmfNotificationEffectBF80Context::last_error() const noexcept {
    return error_;
}
std::expected<VmfNotificationEffectBF80Report, Error>
VmfNotificationEffectBF80Context::run(const VmfNotificationBF80Inputs& inputs,
                                      std::size_t budget) noexcept {
    if (running_)
        return std::unexpected(error(Code::invalid_context, "BF80 is running"));
    if (state_.faulted)
        return std::unexpected(*error_);
    Guard guard(running_);
    std::size_t steps{};
    const auto report = [&](Status status) {
        return VmfNotificationEffectBF80Report{status, snapshot(), steps};
    };
    const auto fail = [&](Error reason) {
        cancel_format();
        error_ = reason;
        state_.faulted = true;
        return std::unexpected(reason);
    };
    while (steps < budget && state_.phase != Phase::finished) {
        switch (state_.phase) {
        case Phase::idle:
            state_.phase = Phase::initial_owner_guard;
            break;
        case Phase::initial_owner_guard:
            if (!inputs.globals.owner_10e7eec)
                return report(Status::needs_owner);
            if (!*inputs.globals.owner_10e7eec) {
                state_.skipped_null_owner = true;
                state_.phase = Phase::finished;
            } else {
                state_.phase = Phase::clock_read;
            }
            break;
        case Phase::clock_read: {
            if (!inputs.clock)
                return report(Status::needs_clock);
            ++state_.clock_calls;
            const auto time = inputs.clock->get_local_time();
            if (!time) {
                auto reason = error(Code::clock_failed, time.error().detail);
                reason.state_error = time.error();
                return fail(reason);
            }
            // GetLocalTime is observable even though89BF80 never reads it.
            state_.phase = Phase::key_38_check;
            break;
        }
        case Phase::key_38_check:
            if (!data_.key_38)
                return report(Status::needs_key_38);
            key_ = *data_.key_38;
            state_.captured_key_38 = key_;
            state_.skipped_zero_key = key_ == 0;
            state_.phase = key_ ? Phase::db_owner : Phase::clear_buffer;
            break;
        case Phase::db_owner:
            if (!inputs.globals.db_owner_11ee900)
                return report(Status::needs_db_owner);
            database_ = *inputs.globals.db_owner_11ee900;
            if (!database_)
                return fail(error(Code::invalid_binding,
                                  "Fixed database object is NULL"));
            state_.phase = Phase::db_lookup_record;
            break;
        case Phase::db_lookup_record: {
            ++state_.record_calls;
            const auto found = find_vmf_notification_record_9a9480(
                database_->record_table, key_);
            if (!found) {
                auto reason =
                    error(Code::db_lookup_failed, found.error().detail);
                reason.db_error = found.error();
                return fail(reason);
            }
            record_ = *found;
            state_.skipped_null_record = !record_;
            state_.phase =
                record_ ? Phase::db_lookup_name : Phase::clear_buffer;
            break;
        }
        case Phase::db_lookup_name: {
            ++state_.name_calls;
            const auto found =
                find_vmf_notification_name_9a92b0(database_->name_table, &key_);
            if (!found) {
                auto reason =
                    error(Code::db_lookup_failed, found.error().detail);
                reason.db_error = found.error();
                return fail(reason);
            }
            name_ = *found;
            if (!name_)
                return fail(
                    error(Code::db_lookup_failed,
                          "NULL name row would form invalid address0xC"));
            state_.phase = Phase::name_owner;
            break;
        }
        case Phase::name_owner:
        case Phase::group_owner:
        case Phase::format_owner:
        case Phase::cleanup_owner:
            if (!inputs.globals.owner_10e7eec)
                return report(Status::needs_owner);
            owner_ = *inputs.globals.owner_10e7eec;
            switch (state_.phase) {
            case Phase::name_owner:
                state_.phase = Phase::name_text;
                break;
            case Phase::group_owner:
                state_.phase = Phase::bind_2_group;
                break;
            case Phase::format_owner:
                state_.phase = Phase::format_selector;
                break;
            default:
                state_.phase = Phase::format_cleanup;
                break;
            }
            break;
        case Phase::name_text:
            if (!name_->text_0c)
                return report(Status::needs_name_text);
            name_value_ = *name_->text_0c;
            state_.phase = Phase::bind_1_name;
            break;
        case Phase::bind_1_name:
        case Phase::bind_2_group: {
            const bool name = state_.phase == Phase::bind_1_name;
            const auto& key =
                name ? inputs.keys.key_f16220 : inputs.keys.key_f16214;
            if (!key)
                return report(Status::needs_keys);
            ++state_.binding_calls;
            const auto bound = bind_vmf_notification_text(
                owner_ ? &owner_->parameters_4824 : nullptr, key,
                name ? std::optional{name_value_} : group_value_);
            if (!bound) {
                auto reason =
                    error(Code::binding_failed, "Actual716D30 failed");
                reason.text_error = bound.error();
                return fail(reason);
            }
            const bool inserted =
                bound->status == VmfNotificationTextBindStatus::inserted;
            if (name)
                state_.name_bound = inserted;
            else
                state_.group_bound = inserted;
            state_.phase = name ? Phase::db_lookup_group : Phase::clear_buffer;
            break;
        }
        case Phase::db_lookup_group: {
            if (!inputs.globals.group_table_11f2cdc)
                return report(Status::needs_group_table);
            ++state_.group_calls;
            const auto found = find_vmf_notification_group_9a9340(
                *inputs.globals.group_table_11f2cdc, record_);
            if (!found) {
                auto reason =
                    error(Code::db_lookup_failed, found.error().detail);
                reason.db_error = found.error();
                return fail(reason);
            }
            group_ = *found;
            state_.skipped_null_group = !group_;
            state_.phase = group_ ? Phase::group_text : Phase::clear_buffer;
            break;
        }
        case Phase::group_text:
            if (!group_->text_10)
                return report(Status::needs_group_text);
            group_value_ = *group_->text_10;
            state_.phase = Phase::group_owner;
            break;
        case Phase::clear_buffer:
            body_.fill('\0');
            ++state_.buffer_clears;
            state_.phase = Phase::format_owner;
            break;
        case Phase::format_selector:
            if (!owner_)
                return fail(
                    error(Code::invalid_binding, "Selector15 owner is NULL"));
            owner_->selector_4820 = 15U;
            ++state_.selector_stores;
            state_.phase = Phase::format_high;
            break;
        case Phase::format_high:
            if (!data_.body_high_2c)
                return report(Status::needs_format_id);
            high_ = *data_.body_high_2c;
            state_.phase = Phase::format_low;
            break;
        case Phase::format_low:
            if (!data_.body_low_28)
                return report(Status::needs_format_id);
            low_ = *data_.body_low_28;
            state_.phase = Phase::format_begin;
            break;
        case Phase::format_begin: {
            if (!inputs.formatter)
                return report(Status::needs_formatter);
            formatter_ = inputs.formatter;
            ++state_.formatter_begins;
            const auto begun = formatter_->begin_catalog(
                *owner_, body_, (std::uint64_t{high_} << 32) | low_,
                inputs.arguments);
            if (!begun) {
                auto reason = error(Code::format_failed, begun.error().detail);
                reason.replace_error = begun.error();
                return fail(reason); // A rejected begin never owned the root.
            }
            formatting_ = true;
            state_.phase = Phase::format_run;
            break;
        }
        case Phase::format_run: {
            ++state_.formatter_polls;
            const auto nested = formatter_->run(1);
            if (!nested) {
                auto reason = error(Code::format_failed, nested.error().detail);
                reason.replace_error = nested.error();
                return fail(reason);
            }
            if (nested->status != VmfNotificationWrapperStatus::finished) {
                if (nested->steps) {
                    ++steps;
                    ++state_.steps;
                    if (nested->status ==
                        VmfNotificationWrapperStatus::step_limit)
                        continue;
                }
                return report(Status::formatting_pending);
            }
            formatting_ = false;
            state_.body_formatted = true;
            state_.phase = Phase::cleanup_owner;
            break;
        }
        case Phase::format_cleanup:
            if (!owner_)
                return fail(error(Code::invalid_binding,
                                  "Selector0/clear owner is NULL"));
            owner_->selector_4820 = 0U;
            ++state_.selector_stores;
            clear_vmf_notification_text(owner_->parameters_4824);
            ++state_.parameter_clears;
            state_.text_cleared = true;
            state_.phase = Phase::ui_ring_check;
            break;
        case Phase::ui_ring_check:
            if (!inputs.globals.text_owner_10e7528)
                return report(Status::needs_text_owner);
            text_owner_ = *inputs.globals.text_owner_10e7528;
            state_.ui_skipped = !text_owner_;
            state_.phase = text_owner_ ? Phase::ui_ring_push : Phase::finished;
            break;
        case Phase::ui_ring_push: {
            ++state_.ring_calls;
            const auto pushed = push_vmf_notification_text_single(
                text_owner_->single,
                std::string_view{body_.data(), body_.size()});
            if (!pushed) {
                auto reason = error(Code::ring_failed, "Actual89BBC0 failed");
                reason.ring_error = pushed.error();
                return fail(reason);
            }
            state_.ui_notified = true;
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

VmfNotificationEffectBF80Frame::VmfNotificationEffectBF80Frame(
    const VmfNotificationBF80Inputs& inputs) noexcept
    : inputs_(inputs) {}
VmfNotificationEffectBF80Frame::~VmfNotificationEffectBF80Frame() noexcept {
    release();
}
std::expected<void, VmfNotificationStateError>
VmfNotificationEffectBF80Frame::begin(
    VmfNotificationChildEffectData& data) noexcept {
    if (invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "BF80 frame is leased or active"});
    error_.reset();
    invocation_.emplace(data);
    return {};
}
std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
VmfNotificationEffectBF80Frame::run(std::size_t budget) noexcept {
    if (!invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "BF80 frame is not runnable"});
    Guard guard(running_);
    const auto result = invocation_->run(inputs_, budget);
    if (!result) {
        error_ = result.error();
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::host_failure,
            result.error().detail});
    }
    return result->status == Status::finished
               ? VmfNotificationEffectStatus::finished
               : VmfNotificationEffectStatus::pending;
}
void VmfNotificationEffectBF80Frame::release() noexcept {
    if (running_ || releasing_)
        return;
    Guard guard(releasing_);
    invocation_.reset();
}
std::optional<VmfNotificationEffectBF80Snapshot>
VmfNotificationEffectBF80Frame::snapshot() const noexcept {
    return invocation_ ? std::optional{invocation_->snapshot()} : std::nullopt;
}
std::optional<Error>
VmfNotificationEffectBF80Frame::last_error() const noexcept {
    return error_;
}
} // namespace tdur::ui

#include "tdur/ui/vmf_notification_effect_db30.hpp"

#include <bit>
#include <charconv>
#include <utility>

namespace tdur::ui {
namespace {
using Phase = VmfNotificationDB30Phase;
using Status = VmfNotificationDB30Status;
using Code = VmfNotificationDB30ErrorCode;
using Error = VmfNotificationDB30Error;
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}, {}, {}, {}, {}, {}};
}
bool number(std::array<char, 12>& output, std::uint32_t value) noexcept {
    auto converted = std::to_chars(output.data(), output.data() + 11,
                                   std::bit_cast<std::int32_t>(value));
    if (converted.ec != std::errc{})
        return false;
    *converted.ptr = '\0';
    return true;
}
struct Guard {
    bool& active;
    explicit Guard(bool& flag) noexcept : active(flag) { active = true; }
    ~Guard() { active = false; }
};
} // namespace

VmfNotificationDB30Context::VmfNotificationDB30Context(
    VmfNotificationChildState& child,
    const VmfNotificationChildEffectData& data) noexcept
    : child_(child), data_(data) {
    if (&child != &data.child) {
        state_.faulted = true;
        error_ =
            error(Code::child_identity, "Metadata belongs to another child");
    }
}
VmfNotificationDB30Context::~VmfNotificationDB30Context() noexcept {
    running_ = true;
    cancel_format();
}
void VmfNotificationDB30Context::cancel_format() noexcept {
    if (std::exchange(formatting_, false)) {
        // Serialized exclusive ownership is required. Never leave borrowed
        // buffers in a running foreign formatter during destruction.
        if (!formatter_->cancel())
            std::terminate();
    }
}
VmfNotificationDB30Snapshot
VmfNotificationDB30Context::snapshot() const noexcept {
    auto result = state_;
    result.output_argument = output_;
    return result;
}
std::optional<Error> VmfNotificationDB30Context::last_error() const noexcept {
    return error_;
}
std::expected<VmfNotificationDB30Report, Error>
VmfNotificationDB30Context::run(const VmfNotificationDB30Inputs& inputs,
                                std::size_t budget) noexcept {
    if (running_)
        return std::unexpected(error(Code::invalid_context, "DB30 is running"));
    if (state_.faulted)
        return std::unexpected(*error_);
    Guard guard(running_);
    std::size_t steps{};
    const auto report = [&](Status status) {
        return VmfNotificationDB30Report{status, snapshot(), steps};
    };
    const auto fail = [&](Error reason) {
        cancel_format();
        state_.faulted = true;
        error_ = reason;
        return std::unexpected(reason);
    };
    while (steps < budget && state_.phase != Phase::finished) {
        switch (state_.phase) {
        case Phase::guard:
            if (!inputs.owner_10e7eec)
                return report(Status::needs_owner);
            if (!*inputs.owner_10e7eec) {
                state_.skipped_null_owner = true;
                state_.phase = Phase::finished;
            } else {
                state_.phase = Phase::clock;
            }
            break;
        case Phase::clock: {
            if (!inputs.clock)
                return report(Status::needs_clock);
            if (!*inputs.clock)
                return fail(error(Code::invalid_binding, "Clock is NULL"));
            ++state_.clock_calls;
            const auto value = (*inputs.clock)->get_local_time();
            if (!value) {
                auto reason = error(Code::clock, value.error().detail);
                reason.state_error = value.error();
                return fail(reason);
            }
            state_.datepack = value->datepack();
            state_.phase = Phase::first_owner;
            break;
        }
        case Phase::first_owner:
        case Phase::evaluator_owner:
        case Phase::output_owner:
        case Phase::limit_owner:
        case Phase::title_owner:
        case Phase::body_owner:
        case Phase::reset_owner:
        case Phase::final_owner:
            if (!inputs.owner_10e7eec)
                return report(Status::needs_owner);
            owner_ = *inputs.owner_10e7eec;
            switch (state_.phase) {
            case Phase::first_owner:
                output_ = 0;
                state_.phase = Phase::weight;
                break;
            case Phase::evaluator_owner:
                state_.phase = Phase::evaluate;
                break;
            case Phase::output_owner:
                state_.phase = Phase::output_bind;
                break;
            case Phase::limit_owner:
                state_.phase = Phase::limit_bind;
                break;
            case Phase::title_owner:
                state_.phase = Phase::title_selector;
                break;
            case Phase::body_owner:
                state_.phase = Phase::body_begin;
                break;
            case Phase::reset_owner:
                state_.phase = Phase::reset_selector;
                break;
            case Phase::final_owner:
                state_.phase = Phase::final_clear;
                break;
            default:
                break;
            }
            break;
        case Phase::weight:
            if (!child_.weight_14)
                return report(Status::needs_weight);
            value_ = *child_.weight_14;
            state_.phase = Phase::first_bind;
            break;
        case Phase::limit_read:
            if (!child_.limit_18)
                return report(Status::needs_limit);
            value_ = *child_.limit_18;
            state_.phase = Phase::limit_owner;
            break;
        case Phase::output_read:
            value_ = output_;
            state_.phase = Phase::output_owner;
            break;
        case Phase::first_bind:
        case Phase::result_bind:
        case Phase::output_bind:
        case Phase::limit_bind: {
            const auto index = state_.phase == Phase::first_bind    ? 0U
                               : state_.phase == Phase::result_bind ? 1U
                               : state_.phase == Phase::output_bind ? 2U
                                                                    : 3U;
            if (!inputs.keys[index])
                return report(Status::needs_key);
            if (!number(number_, value_))
                return fail(
                    error(Code::binding, "Signed decimal buffer is too small"));
            ++state_.binding_calls;
            const auto bound = bind_vmf_notification_text(
                owner_ ? &owner_->parameters_4824 : nullptr, inputs.keys[index],
                std::string_view{number_.data()});
            if (!bound) {
                auto reason = error(Code::binding, "Actual716D30 failed");
                reason.binding_error = bound.error();
                return fail(reason);
            }
            state_.phase = index == 0   ? Phase::evaluator_owner
                           : index == 1 ? Phase::output_read
                           : index == 2 ? Phase::limit_read
                                        : Phase::clear_buffers;
            break;
        }
        case Phase::evaluate: {
            if (!inputs.evaluator)
                return report(Status::needs_evaluator);
            if (!*inputs.evaluator)
                return fail(
                    error(Code::invalid_binding, "Child evaluator is NULL"));
            ++state_.evaluator_calls;
            const auto evaluated =
                (*inputs.evaluator)->evaluate(child_, &output_);
            if (!evaluated) {
                auto reason = error(Code::evaluator, evaluated.error().detail);
                reason.state_error = evaluated.error();
                return fail(reason);
            }
            value_ = state_.evaluator_result = *evaluated;
            state_.phase = Phase::result_bind;
            break;
        }
        case Phase::clear_buffers:
            body_.fill('\0');
            title_.fill('\0');
            ++state_.buffer_clears;
            state_.phase = Phase::title_owner;
            break;
        case Phase::title_selector:
        case Phase::reset_selector:
            if (!owner_)
                return fail(
                    error(Code::invalid_binding, "Selector owner is NULL"));
            owner_->selector_4820 =
                state_.phase == Phase::title_selector ? 15U : 0U;
            ++state_.selector_stores;
            state_.phase = state_.phase == Phase::title_selector
                               ? Phase::title_high
                               : Phase::record_init;
            break;
        case Phase::title_high:
        case Phase::body_high: {
            const auto value = state_.phase == Phase::title_high
                                   ? data_.title_high_24
                                   : data_.body_high_2c;
            if (!value)
                return report(Status::needs_id);
            high_ = *value;
            state_.phase = state_.phase == Phase::title_high ? Phase::title_low
                                                             : Phase::body_low;
            break;
        }
        case Phase::title_low:
        case Phase::body_low: {
            const auto value = state_.phase == Phase::title_low
                                   ? data_.title_low_20
                                   : data_.body_low_28;
            if (!value)
                return report(Status::needs_id);
            low_ = *value;
            state_.phase = state_.phase == Phase::title_low ? Phase::title_begin
                                                            : Phase::body_owner;
            break;
        }
        case Phase::title_begin:
        case Phase::body_begin: {
            if (!formatter_) {
                if (!inputs.formatter)
                    return report(Status::needs_formatter);
                formatter_ = *inputs.formatter;
                if (!formatter_)
                    return fail(
                        error(Code::invalid_binding, "Formatter is NULL"));
            }
            if (!owner_)
                return fail(
                    error(Code::invalid_binding, "Catalog owner is NULL"));
            const bool title = state_.phase == Phase::title_begin;
            const auto id = (std::uint64_t{high_} << 32) | low_;
            ++state_.formatter_begins;
            const auto begun = formatter_->begin_catalog(
                *owner_,
                title ? std::span<char>{title_} : std::span<char>{body_}, id,
                inputs.arguments);
            if (!begun) {
                auto reason = error(Code::formatter, begun.error().detail);
                reason.formatter_error = begun.error();
                return fail(reason); // Never cancel another caller's root.
            }
            formatting_ = true;
            state_.phase = title ? Phase::title_run : Phase::body_run;
            break;
        }
        case Phase::title_run:
        case Phase::body_run: {
            ++state_.formatter_polls;
            const auto nested = formatter_->run(1);
            if (!nested) {
                auto reason = error(Code::formatter, nested.error().detail);
                reason.formatter_error = nested.error();
                return fail(reason);
            }
            if (nested->status != VmfNotificationWrapperStatus::finished) {
                if (nested->steps != 0) {
                    ++steps;
                    ++state_.steps;
                    if (nested->status ==
                        VmfNotificationWrapperStatus::step_limit)
                        continue;
                }
                return report(Status::formatting_pending);
            }
            formatting_ = false;
            state_.phase = state_.phase == Phase::title_run
                               ? Phase::body_high
                               : Phase::reset_owner;
            break;
        }
        case Phase::record_init:
            initialize_vmf_notification_record(record_);
            ++state_.record_initializations;
            state_.phase = Phase::record_populate;
            break;
        case Phase::record_populate: {
            if (!language_) {
                if (!inputs.language)
                    return report(Status::needs_language);
                language_ = *inputs.language;
            }
            VmfNotificationRecordArgs args;
            args.field_00 = 4;
            args.field_04 = 1;
            args.field_08 = state_.datepack;
            args.title = std::string_view{title_.data(), title_.size()};
            args.body = std::string_view{body_.data(), body_.size()};
            args.language = 0xffffffffU;
            // F035DF is corroborated empty. This is a known non-NULL C-string,
            // not an inferred missing binding or omitted record argument.
            args.text_96c = std::string_view{""};
            args.text_95c = std::string_view{""};
            ++state_.record_calls;
            const auto populated =
                populate_vmf_notification_record(record_, args, language_);
            if (!populated) {
                auto reason = error(Code::record, populated.error().detail);
                reason.record_error = populated.error();
                return fail(reason);
            }
            state_.phase = Phase::host_query;
            break;
        }
        case Phase::host_query: {
            if (!inputs.host_114c15c)
                return report(Status::needs_host);
            if (!*inputs.host_114c15c)
                return fail(error(Code::invalid_binding, "News host is NULL"));
            ++state_.host_calls;
            const auto selected =
                (*inputs.host_114c15c)->notification_news_owner();
            if (!selected) {
                auto reason = error(Code::queue, selected.error().detail);
                reason.state_error = selected.error();
                return fail(reason);
            }
            news_ = *selected;
            state_.phase = Phase::enqueue;
            break;
        }
        case Phase::enqueue: {
            if (!news_)
                return fail(error(Code::invalid_binding,
                                  "Selected news owner is NULL"));
            ++state_.queue_calls;
            const auto inserted = enqueue_vmf_notification(
                news_->queue, news_->domain, record_, *language_);
            if (!inserted) {
                auto reason = error(Code::queue, inserted.error().detail);
                reason.queue_error = inserted.error();
                return fail(reason);
            }
            state_.phase = Phase::ui_owner;
            break;
        }
        case Phase::ui_owner:
            if (!inputs.text_owner_10e7528)
                return report(Status::needs_text_owner);
            text_owner_ = *inputs.text_owner_10e7528;
            state_.phase = text_owner_ ? Phase::ui_write : Phase::final_owner;
            break;
        case Phase::ui_write: {
            ++state_.ring_calls;
            const auto written = push_vmf_notification_text_double(
                text_owner_->pair,
                std::string_view{title_.data(), title_.size()},
                std::string_view{body_.data(), body_.size()});
            if (!written) {
                auto reason = error(Code::ring, "Actual89BC20 failed");
                reason.ring_error = written.error();
                return fail(reason);
            }
            state_.phase = Phase::final_owner;
            break;
        }
        case Phase::final_clear:
            if (!owner_)
                return fail(error(Code::invalid_binding,
                                  "Final parameter owner is NULL"));
            clear_vmf_notification_text(owner_->parameters_4824);
            ++state_.parameter_clears;
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

VmfNotificationDB30Frame::VmfNotificationDB30Frame(
    const VmfNotificationDB30Inputs& inputs) noexcept
    : inputs_(inputs) {}
VmfNotificationDB30Frame::~VmfNotificationDB30Frame() { release(); }
std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
VmfNotificationDB30Frame::begin(
    VmfNotificationChildState& child,
    const VmfNotificationChildEffectData& data) noexcept {
    if (invocation_ || running_ || releasing_ || &data.child != &child)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "DB30 frame leased/active or child metadata identity mismatch"});
    error_.reset();
    invocation_.emplace(child, data);
    return this;
}
std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
VmfNotificationDB30Frame::run(std::size_t budget) noexcept {
    if (!invocation_ || running_ || releasing_)
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::invalid_context,
            "DB30 frame is not runnable"});
    Guard guard(running_);
    auto result = invocation_->run(inputs_, budget);
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
void VmfNotificationDB30Frame::release() noexcept {
    if (running_ || releasing_)
        return;
    Guard guard(releasing_);
    invocation_.reset();
}
std::optional<VmfNotificationDB30Snapshot>
VmfNotificationDB30Frame::snapshot() const noexcept {
    return invocation_ ? std::optional{invocation_->snapshot()} : std::nullopt;
}
std::optional<Error> VmfNotificationDB30Frame::last_error() const noexcept {
    return error_;
}
} // namespace tdur::ui

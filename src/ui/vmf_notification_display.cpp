#include "tdur/ui/vmf_notification_display.hpp"

#include <algorithm>
#include <utility>

namespace tdur::ui {
namespace {
using Code = VmfNotificationDisplayErrorCode;
using Error = VmfNotificationDisplayError;
using Phase = VmfNotificationDisplayPhase;
using Branch = VmfNotificationDisplayBranch;
using Status = VmfNotificationDisplayStatus;
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}, {}, {}};
}
} // namespace

VmfNotificationDisplayContext::VmfNotificationDisplayContext(
    VmfNotificationWrapperOwner* owner, VmfDisplayRuntime* display,
    std::optional<VmfNotificationFormatSource> name,
    std::optional<VmfTextReference> format,
    const VmfNotificationCallArguments& arguments,
    VmfTargetLookupLimits limits) noexcept
    : owner_(owner), display_(display), name_(std::move(name)),
      format_(std::move(format)), arguments_(arguments), limits_(limits) {}
VmfNotificationDisplayContext::VmfNotificationDisplayContext(
    VmfNotificationWrapperOwner* owner, VmfDisplayRuntime* display,
    std::optional<VmfNotificationFormatSource> name,
    VmfNotificationUnretainedFormat format,
    const VmfNotificationCallArguments& arguments,
    VmfTargetLookupLimits limits) noexcept
    : owner_(owner), display_(display), name_(std::move(name)),
      pending_format_(format), arguments_(arguments), limits_(limits) {}
VmfNotificationDisplayContext::~VmfNotificationDisplayContext() noexcept {
    running_ = true;
    cancel_format();
}
void VmfNotificationDisplayContext::cancel_format() noexcept {
    if (std::exchange(formatting_, false) && !formatter_->cancel())
        std::terminate(); // Serialized exclusive borrower contract.
}
VmfNotificationDisplaySnapshot
VmfNotificationDisplayContext::snapshot() const noexcept {
    return state_;
}
std::string_view VmfNotificationDisplayContext::name_view() const noexcept {
    const auto bytes = name_->bytes();
    return bytes.empty() ? std::string_view{}
                         : std::string_view(bytes.data(), bytes.size());
}
std::expected<VmfNotificationDisplayReport, Error>
VmfNotificationDisplayContext::run(const VmfNotificationDisplayInputs& inputs,
                                   std::size_t step_budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(error(Code::invalid_context,
                                     "Display formatter is active/faulted"));
    running_ = true;
    struct Reset {
        bool& running;
        ~Reset() { running = false; }
    } reset{running_};
    std::size_t executed{};
    const auto report = [&](Status status) {
        return VmfNotificationDisplayReport{status, state_, executed};
    };
    const auto fail = [&](Error reason) {
        cancel_format();
        state_.faulted = true;
        return std::unexpected(reason);
    };
    const auto host_failure = [&](const VmfHostVariableError& cause) {
        auto reason = error(Code::host, cause.detail);
        reason.host_error = cause;
        return fail(reason);
    };
    while (state_.phase != Phase::finished) {
        if (executed == step_budget)
            return report(Status::step_limit);
        switch (state_.phase) {
        case Phase::format_guard:
            if (!pending_format_ && !format_) {
                state_.branch = Branch::skipped_null_format;
                state_.phase = Phase::finished;
            } else {
                state_.phase = Phase::name_guard;
            }
            break;
        case Phase::name_guard: {
            if (!name_)
                return fail(error(Code::invalid_name,
                                  "Needed display name is confirmed NULL"));
            const auto bytes = name_->bytes();
            const auto length = std::min<std::size_t>(bytes.size(), 65);
            const auto end =
                std::find(bytes.begin(), bytes.begin() + length, '\0');
            const auto prefix = static_cast<std::size_t>(end - bytes.begin());
            if (prefix > 64) {
                state_.branch = Branch::skipped_long_name;
                state_.phase = Phase::finished;
            } else {
                state_.phase = pending_format_ ? Phase::format_binding
                                               : Phase::formatter_begin;
            }
            break;
        }
        case Phase::format_binding:
            if (!inputs.format_reference)
                return report(Status::needs_format_reference);
            if (inputs.format_reference->extent_bytes() == 0 ||
                (pending_format_->pointer
                     ? inputs.format_reference->c_str() !=
                           pending_format_->pointer
                     : pending_format_->minimum_extent != 0 ||
                           !inputs.format_reference->text().empty()) ||
                inputs.format_reference->extent_bytes() <
                    pending_format_->minimum_extent)
                return fail(
                    error(Code::invalid_reference,
                          "Format owner differs from captured pointer"));
            format_ = *inputs.format_reference;
            state_.phase = Phase::formatter_begin;
            break;
        case Phase::formatter_begin: {
            if (!inputs.formatter)
                return report(Status::needs_formatter);
            if (*inputs.formatter == nullptr)
                return fail(error(Code::invalid_binding,
                                  "Formatter service is confirmed NULL"));
            const auto source = VmfNotificationFormatSource::retained(*format_);
            if (!source) {
                auto reason =
                    error(Code::invalid_reference, source.error().detail);
                reason.formatter_error = source.error();
                return fail(reason);
            }
            formatter_ = *inputs.formatter;
            ++state_.formatter_begins;
            const auto begun =
                formatter_->begin_plain(owner_, local_, *source, arguments_);
            if (!begun) {
                auto reason = error(Code::formatter, begun.error().detail);
                reason.formatter_error = begun.error();
                return fail(reason);
            }
            formatting_ = true;
            state_.phase = Phase::formatter_run;
            break;
        }
        case Phase::formatter_run: {
            ++state_.formatter_runs;
            const auto formatted = formatter_->run(1);
            if (!formatted) {
                auto reason = error(Code::formatter, formatted.error().detail);
                reason.formatter_error = formatted.error();
                return fail(reason);
            }
            state_.formatting = formatted->state;
            ++executed;
            ++state_.steps;
            if (formatted->status == VmfNotificationWrapperStatus::finished) {
                formatting_ = false;
                if (formatted->state.ordering_gap) {
                    state_.branch = Branch::owned_scan_error;
                    state_.phase = Phase::owned_error;
                } else if (formatted->state.delimiter_met) {
                    state_.branch = Branch::cached;
                    state_.phase = Phase::cache_write;
                } else {
                    state_.branch = Branch::linked_original;
                    state_.phase = Phase::original_link;
                }
            } else if (formatted->status !=
                           VmfNotificationWrapperStatus::step_limit ||
                       formatted->steps == 0) {
                return report(Status::format_pending);
            }
            continue;
        }
        case Phase::owned_error:
        case Phase::original_link: {
            if (!display_)
                return fail(error(Code::invalid_binding,
                                  "Needed display is confirmed NULL"));
            ++state_.host_calls;
            const auto written =
                state_.phase == Phase::owned_error
                    ? write_vmf_host_text(*display_, name_view(), "(ERR:1)",
                                          limits_)
                    : write_vmf_host_linked_text(*display_, name_view(),
                                                 *format_, limits_);
            if (!written)
                return host_failure(written.error());
            state_.phase = Phase::finished;
            break;
        }
        case Phase::cache_write: {
            if (!display_)
                return fail(error(Code::invalid_binding,
                                  "Needed display is confirmed NULL"));
            if (!inputs.cache)
                return report(Status::needs_cache);
            if (*inputs.cache == nullptr)
                return fail(error(Code::invalid_binding,
                                  "Needed format cache is confirmed NULL"));
            cache_ = *inputs.cache;
            ++state_.cache_calls;
            const auto cached =
                cache_->write(*display_, name_view(),
                              std::string_view(local_.data(), local_.size()),
                              inputs.full_cache_fallback, limits_);
            if (!cached) {
                const auto& cause = cached.error();
                if (cause.code ==
                        VmfNotificationFormatErrorCode::invalid_fallback &&
                    !inputs.full_cache_fallback && !cause.slot &&
                    !cause.name_copied && !cause.value_copied &&
                    !cause.host_error) {
                    // The real helper has selected FULL before reading its
                    // fallback. Nothing was written/called yet. Resume that
                    // remaining owned-setter branch without reselecting cache.
                    state_.fallback_selected = true;
                    state_.phase = Phase::full_cache_fallback;
                    break;
                }
                if (cause.code ==
                    VmfNotificationFormatErrorCode::host_variables)
                    ++state_.host_calls;
                auto reason =
                    error(Code::cache, "Format cache rejected publication");
                reason.cache_error = cause;
                return fail(reason);
            }
            state_.cache_result = *cached;
            state_.host_calls += cached->host_call_completed ? 1U : 0U;
            state_.phase = Phase::finished;
            break;
        }
        case Phase::full_cache_fallback: {
            if (!inputs.full_cache_fallback)
                return report(Status::needs_cache_fallback);
            const auto& fallback = *inputs.full_cache_fallback;
            if (fallback.extent_bytes() == 0 || !fallback.c_str())
                return fail(
                    error(Code::invalid_reference,
                          "Full-cache fallback has no retained backing"));
            ++state_.host_calls;
            const auto written = write_vmf_host_text(*display_, name_view(),
                                                     fallback.text(), limits_);
            if (!written)
                return host_failure(written.error());
            state_.cache_result = VmfNotificationFormatReport{
                VmfNotificationFormatBranch::fallback, {}, true};
            state_.phase = Phase::finished;
            break;
        }
        case Phase::finished:
            break;
        }
        ++executed;
        ++state_.steps;
    }
    return report(Status::finished);
}

} // namespace tdur::ui

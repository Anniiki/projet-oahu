#include "tdur/ui/vmf_notification_money.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace tdur::ui {
namespace {
using Error = VmfNotificationReplaceError;
using Code = VmfNotificationReplaceErrorCode;
using Phase = VmfNotificationMoneyPhase;
using Wait = VmfNotificationMoneyWait;
using Status = VmfNotificationNamedStatus;
struct RunGuard {
    bool& flag;
    ~RunGuard() { flag = false; }
};
} // namespace

VmfNotificationMoneyContext::VmfNotificationMoneyContext(
    std::span<char> destination, std::int32_t amount) noexcept
    : destination_(destination) {
    state_.remaining = amount;
}
VmfNotificationMoneySnapshot
VmfNotificationMoneyContext::snapshot() const noexcept {
    return state_;
}
std::expected<void, Error>
VmfNotificationMoneyContext::copy(std::span<const char> source) noexcept {
    for (std::size_t at = 0; at < source.size(); ++at) {
        if (at == destination_.size())
            return std::unexpected(Error{Code::output_range,
                                         "Money copy exceeds destination", at});
        destination_[at] = source[at];
        ++state_.stores;
        if (source[at] == '\0')
            return {};
    }
    return std::unexpected(
        Error{Code::source_range, "Money copy has no bounded NUL"});
}
std::expected<void, Error>
VmfNotificationMoneyContext::prepend(std::string_view prefix) noexcept {
    std::array<char, 32> scratch;
    std::size_t length{};
    for (const char byte : prefix) {
        if (length == scratch.size())
            return std::unexpected(
                Error{Code::output_range, "Money scratch prefix too long"});
        scratch[length++] = byte;
    }
    for (std::size_t at = 0; at < destination_.size(); ++at) {
        if (length == scratch.size())
            return std::unexpected(
                Error{Code::output_range, "Money scratch exceeds32 bytes"});
        const auto byte = destination_[at];
        scratch[length++] = byte;
        if (byte == '\0')
            return copy(std::span(scratch).first(length));
    }
    return std::unexpected(
        Error{Code::source_range, "Money destination lost its NUL"});
}
std::expected<VmfNotificationMoneyReport, Error>
VmfNotificationMoneyContext::run(const VmfNotificationMoneyInputs& inputs,
                                 std::size_t step_budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(
            Error{Code::invalid_context, "Money context is active or faulted"});
    running_ = true;
    const RunGuard guard{running_};
    const auto fail = [&](Error error) {
        state_.faulted = true;
        return std::unexpected(error);
    };
    std::size_t executed{};
    state_.wait = Wait::none;
    const auto report = [&](Status status) {
        return VmfNotificationMoneyReport{status, state_, executed};
    };
    const auto pending = [&](Wait wait) {
        state_.wait = wait;
        return report(Status::needs_inputs);
    };
    while (state_.phase != Phase::finished) {
        if (executed == step_budget)
            return report(Status::step_limit);
        switch (state_.phase) {
        case Phase::initialize:
            if (destination_.empty() ||
                destination_.size() > std::numeric_limits<std::uint32_t>::max())
                return fail(Error{Code::invalid_capacity,
                                  "Money capacity must be a positive DWORD"});
            destination_[0] = '\0';
            ++state_.stores;
            state_.phase = Phase::iteration;
            break;
        case Phase::iteration:
            state_.phase =
                state_.remaining >= 1000 ? Phase::locale : Phase::leading;
            break;
        case Phase::locale:
            if (!locale_) {
                if (!inputs.locale)
                    return pending(Wait::locale);
                if (*inputs.locale == nullptr)
                    return fail(Error{Code::invalid_named_binding,
                                      "Money locale is confirmed NULL"});
                locale_ = *inputs.locale;
            }
            // Delegate the cache guard AND copy/mode capture to one real
            // finalizer tick. No yield/recheck can skip an already selected
            // units pass. A populated cache finishes without unit effects.
            units_.emplace(VmfMenuFinalizerContext::refresh_language(*locale_));
            state_.phase = Phase::units;
            [[fallthrough]];
        case Phase::units: {
            const auto result = units_->run_language(*locale_, inputs.units, 1);
            if (!result)
                return fail(Error{Code::named_failure, result.error().detail});
            ++state_.unit_runs;
            ++executed;
            ++state_.steps;
            if (result->status == VmfMenuFinalizerStatus::language_ready) {
                units_.reset();
                state_.phase = Phase::group;
            } else if (result->status != VmfMenuFinalizerStatus::step_limit ||
                       result->steps == 0) {
                return pending(Wait::units);
            }
            continue;
        }
        case Phase::group: {
            const auto cached = locale_->cached_storage();
            const bool us =
                cached[0] == 'u' && cached[1] == 's' && cached[2] == '\0';
            const auto remainder = state_.remaining % 1000;
            const std::array<char, 4> prefix{
                us ? ',' : ' ', static_cast<char>('0' + remainder / 100),
                static_cast<char>('0' + (remainder / 10) % 10),
                static_cast<char>('0' + remainder % 10)};
            const auto copied = prepend({prefix.data(), prefix.size()});
            if (!copied)
                return fail(copied.error());
            state_.remaining /= 1000;
            ++state_.groups;
            state_.phase = Phase::iteration;
            break;
        }
        case Phase::leading: {
            std::array<char, 12> digits;
            const auto converted = std::to_chars(
                digits.data(), digits.data() + digits.size(), state_.remaining);
            const auto copied = prepend(
                {digits.data(),
                 static_cast<std::size_t>(converted.ptr - digits.data())});
            if (!copied)
                return fail(copied.error());
            state_.phase = Phase::currency;
            break;
        }
        case Phase::currency:
            if (!inputs.currency)
                return pending(Wait::currency);
            currency_ = *inputs.currency;
            if (!currency_) {
                state_.phase = Phase::finished;
                break;
            }
            currency_text_[0] = '\0';
            state_.phase = Phase::currency_begin;
            // Capture owner and begin its distinct invocation before yield.
            // A later global-owner change cannot select another frame owner.
            [[fallthrough]];
        case Phase::currency_begin: {
            const auto result =
                currency_->begin({currency_text_, 0x039d61b2U, destination_});
            if (!result)
                return fail(result.error());
            ++state_.currency_begins;
            state_.phase = Phase::currency_run;
            break;
        }
        case Phase::currency_run: {
            const auto result = currency_->run(1);
            if (!result)
                return fail(result.error());
            if (result->steps > 1)
                return fail(Error{Code::invalid_named_result,
                                  "Currency backend exceeded its budget"});
            ++state_.currency_runs;
            ++executed;
            ++state_.steps;
            if (result->status == Status::finished) {
                // Consume the completed child's bytes before any yield.
                const auto copied = copy(currency_text_);
                if (!copied)
                    return fail(copied.error());
                state_.phase = Phase::finished;
            } else if (result->status == Status::needs_inputs ||
                       (result->status == Status::step_limit &&
                        result->steps == 0)) {
                return pending(Wait::currency);
            } else if (result->status != Status::step_limit) {
                return fail(Error{Code::invalid_named_result,
                                  "Currency backend returned unknown status"});
            }
            continue;
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

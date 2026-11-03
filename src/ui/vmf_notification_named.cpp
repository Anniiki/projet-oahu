#include "tdur/ui/vmf_notification_named.hpp"

#include "tdur/ui/vmf_notification_completion.hpp"

#include <algorithm>
#include <bit>
#include <charconv>

namespace tdur::ui {
namespace {
using Error = VmfNotificationReplaceError;
using Code = VmfNotificationReplaceErrorCode;
using Phase = VmfNotificationNamedPhase;
using Token = VmfNotificationNamedToken;
using Wait = VmfNotificationNamedWait;
using Status = VmfNotificationNamedStatus;
constexpr std::array<std::string_view, 9> prefixes{
    "PLNA",     "NEWS",     "ACHV_PTS", "LEVEL",  "COMPL_PCT",
    "DRV_MODE", "RACE_LUX", "MY_CLUB",  "REW_MAX"};
constexpr std::array<std::uint64_t, 6> level_ids{0x037add73U, 0x038a1fb3U,
                                                 0x03a8a433U, 0x039961f3U,
                                                 0x03b7e673U, 0x039a7b82U};
constexpr std::array<std::uint64_t, 3> driving_ids{0x0341ad22U, 0x0350ef62U,
                                                   0x03b7ee92U};
struct RunGuard {
    bool& flag;
    ~RunGuard() { flag = false; }
};
} // namespace

VmfNotificationNamedResolver::VmfNotificationNamedResolver(
    const VmfNotificationNamedInputs& inputs) noexcept
    : inputs_(inputs) {}
VmfNotificationNamedSnapshot
VmfNotificationNamedResolver::snapshot() const noexcept {
    return state_;
}
std::expected<void, Error> VmfNotificationNamedResolver::begin(
    const VmfNotificationNamedRequest& request) noexcept {
    if (running_ || state_.faulted ||
        (state_.phase != Phase::idle && state_.phase != Phase::finished))
        return std::unexpected(
            Error{Code::invalid_context, "Named resolver is already active"});
    const auto bytes = request.format.bytes();
    if (request.offset > bytes.size())
        return std::unexpected(Error{Code::source_range,
                                     "Named body offset exceeds backing",
                                     request.offset});
    state_ = {};
    initialized_ = 0;
    profile_ = nullptr;
    news_ = nullptr;
    computation_.reset();
    money_.reset();
    bool padding{};
    for (std::size_t at = 0; at < key_.size(); ++at) {
        const char byte = !padding && at < bytes.size() - request.offset
                              ? bytes[request.offset + at]
                              : '\0';
        key_[at] = byte;
        padding = padding || byte == '\0';
    }
    key_.back() = '\0';
    state_.phase = Phase::dispatch;
    return {};
}
std::expected<void, Error>
VmfNotificationNamedResolver::text(std::span<const char> source, bool padded,
                                   bool implied_nul) noexcept {
    bool padding{};
    for (std::size_t at = 0; at < output_.size(); ++at) {
        if (!padding && at >= source.size() && !implied_nul)
            return std::unexpected(Error{Code::source_range,
                                         "Named value has no bounded NUL", at});
        const char byte = !padding && at < source.size() ? source[at] : '\0';
        output_[at] = byte;
        if (byte == '\0') {
            if (!padded) {
                initialized_ = at + 1;
                state_.phase = Phase::finished;
                return {};
            }
            padding = true;
        }
    }
    if (padded) {
        output_.back() = '\0';
        initialized_ = output_.size();
        state_.phase = Phase::finished;
        return {};
    }
    return std::unexpected(
        Error{Code::output_range, "Named sprintf exceeds512 bytes", 512});
}
std::expected<void, Error>
VmfNotificationNamedResolver::literal(std::string_view value) noexcept {
    return text({value.data(), value.size()});
}
std::expected<void, Error>
VmfNotificationNamedResolver::integer(std::int32_t value) noexcept {
    std::array<char, 12> digits;
    const auto result =
        std::to_chars(digits.data(), digits.data() + digits.size(), value);
    return text(std::span(digits).first(
        static_cast<std::size_t>(result.ptr - digits.data())));
}
std::expected<VmfNotificationNamedReport, Error>
VmfNotificationNamedResolver::run(std::size_t step_budget) noexcept {
    if (running_ || state_.faulted || state_.phase == Phase::idle)
        return std::unexpected(Error{Code::invalid_context,
                                     "Named resolver is idle/active/faulted"});
    running_ = true;
    const RunGuard guard{running_};
    const auto fail = [&](Error error) {
        state_.faulted = true;
        return std::unexpected(error);
    };
    std::size_t executed{};
    state_.wait = Wait::none;
    const auto report = [&](Status status) {
        return VmfNotificationNamedReport{
            status, executed,
            status == Status::finished
                ? std::span<const char>(output_).first(initialized_)
                : std::span<const char>{}};
    };
    const auto pending = [&](Wait wait) {
        state_.wait = wait;
        return report(Status::needs_inputs);
    };
    const auto host = [&]() -> std::expected<VmfNotificationNamedHost*, Error> {
        if (*inputs_.host == nullptr)
            return std::unexpected(Error{Code::invalid_named_binding,
                                         "Named host is confirmed NULL"});
        return *inputs_.host;
    };
    while (state_.phase != Phase::finished) {
        if (executed == step_budget)
            return report(Status::step_limit);
        switch (state_.phase) {
        case Phase::dispatch: {
            for (std::size_t at = 0; at < prefixes.size(); ++at) {
                if (std::equal(prefixes[at].begin(), prefixes[at].end(),
                               key_.begin())) {
                    state_.token = static_cast<Token>(at);
                    break;
                }
            }
            switch (state_.token) {
            case Token::player_name:
            case Token::club:
                state_.phase = Phase::profile_query;
                break;
            case Token::news:
                state_.phase = Phase::news_query;
                break;
            case Token::achievement_points:
            case Token::level:
                state_.phase = Phase::state_query;
                break;
            case Token::completion:
                state_.phase = Phase::state_first;
                break;
            case Token::driving_mode:
                state_.phase = Phase::driving_mode;
                break;
            case Token::race_luxury:
                catalog_id_ = 0x03ab6967U;
                catalog_selector_ = 7;
                catalog_fallback_ = "(ERR:RACE_LX)";
                state_.phase = Phase::catalog;
                break;
            case Token::reward:
                state_.phase = Phase::reward;
                break;
            case Token::parameter:
                state_.phase = Phase::parameter;
                break;
            }
            break;
        }
        case Phase::profile_query: {
            if (!inputs_.host)
                return pending(Wait::host);
            const auto selected = host();
            if (!selected)
                return fail(selected.error());
            const auto result = (*selected)->profile();
            ++state_.profile_calls;
            if (!result)
                return fail(Error{Code::named_failure, result.error().detail});
            if (*result == nullptr)
                return fail(Error{Code::invalid_named_binding,
                                  "Profile NULL would be dereferenced"});
            profile_ = *result;
            state_.phase = Phase::profile_text;
            break;
        }
        case Phase::profile_text: {
            const bool player = state_.token == Token::player_name;
            const auto& source = player ? profile_->name_58 : profile_->club_8c;
            if (!source)
                return pending(Wait::profile_text);
            const auto bytes = source->bytes();
            const auto result = !player && (bytes.empty() || bytes[0] == '\0')
                                    ? literal("ERR:NO_CLUB")
                                    : text(bytes, player);
            if (!result)
                return fail(result.error());
            break;
        }
        case Phase::news_query: {
            if (!inputs_.host)
                return pending(Wait::host);
            const auto selected = host();
            if (!selected)
                return fail(selected.error());
            const auto result = (*selected)->news();
            ++state_.news_calls;
            if (!result)
                return fail(Error{Code::named_failure, result.error().detail});
            if (*result == nullptr)
                return fail(Error{Code::invalid_named_binding,
                                  "News NULL would be dereferenced"});
            news_ = *result;
            state_.phase = Phase::news_count;
            break;
        }
        case Phase::news_count: {
            if (!news_->records)
                return pending(Wait::news_records);
            // The native first inactive record returns0 without invoking
            // 895880/898B40; do not require an unused language binding.
            if ((*news_->records)[0].field_04 == 0) {
                const auto written = integer(0);
                if (!written)
                    return fail(written.error());
                break;
            }
            if (!inputs_.language)
                return pending(Wait::language);
            if (*inputs_.language == nullptr)
                return fail(Error{Code::invalid_named_binding,
                                  "News language service is confirmed NULL"});
            const auto count = count_vmf_notification_prefix(
                *news_->records, **inputs_.language);
            if (!count)
                return fail(Error{Code::named_failure, count.error().detail});
            const auto written = integer(std::bit_cast<std::int32_t>(*count));
            if (!written)
                return fail(written.error());
            break;
        }
        case Phase::state_first:
        case Phase::state_query: {
            if (!inputs_.host)
                return pending(Wait::host);
            const auto selected = host();
            if (!selected)
                return fail(selected.error());
            const auto result = (*selected)->notification_state();
            ++state_.state_calls;
            if (!result)
                return fail(Error{Code::named_failure, result.error().detail});
            if (state_.phase == Phase::state_first) {
                // First return value, including NULL, is genuinely ignored.
                state_.phase = Phase::state_query;
                break;
            }
            if (*result == nullptr)
                return fail(Error{Code::invalid_named_binding,
                                  "Notification state NULL is dereferenced"});
            computation_.emplace(
                state_.token == Token::level
                    ? VmfNotificationStateContext::level(**result)
                    : VmfNotificationStateContext::sum(**result));
            state_.phase = Phase::state_run;
            break;
        }
        case Phase::state_run: {
            const auto result = computation_->run(1);
            if (!result)
                return fail(Error{Code::named_failure, result.error().detail});
            ++state_.state_runs;
            ++executed;
            ++state_.steps;
            if (result->status == VmfNotificationStateStatus::finished) {
                state_.state_result = result->state.result;
                if (state_.token == Token::achievement_points) {
                    const auto written = integer(
                        std::bit_cast<std::int32_t>(*state_.state_result));
                    if (!written)
                        return fail(written.error());
                } else if (state_.token == Token::completion) {
                    state_.phase = Phase::completion;
                } else if (*state_.state_result < level_ids.size()) {
                    catalog_id_ = level_ids[*state_.state_result];
                    catalog_selector_ = 0;
                    catalog_fallback_ = "(ERR:LVL)";
                    state_.phase = Phase::catalog;
                } else {
                    const auto written = literal("(ERR:LVL)");
                    if (!written)
                        return fail(written.error());
                }
            } else if (result->status !=
                           VmfNotificationStateStatus::step_limit ||
                       result->steps == 0) {
                return pending(Wait::state);
            }
            continue;
        }
        case Phase::completion: {
            if (!inputs_.x87_control_word)
                return pending(Wait::control_word);
            const auto value = compute_vmf_notification_completion(
                *state_.state_result, *inputs_.x87_control_word);
            if (!value)
                return fail(Error{Code::named_failure,
                                  "Unsupported explicit x87 control word"});
            const auto written = integer(value->integer);
            if (!written)
                return fail(written.error());
            break;
        }
        case Phase::driving_mode: {
            if (!inputs_.driving_mode)
                return pending(Wait::driving_mode);
            const auto mode =
                std::bit_cast<std::int32_t>(*inputs_.driving_mode);
            if (mode < 1 || mode > 3) {
                const auto written = literal("(ERR:NODRVMODE)");
                if (!written)
                    return fail(written.error());
            } else {
                catalog_id_ = driving_ids[static_cast<std::size_t>(mode - 1)];
                catalog_selector_ = 0;
                catalog_fallback_ = "(ERR:NODRVMODE)";
                state_.phase = Phase::catalog;
            }
            break;
        }
        case Phase::catalog: {
            if (!inputs_.catalogs)
                return pending(Wait::catalogs);
            const auto value = select_vmf_catalog_text(
                inputs_.catalogs, catalog_selector_, catalog_id_,
                inputs_.catalog_record_budget);
            ++state_.catalog_calls;
            if (!value)
                return fail(Error{Code::named_failure, value.error().detail});
            const auto written =
                *value ? text((*value)->bytes) : literal(catalog_fallback_);
            if (!written)
                return fail(written.error());
            break;
        }
        case Phase::reward:
            if (!inputs_.reward)
                return pending(Wait::reward);
            money_.emplace(output_,
                           std::bit_cast<std::int32_t>(*inputs_.reward));
            state_.phase = Phase::money;
            break;
        case Phase::money: {
            const auto result = money_->run(inputs_.money, 1);
            if (!result)
                return fail(result.error());
            ++executed;
            ++state_.steps;
            if (result->status == Status::finished) {
                const auto end =
                    std::find(output_.begin(), output_.end(), '\0');
                if (end == output_.end())
                    return fail(Error{Code::invalid_named_result,
                                      "Money output has no bounded NUL"});
                initialized_ =
                    static_cast<std::size_t>(end - output_.begin()) + 1;
                state_.phase = Phase::finished;
            } else if (result->status == Status::needs_inputs ||
                       result->steps == 0) {
                return pending(Wait::money);
            }
            continue;
        }
        case Phase::parameter: {
            if (!inputs_.parameters)
                return pending(Wait::parameters);
            if (*inputs_.parameters == nullptr)
                return fail(Error{Code::invalid_named_binding,
                                  "Named parameters are confirmed NULL"});
            const auto found = find_vmf_notification_text(
                **inputs_.parameters, std::string_view(key_.data()));
            if (!found)
                return fail(Error{Code::named_failure,
                                  "Named parameter lookup rejected its key"});
            const auto written =
                *found ? text((**inputs_.parameters).slots[**found].value,
                              false, false)
                       : literal("(ERR:3)");
            if (!written)
                return fail(written.error());
            break;
        }
        case Phase::idle:
        case Phase::finished:
            break;
        }
        ++executed;
        ++state_.steps;
    }
    return report(Status::finished);
}
} // namespace tdur::ui

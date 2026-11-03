#include "tdur/ui/vmf_tutorial_service.hpp"

#include <limits>
#include <utility>

namespace tdur::ui {
namespace {
VmfTutorialServiceError error(VmfTutorialServiceErrorCode code,
                              std::string_view detail) noexcept {
    return {code, detail, {}, {}, {}};
}
} // namespace

std::expected<VmfTutorialDatabaseSource, VmfTutorialServiceError>
bind_vmf_tutorial_database(const VmfTutorialDatabase& data) noexcept {
    const auto valid = data.text_by_id(0);
    if (!valid)
        return std::unexpected(
            VmfTutorialServiceError{VmfTutorialServiceErrorCode::database,
                                    "Tutorial data is invalid",
                                    valid.error(),
                                    {},
                                    {}});
    if (data.rows().size() >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected(
            error(VmfTutorialServiceErrorCode::resource_limit,
                  "Tutorial count exceeds signed32"));
    return VmfTutorialDatabaseSource{
        static_cast<std::int32_t>(data.rows().size()), &data, &data};
}
VmfTutorialServiceContext::VmfTutorialServiceContext(
    std::optional<VmfTextReference> tag,
    VmfTargetLookupLimits target_limits) noexcept
    : tag_(std::move(tag)), target_limits_(target_limits) {}
VmfTutorialServiceContext VmfTutorialServiceContext::create(
    std::optional<VmfTextReference> tag,
    VmfTargetLookupLimits target_limits) noexcept {
    return VmfTutorialServiceContext{std::move(tag), target_limits};
}
VmfTutorialServiceContext::VmfTutorialServiceContext(
    VmfTutorialServiceContext&& other) noexcept
    : tag_(std::move(other.tag_)), target_limits_(other.target_limits_),
      state_(other.state_), manager_(other.manager_),
      guard_display_(other.guard_display_), table_source_(other.table_source_),
      sentinel_source_(other.sentinel_source_), count_(other.count_),
      rows_(std::move(other.rows_)), message_(std::move(other.message_)),
      valid_(std::exchange(other.valid_, false)) {}
VmfTutorialServiceContext& VmfTutorialServiceContext::operator=(
    VmfTutorialServiceContext&& other) noexcept {
    if (this != &other) {
        tag_ = std::move(other.tag_);
        target_limits_ = other.target_limits_;
        state_ = other.state_;
        manager_ = other.manager_;
        guard_display_ = other.guard_display_;
        table_source_ = other.table_source_;
        sentinel_source_ = other.sentinel_source_;
        count_ = other.count_;
        rows_ = std::move(other.rows_);
        message_ = std::move(other.message_);
        valid_ = std::exchange(other.valid_, false);
    }
    return *this;
}
VmfTutorialServiceSnapshot
VmfTutorialServiceContext::snapshot() const noexcept {
    auto result = state_;
    if (message_)
        result.message = message_->snapshot();
    return result;
}
void VmfTutorialServiceContext::finish(
    VmfTutorialServiceDecision decision) noexcept {
    state_.decision = decision;
    state_.phase = VmfTutorialServicePhase::finished;
}

std::expected<VmfTutorialServiceReport, VmfTutorialServiceError>
VmfTutorialServiceContext::run(const VmfTutorialServiceInputs& inputs,
                               std::size_t step_budget) noexcept {
    if (!valid_ || running_ || state_.faulted)
        return std::unexpected(
            error(VmfTutorialServiceErrorCode::invalid_context,
                  "Tutorial service context is invalid/faulted"));
    struct Guard {
        bool& running;
        explicit Guard(bool& flag) noexcept : running(flag) { running = true; }
        ~Guard() { running = false; }
    } guard(running_);
    std::size_t steps = 0;
    const auto report = [&](VmfTutorialServiceStatus status,
                            std::optional<VmfHelpMessageStatus> nested = {}) {
        return VmfTutorialServiceReport{status, snapshot(), steps, nested};
    };
    const auto fault = [&](VmfTutorialServiceError failure)
        -> std::expected<VmfTutorialServiceReport, VmfTutorialServiceError> {
        state_.faulted = true;
        return std::unexpected(failure);
    };
    using Phase = VmfTutorialServicePhase;
    using Status = VmfTutorialServiceStatus;
    using Decision = VmfTutorialServiceDecision;
    while (state_.phase != Phase::finished && steps < step_budget) {
        switch (state_.phase) {
        case Phase::tag:
            if (!tag_)
                finish(Decision::null_tag);
            else if (tag_->extent_bytes() == 0)
                return fault(
                    error(VmfTutorialServiceErrorCode::invalid_reference,
                          "Tutorial tag lacks an owned reference"));
            else
                state_.phase = Phase::caller;
            break;
        case Phase::caller:
            if (!inputs.caller)
                return report(Status::needs_caller_binding);
            if (!*inputs.caller)
                return fault(
                    error(VmfTutorialServiceErrorCode::invalid_native_binding,
                          "Native tutorial caller would be NULL"));
            if ((**inputs.caller).flag_929 != 0)
                finish(Decision::caller_guard);
            else
                state_.phase = Phase::manager;
            break;
        case Phase::manager:
            if (!inputs.manager)
                return report(Status::needs_manager_binding);
            if (!*inputs.manager)
                return fault(
                    error(VmfTutorialServiceErrorCode::invalid_native_binding,
                          "Native tutorial manager would be NULL"));
            manager_ = *inputs.manager;
            state_.phase = Phase::display;
            break;
        case Phase::display:
            if (!manager_->display)
                return report(Status::needs_display_binding);
            guard_display_ = *manager_->display;
            if (!guard_display_)
                finish(Decision::missing_display);
            else
                state_.phase = Phase::avert;
            break;
        case Phase::avert: {
            const auto found = guard_display_->resolve_target(
                guard_display_->root(), "/avert", target_limits_);
            if (!found)
                return fault({VmfTutorialServiceErrorCode::display,
                              "Tutorial /avert lookup rejected",
                              {},
                              found.error(),
                              {}});
            if (!found->found)
                finish(Decision::missing_avert);
            else
                state_.phase = Phase::active;
            break;
        }
        case Phase::active:
            if (manager_->active != 0)
                finish(Decision::manager_active);
            else
                state_.phase = Phase::global_flag;
            break;
        case Phase::global_flag:
            if (!inputs.flag_1416155)
                return report(Status::needs_global_flag);
            if (*inputs.flag_1416155 != 0)
                finish(Decision::global_guard);
            else
                state_.phase = Phase::database;
            break;
        case Phase::database:
            if (!inputs.database)
                return report(Status::needs_database_binding);
            if (!*inputs.database)
                return fault(
                    error(VmfTutorialServiceErrorCode::invalid_native_binding,
                          "Native tutorial DB container would be NULL"));
            table_source_ = *inputs.database;
            state_.phase = Phase::count;
            break;
        case Phase::count:
            if (!table_source_->row_count)
                return report(Status::needs_row_count);
            count_ = *table_source_->row_count;
            state_.phase = Phase::rows;
            break;
        case Phase::rows: {
            if (!table_source_->rows)
                return report(Status::needs_rows_binding);
            if (!*table_source_->rows) {
                finish(Decision::null_rows);
                break;
            }
            if (count_ <= 0) {
                finish(Decision::nonpositive_count);
                break;
            }
            const auto valid = (**table_source_->rows).text_by_id(0);
            if (!valid)
                return fault({VmfTutorialServiceErrorCode::database,
                              "Tutorial row source invalid",
                              valid.error(),
                              {},
                              {}});
            rows_ = **table_source_->rows;
            state_.phase = Phase::scan;
            break;
        }
        case Phase::scan:
            if (state_.next_row >= static_cast<std::size_t>(count_)) {
                finish(Decision::no_match);
                break;
            }
            if (state_.next_row >= rows_->rows().size())
                return fault(
                    error(VmfTutorialServiceErrorCode::row_out_of_range,
                          "Native count exceeds the owned row storage"));
            ++state_.rows_compared;
            if (rows_->rows()[state_.next_row].key.text() == tag_->text())
                state_.phase = Phase::sentinel_source;
            else
                ++state_.next_row;
            break;
        case Phase::sentinel_source:
            // 7FBFAD reloads the global DB pointer for EACH matching row.
            if (!inputs.database)
                return report(Status::needs_database_binding);
            if (!*inputs.database)
                return fault(
                    error(VmfTutorialServiceErrorCode::invalid_native_binding,
                          "Sentinel lookup DB container would be NULL"));
            sentinel_source_ = *inputs.database;
            state_.phase = Phase::sentinel_catalog;
            break;
        case Phase::sentinel_catalog: {
            if (!inputs.sentinel_id)
                return report(Status::needs_sentinel_id);
            if (!sentinel_source_->catalog)
                return report(Status::needs_catalog_binding);
            const auto sentinel =
                sentinel_source_->catalog->text_by_id(*inputs.sentinel_id);
            if (!sentinel)
                return fault({VmfTutorialServiceErrorCode::database,
                              "Sentinel catalog rejected",
                              sentinel.error(),
                              {},
                              {}});
            ++state_.sentinel_lookups;
            const auto& row = rows_->rows()[state_.next_row];
            if (*sentinel && row.text.c_str() == (**sentinel).c_str()) {
                ++state_.next_row;
                state_.phase = Phase::scan;
            } else {
                state_.phase = Phase::dispatch;
            }
            break;
        }
        case Phase::dispatch: {
            VmfHelpMessageRequest request{};
            request.topic = 11;
            request.text = rows_->rows()[state_.next_row].text;
            request.flag_10 = 1;
            message_.emplace(VmfHelpMessageContext::create(
                *manager_, std::move(request), target_limits_));
            ++state_.message_invocations;
            state_.phase = Phase::message;
            break;
        }
        case Phase::message: {
            const auto nested =
                message_->run(inputs.message, step_budget - steps);
            if (!nested)
                return fault({VmfTutorialServiceErrorCode::message,
                              "Tutorial presentation rejected",
                              {},
                              {},
                              nested.error()});
            steps += nested->steps;
            state_.steps += nested->steps;
            if (nested->status == VmfHelpMessageStatus::message_finished) {
                finish(Decision::message_call_finished);
                return report(Status::service_finished, nested->status);
            }
            return report(nested->status == VmfHelpMessageStatus::step_limit
                              ? Status::step_limit
                              : Status::message_pending,
                          nested->status);
        }
        case Phase::finished:
            break;
        }
        ++steps;
        ++state_.steps;
    }
    return report(state_.phase == Phase::finished ? Status::service_finished
                                                  : Status::step_limit);
}

} // namespace tdur::ui

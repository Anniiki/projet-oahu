#include "tdur/ui/vmf_notification_catalog_display.hpp"

namespace tdur::ui {
namespace {
using Code = VmfNotificationCatalogDisplayErrorCode;
using Error = VmfNotificationCatalogDisplayError;
using Phase = VmfNotificationCatalogDisplayPhase;
using Status = VmfNotificationCatalogDisplayStatus;
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}, {}, {}};
}
} // namespace

VmfNotificationCatalogDisplayContext::VmfNotificationCatalogDisplayContext(
    const VmfNotificationNamedInputs& globals,
    VmfNotificationWrapperOwner* owner, VmfDisplayRuntime* display,
    std::optional<VmfNotificationFormatSource> name, std::uint64_t format_id,
    const VmfNotificationCallArguments& arguments,
    VmfTargetLookupLimits limits) noexcept
    : globals_(globals), owner_(owner), display_(display),
      name_(std::move(name)), format_id_(format_id), arguments_(arguments),
      limits_(limits) {}
VmfNotificationCatalogDisplaySnapshot
VmfNotificationCatalogDisplayContext::snapshot() const noexcept {
    return state_;
}
std::expected<VmfNotificationCatalogDisplayReport, Error>
VmfNotificationCatalogDisplayContext::run(
    const VmfNotificationCatalogDisplayInputs& inputs,
    std::size_t step_budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(
            error(Code::invalid_context, "Catalog display is active/faulted"));
    running_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{running_};
    std::size_t executed{};
    const auto report =
        [&](Status status,
            std::optional<VmfNotificationDisplayStatus> child = {}) {
            return VmfNotificationCatalogDisplayReport{status, state_, executed,
                                                       child};
        };
    const auto fail = [&](Error reason) {
        state_.faulted = true;
        return std::unexpected(reason);
    };
    while (state_.phase != Phase::finished) {
        if (executed == step_budget)
            return report(Status::step_limit);
        switch (state_.phase) {
        case Phase::catalog_binding:
            if (!globals_.catalogs)
                return report(Status::needs_catalogs);
            registry_ = globals_.catalogs;
            state_.phase = Phase::selector;
            break;
        case Phase::selector:
            if (!owner_)
                return fail(error(Code::invalid_owner,
                                  "Catalog owner is confirmed NULL"));
            if (!owner_->selector_4820)
                return report(Status::needs_selector);
            state_.selector = *owner_->selector_4820;
            state_.descriptor_offset =
                vmf_catalog_descriptor_offset(state_.selector);
            state_.phase = Phase::lookup;
            break;
        case Phase::lookup: {
            ++state_.catalog_calls;
            const auto selected =
                select_vmf_catalog_text(registry_, state_.selector, format_id_,
                                        globals_.catalog_record_budget);
            if (!selected) {
                auto reason = error(Code::catalog, selected.error().detail);
                reason.catalog_error = selected.error();
                return fail(reason);
            }
            found_ = *selected;
            state_.format_found = found_.has_value();
            if (found_) {
                state_.row = found_->row;
                state_.pool_offset = found_->pool_offset;
            }
            state_.phase = Phase::display_begin;
            break;
        }
        case Phase::pool_binding: {
            if (!pool_) {
                if (!inputs.pools)
                    return report(Status::needs_pool_binding);
                for (const auto& candidate : *inputs.pools) {
                    if (candidate.descriptor_offset != state_.descriptor_offset)
                        continue;
                    if (pool_)
                        return fail(
                            error(Code::pool_binding,
                                  "Selected pool binding is duplicated"));
                    pool_ = &candidate;
                }
                if (!pool_)
                    return fail(error(Code::pool_binding,
                                      "Selected physical pool has no owner"));
            }
            if (!pool_->pool)
                return report(Status::needs_pool_binding);
            const auto suffix = pool_->pool->suffix(found_->pool_offset);
            if (!suffix) {
                auto reason = error(Code::reference, suffix.error().detail);
                reason.reference_error = suffix.error();
                return fail(reason);
            }
            if ((found_->bytes.data()
                     ? suffix->c_str() != found_->bytes.data()
                     : !found_->bytes.empty() || !suffix->text().empty()) ||
                suffix->extent_bytes() < found_->bytes.size())
                return fail(error(Code::pool_binding,
                                  "Pool owner differs from lookup backing"));
            format_ = *suffix;
            state_.phase = Phase::display_run;
            break;
        }
        case Phase::display_begin:
            if (found_)
                invocation_.emplace(
                    owner_, display_, name_,
                    VmfNotificationUnretainedFormat{found_->bytes.data(),
                                                    found_->bytes.size()},
                    arguments_, limits_);
            else
                invocation_.emplace(owner_, display_, name_,
                                    std::optional<VmfTextReference>{},
                                    arguments_, limits_);
            ++state_.display_begins;
            state_.phase = Phase::display_run;
            break;
        case Phase::display_run: {
            ++state_.display_runs;
            auto child_inputs = inputs.display;
            child_inputs.format_reference = format_;
            const auto displayed = invocation_->run(child_inputs, 1);
            if (!displayed) {
                auto reason = error(Code::display, displayed.error().detail);
                reason.display_error = displayed.error();
                return fail(reason);
            }
            state_.display = displayed->state;
            ++executed;
            ++state_.steps;
            if (displayed->status == VmfNotificationDisplayStatus::finished) {
                state_.phase = Phase::finished;
            } else if (displayed->status ==
                       VmfNotificationDisplayStatus::needs_format_reference) {
                state_.phase = Phase::pool_binding;
            } else if (displayed->status !=
                           VmfNotificationDisplayStatus::step_limit ||
                       displayed->steps == 0) {
                return report(Status::display_pending, displayed->status);
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

#include "tdur/ui/vmf_notification_wrapper.hpp"

#include "tdur/ui/vmf_notification_scan.hpp"

#include <algorithm>
#include <new>
#include <vector>

namespace tdur::ui {
namespace {
using Error = VmfNotificationReplaceError;
using Code = VmfNotificationReplaceErrorCode;
using Phase = VmfNotificationWrapperPhase;
using Status = VmfNotificationWrapperStatus;
using NamedStatus = VmfNotificationNamedStatus;
struct Guard {
    bool& running;
    ~Guard() { running = false; }
};
} // namespace

struct VmfNotificationFormatter::Storage {
    struct Frame;
    struct Currency final : VmfNotificationCurrencyBackend {
        explicit Currency(Frame& parent_frame) noexcept
            : parent(parent_frame) {}
        Frame& parent;
        VmfNotificationWrapperOwner* selected_owner{};
        Frame* child{};
        VmfNotificationCallArguments arguments;
        std::expected<void, Error>
        begin(const VmfNotificationCurrencyRequest& request) noexcept override;
        std::expected<VmfNotificationCurrencyReport, Error>
        run(std::size_t budget) noexcept override;
    };
    struct Named final : VmfNotificationNamedBackend {
        explicit Named(Frame& frame) noexcept : owner(frame) {}
        Frame& owner;
        VmfNotificationNamedInputs bound;
        std::optional<VmfNotificationNamedResolver> resolver;
        void reset() noexcept { resolver.emplace(bound); }
        void refresh() noexcept;
        std::expected<void, Error>
        begin(const VmfNotificationNamedRequest& request) noexcept override {
            return resolver->begin(request);
        }
        std::expected<VmfNotificationNamedReport, Error>
        run(std::size_t budget) noexcept override {
            if (budget == 0)
                return VmfNotificationNamedReport{
                    NamedStatus::step_limit, 0, {}};
            refresh();
            // One real resolver phase per refresh: a getter's global change
            // is visible at the NEXT native global-read site, even within a
            // larger parent's run budget.
            return resolver->run(1);
        }
    };
    struct Frame {
        Frame(Storage& pool, std::size_t position) noexcept
            : storage(pool), depth(position), named(*this), currency(*this) {}
        Storage& storage;
        std::size_t depth;
        VmfNotificationWrapperSnapshot state;
        VmfNotificationWrapperOwner* owner{};
        std::span<char> destination;
        const VmfNotificationCallArguments* arguments{};
        std::optional<VmfNotificationFormatSource> format;
        std::optional<std::span<const VmfCatalogBinding>> registry;
        std::uint64_t format_id{};
        std::uint32_t selector{};
        VmfNotificationBorrowedArguments collected;
        VmfNotificationReplaceInputs replacement_inputs;
        std::optional<VmfNotificationReplaceContext> replacement;
        Named named;
        Currency currency;
        bool initialized{}, active{}, running{};

        void start(VmfNotificationWrapperOwner* selected,
                   std::span<char> output,
                   std::optional<VmfNotificationFormatSource> source,
                   const VmfNotificationCallArguments& values, bool catalog,
                   std::uint64_t id) noexcept {
            owner = selected;
            destination = output;
            format = std::move(source);
            arguments = &values;
            format_id = id;
            registry.reset();
            state = {};
            state.phase =
                catalog ? Phase::catalog_binding : Phase::format_guard;
            collected = {};
            replacement_inputs = {};
            replacement.reset();
            named.reset();
            currency.child = nullptr;
            currency.selected_owner = nullptr;
            initialized = active = true;
            ++storage.active;
            storage.maximum = std::max(storage.maximum, depth + 1);
        }
        VmfNotificationWrapperSnapshot snapshot() const noexcept {
            auto result = state;
            result.active_frames = storage.active;
            result.max_depth_used = storage.maximum;
            if (named.resolver)
                result.named = named.resolver->snapshot();
            return result;
        }
        void finish() noexcept {
            state.phase = Phase::finished;
            if (active) {
                active = false;
                --storage.active;
            }
        }
        void enter_replacement() noexcept {
            replacement.emplace(destination);
            replacement_inputs.format = format;
            replacement_inputs.borrowed_arguments = collected;
            replacement_inputs.named_backend = &named;
            state.phase = Phase::replacement;
        }
        std::expected<VmfNotificationWrapperReport, Error> step() noexcept;
    };

    explicit Storage(VmfNotificationFormatServices& shared) noexcept
        : services(shared) {}
    VmfNotificationFormatServices& services;
    std::vector<std::unique_ptr<Frame>> frames;
    std::size_t active{}, maximum{};
    bool running{};

    bool can_begin() const noexcept {
        const auto& root = *frames[0];
        return !running && (!root.initialized || root.state.faulted ||
                            root.state.phase == Phase::finished);
    }
    void reset_root() noexcept {
        active = maximum = 0;
        for (auto& frame : frames) {
            frame->active = frame->initialized = false;
        }
    }
};

void VmfNotificationFormatter::Storage::Named::refresh() noexcept {
    bound = owner.storage.services.named;
    bound.parameters = owner.owner ? &owner.owner->parameters_4824 : nullptr;
    const auto currency_owner = owner.storage.services.currency_owner;
    if (!currency_owner) {
        bound.money.currency.reset();
    } else if (*currency_owner == nullptr) {
        bound.money.currency = nullptr;
    } else {
        owner.currency.selected_owner = *currency_owner;
        bound.money.currency = &owner.currency;
    }
}
std::expected<void, Error> VmfNotificationFormatter::Storage::Currency::begin(
    const VmfNotificationCurrencyRequest& request) noexcept {
    if (!selected_owner || (child && child->active))
        return std::unexpected(Error{Code::invalid_context,
                                     "Currency invocation has no free frame"});
    const auto next = parent.depth + 1;
    if (next == parent.storage.frames.size())
        return std::unexpected(Error{Code::recursion_limit,
                                     "Currency recursion exceeds frame pool",
                                     next});
    child = parent.storage.frames[next].get();
    if (child->active)
        return std::unexpected(Error{Code::invalid_context,
                                     "Currency child frame is already active"});
    // The typed currency signature supplies exactly one numeric argument.
    // Unused typed slots are known NULL, never native uninitialized DWORDs.
    for (auto& value : arguments)
        value.emplace(std::nullopt);
    arguments[0].emplace(VmfNotificationFormatSource::borrowed(request.number));
    child->start(selected_owner, request.destination, {}, arguments, true,
                 request.format_id);
    return {};
}
std::expected<VmfNotificationCurrencyReport, Error>
VmfNotificationFormatter::Storage::Currency::run(std::size_t budget) noexcept {
    if (!child)
        return std::unexpected(
            Error{Code::invalid_context, "Currency child was not begun"});
    if (budget == 0)
        return VmfNotificationCurrencyReport{NamedStatus::step_limit, 0};
    const auto result = child->step();
    if (!result)
        return std::unexpected(result.error());
    const auto status =
        result->status == Status::finished     ? NamedStatus::finished
        : result->status == Status::step_limit ? NamedStatus::step_limit
                                               : NamedStatus::needs_inputs;
    return VmfNotificationCurrencyReport{status, result->steps};
}

std::expected<VmfNotificationWrapperReport, Error>
VmfNotificationFormatter::Storage::Frame::step() noexcept {
    if (!initialized || running || state.faulted)
        return std::unexpected(Error{Code::invalid_context,
                                     "Wrapper frame is idle/active/faulted"});
    running = true;
    const Guard guard{running};
    const auto report = [&](Status status, std::size_t steps) {
        return VmfNotificationWrapperReport{status, snapshot(), steps};
    };
    const auto fail = [&](Error reason) {
        state.faulted = true;
        return std::unexpected(reason);
    };
    switch (state.phase) {
    case Phase::idle:
        return fail(Error{Code::invalid_context, "Wrapper phase is idle"});
    case Phase::catalog_binding:
        if (!storage.services.named.catalogs)
            return report(Status::needs_catalogs, 0);
        registry = storage.services.named.catalogs;
        state.phase = Phase::selector;
        break;
    case Phase::selector:
        if (!owner)
            return fail(Error{Code::invalid_named_binding,
                              "Catalog format owner is NULL"});
        if (!owner->selector_4820)
            return report(Status::needs_selector, 0);
        selector = *owner->selector_4820;
        state.phase = Phase::catalog;
        break;
    case Phase::catalog: {
        const auto value = select_vmf_catalog_text(
            registry, selector, format_id,
            storage.services.named.catalog_record_budget);
        ++state.catalog_calls;
        if (!value)
            return fail(Error{Code::named_failure, value.error().detail});
        if (*value)
            format = VmfNotificationFormatSource::borrowed((*value)->bytes);
        else
            format.reset();
        state.phase = Phase::format_guard;
        break;
    }
    case Phase::format_guard:
        state.format_found = format.has_value();
        if (!format) {
            state.null_format_noop = true;
            finish();
        } else {
            state.phase = Phase::scan;
        }
        break;
    case Phase::scan: {
        const auto count = scan_vmf_notification_tokens(format->bytes());
        if (!count)
            return fail(Error{Code::source_range,
                              "Wrapper pre-scan rejected format",
                              count.error().offset});
        state.scanned_count = *count;
        if (*count < 0) {
            if (destination.empty() || destination.size() > UINT32_MAX)
                return fail(
                    Error{Code::invalid_capacity,
                          "Ordering-gap copy needs positive DWORD capacity"});
            constexpr char text[] = "(ERR:1)";
            for (std::size_t at = 0; at < destination.size(); ++at)
                destination[at] = at < sizeof(text) ? text[at] : '\0';
            destination.back() = '\0';
            state.ordering_gap = true;
            finish();
        } else if (*count > 9) {
            return fail(Error{Code::invalid_context,
                              "Pre-scan result outside established0..9"});
        } else if (*count == 0) {
            enter_replacement();
        } else {
            state.phase = Phase::gather;
        }
        break;
    }
    case Phase::gather: {
        const auto index = state.arguments_read;
        const auto& input = (*arguments)[index];
        if (!input)
            return report(Status::needs_argument, 0);
        collected[index] = *input;
        ++state.arguments_read;
        if (!*input || state.arguments_read ==
                           static_cast<std::size_t>(*state.scanned_count))
            enter_replacement();
        break;
    }
    case Phase::replacement: {
        const auto result = replacement->run(replacement_inputs, 1);
        if (!result)
            return fail(result.error());
        state.delimiter_met = result->state.delimiter_met;
        state.steps += result->steps;
        if (result->status == VmfNotificationReplaceStatus::finished) {
            finish();
            return report(Status::finished, result->steps);
        }
        return report(result->status == VmfNotificationReplaceStatus::step_limit
                          ? Status::step_limit
                          : Status::dependency_pending,
                      result->steps);
    }
    case Phase::finished:
        return report(Status::finished, 0);
    }
    ++state.steps;
    return report(state.phase == Phase::finished ? Status::finished
                                                 : Status::step_limit,
                  1);
}

VmfNotificationFormatter::VmfNotificationFormatter(
    std::unique_ptr<Storage> state) noexcept
    : storage_(std::move(state)) {}
VmfNotificationFormatter::~VmfNotificationFormatter() = default;
VmfNotificationFormatter::VmfNotificationFormatter(
    VmfNotificationFormatter&&) noexcept = default;
VmfNotificationFormatter& VmfNotificationFormatter::operator=(
    VmfNotificationFormatter&&) noexcept = default;
std::expected<VmfNotificationFormatter, Error>
VmfNotificationFormatter::create(VmfNotificationFormatServices& services,
                                 std::size_t max_depth) noexcept {
    if (max_depth == 0 || max_depth > 32)
        return std::unexpected(
            Error{Code::invalid_context, "Frame depth must be within1..32"});
    try {
        auto storage = std::make_unique<Storage>(services);
        storage->frames.reserve(max_depth);
        for (std::size_t i = 0; i < max_depth; ++i)
            storage->frames.push_back(
                std::make_unique<Storage::Frame>(*storage, i));
        return VmfNotificationFormatter(std::move(storage));
    } catch (const std::bad_alloc&) {
        return std::unexpected(Error{Code::allocation_failure,
                                     "Could not allocate format frame pool"});
    }
}
std::expected<void, Error> VmfNotificationFormatter::begin_plain(
    VmfNotificationWrapperOwner* owner, std::span<char> destination,
    std::optional<VmfNotificationFormatSource> format,
    const VmfNotificationCallArguments& arguments) noexcept {
    if (!storage_ || !storage_->can_begin())
        return std::unexpected(Error{Code::invalid_context,
                                     "Formatter already has an active root"});
    storage_->reset_root();
    storage_->frames[0]->start(owner, destination, std::move(format), arguments,
                               false, 0);
    return {};
}
std::expected<void, Error> VmfNotificationFormatter::begin_catalog(
    VmfNotificationWrapperOwner& owner, std::span<char> destination,
    std::uint64_t format_id,
    const VmfNotificationCallArguments& arguments) noexcept {
    if (!storage_ || !storage_->can_begin())
        return std::unexpected(Error{Code::invalid_context,
                                     "Formatter already has an active root"});
    storage_->reset_root();
    storage_->frames[0]->start(&owner, destination, {}, arguments, true,
                               format_id);
    return {};
}
VmfNotificationWrapperSnapshot
VmfNotificationFormatter::snapshot() const noexcept {
    if (!storage_) {
        VmfNotificationWrapperSnapshot result;
        result.faulted = true;
        return result;
    }
    return storage_->frames[0]->snapshot();
}
std::expected<void, Error> VmfNotificationFormatter::cancel() noexcept {
    if (!storage_ || storage_->running)
        return std::unexpected(Error{
            Code::invalid_context, "Cannot cancel a moved/running formatter"});
    for (auto it = storage_->frames.rbegin(); it != storage_->frames.rend();
         ++it) {
        auto& frame = **it;
        frame.replacement.reset();
        frame.replacement_inputs = {};
        frame.collected = {};
        frame.format.reset();
        frame.registry.reset();
        frame.arguments = nullptr;
        frame.destination = {};
        frame.owner = nullptr;
        frame.named.resolver.reset();
        frame.named.bound = {};
        frame.currency.arguments = {};
        frame.currency.child = nullptr;
        frame.currency.selected_owner = nullptr;
        frame.state = {};
        frame.initialized = frame.active = false;
    }
    storage_->active = storage_->maximum = 0;
    return {};
}
std::optional<VmfNotificationWrapperSnapshot>
VmfNotificationFormatter::frame_snapshot(std::size_t depth) const noexcept {
    if (!storage_ || depth >= storage_->frames.size() ||
        !storage_->frames[depth]->initialized)
        return {};
    return storage_->frames[depth]->snapshot();
}
std::expected<VmfNotificationWrapperReport, Error>
VmfNotificationFormatter::run(std::size_t step_budget) noexcept {
    if (!storage_ || storage_->running || !storage_->frames[0]->initialized)
        return std::unexpected(
            Error{Code::invalid_context, "Formatter is idle/moved/running"});
    storage_->running = true;
    const Guard guard{storage_->running};
    std::size_t executed{};
    const auto report = [&](Status status) {
        return VmfNotificationWrapperReport{status, snapshot(), executed};
    };
    if (storage_->frames[0]->state.faulted)
        return std::unexpected(
            Error{Code::invalid_context, "Formatter root is faulted"});
    if (storage_->frames[0]->state.phase == Phase::finished)
        return report(Status::finished);
    while (executed < step_budget) {
        const auto result = storage_->frames[0]->step();
        if (!result)
            return std::unexpected(result.error());
        executed += result->steps;
        if (result->status != Status::step_limit)
            return report(result->status);
        if (result->steps == 0)
            return std::unexpected(
                Error{Code::invalid_context, "Wrapper step made no progress"});
    }
    return report(Status::step_limit);
}

} // namespace tdur::ui

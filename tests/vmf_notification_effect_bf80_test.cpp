#include "tdur/ui/vmf_notification_dispatch.hpp"
#include "tdur/ui/vmf_notification_effect_bf80.hpp"
#include "vmf_synthetic_movie.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace bf80_allocations {
bool enabled{};
std::size_t calls{};
} // namespace bf80_allocations
void* operator new(std::size_t bytes) {
    if (bf80_allocations::enabled)
        ++bf80_allocations::calls;
    if (auto* result = std::malloc(bytes == 0 ? 1 : bytes))
        return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::ui;
using Context = VmfNotificationEffectBF80Context;
using Phase = VmfNotificationEffectBF80Phase;
using Status = VmfNotificationEffectBF80Status;
std::size_t checks{};
const char* scenario = "regressions";
void check(bool value, const char* detail) {
    ++checks;
    if (!value) {
        std::cerr << "BF80 [" << scenario << "]: " << detail << '\n';
        std::exit(EXIT_FAILURE);
    }
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Required operation failed");
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}
struct Clock : VmfNotificationClock {
    std::size_t calls{};
    bool fail{};
    std::function<void()> callback;
    std::expected<VmfNotificationTime, VmfNotificationStateError>
    get_local_time() noexcept override {
        ++calls;
        if (callback)
            callback();
        if (fail)
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Synthetic clock failure"});
        return VmfNotificationTime{};
    }
};
struct Catalog {
    std::vector<std::byte> rows;
    std::vector<char> pool;
    void add(std::uint64_t id, std::string_view text) {
        const auto offset = static_cast<std::uint32_t>(pool.size());
        pool.insert(pool.end(), text.begin(), text.end());
        pool.push_back('\0');
        const std::array words{static_cast<std::uint32_t>(id),
                               static_cast<std::uint32_t>(id >> 32), offset,
                               0U};
        for (auto word : words)
            for (unsigned shift = 0; shift < 32; shift += 8)
                rows.push_back(std::byte((word >> shift) & 255U));
    }
    VmfCatalogBinding binding(std::uint32_t selector) const {
        return {vmf_catalog_descriptor_offset(selector),
                {static_cast<std::int32_t>(rows.size() / 16), rows, pool}};
    }
};
// Only clock, database/loader metadata, and catalog backing are fixtures.
// Lookups, bindings, formatter, rings, child/dispatch frames are actual code.
struct Harness {
    static constexpr std::uint64_t format_id = 0x0000000200000100ULL;
    Clock clock;
    VmfNotificationChildState child;
    VmfNotificationChildEffectData data{child};
    VmfNotificationWrapperOwner owner, second, third;
    std::array<DatabaseRecord, 2> records{DatabaseRecord{42U},
                                          DatabaseRecord{99U}};
    std::array<DatabaseNameRow, 2> names{DatabaseNameRow{42U, "Driver_A"},
                                         DatabaseNameRow{99U, "Driver_B"}};
    std::array<DatabaseGroupRow, 1> groups{};
    VmfNotificationDatabaseOwner11EE900 database{
        {0x1000U, 2U, std::span{records}}, {0x2000U, 2U, std::span{names}}};
    DatabaseGroupTable group_table{0x3000U, 1U, std::span{groups}};
    VmfNotificationTextOwner text, other_text;
    Catalog catalog, alternative;
    std::array<VmfCatalogBinding, 3> registry{};
    VmfNotificationNamedInputs named;
    VmfNotificationFormatServices services{named};
    VmfNotificationFormatter formatter =
        take(VmfNotificationFormatter::create(services));
    VmfNotificationBF80Inputs inputs;
    Harness() {
        data.key_38 = 42U;
        data.body_low_28 = 0x100U;
        data.body_high_2c = 2U;
        groups[0].references_18_88.fill(&records[1]);
        groups[0].references_18_88[0] = &records[0];
        groups[0].text_10 = std::string_view{"Club_A"};
        catalog.add(format_id, "Driver \xc2\xa7NAME\xc2\xa7 / Club "
                               "\xc2\xa7GROUP\xc2\xa7");
        alternative.add(format_id, "Selected7 old low");
        alternative.add(format_id + 1, "Selected7 live low");
        alternative.add(format_id + 0x100000001ULL, "Wrong high");
        registry = {catalog.binding(15), alternative.binding(7),
                    catalog.binding(0)};
        named.catalogs = registry;
        inputs.globals.owner_10e7eec = &owner;
        inputs.globals.text_owner_10e7528 = &text;
        inputs.globals.db_owner_11ee900 = &database;
        inputs.globals.group_table_11f2cdc = &group_table;
        inputs.keys = {"NAME", "GROUP"};
        inputs.clock = &clock;
        inputs.formatter = &formatter;
        for (auto& argument : inputs.arguments)
            argument.emplace(std::nullopt);
    }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
    Harness(Harness&&) = delete;
    Harness& operator=(Harness&&) = delete;
};
void to_phase(Context& context, const VmfNotificationBF80Inputs& inputs,
              Phase target) {
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        if (context.snapshot().phase == target)
            return;
        const auto result = take(context.run(inputs, 1));
        check(result.steps == 1, "A progressing run1 consumes one local step");
        check(result.status != Status::finished,
              "Target phase precedes finish");
    }
    check(false, "Phase bound exhausted");
}
VmfNotificationEffectBF80Report finish(Context& context,
                                       const VmfNotificationBF80Inputs& inputs,
                                       std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        const auto before = context.snapshot().steps;
        const auto result = take(context.run(inputs, budget));
        check(result.steps <= budget, "Report uses bounded local step count");
        check(result.state.steps == before + result.steps,
              "Snapshot holds cumulative completed progress");
        if (result.status == Status::finished)
            return result;
        check(result.steps != 0 &&
                  (result.status == Status::step_limit ||
                   result.status == Status::formatting_pending),
              "Fully supplied invocation progresses until finished");
    }
    check(false, "Completion bound exhausted");
    std::exit(EXIT_FAILURE);
}
std::string_view slot(const VmfNotificationWrapperOwner& owner,
                      std::size_t index = 0) {
    return owner.parameters_4824.slots[index].value.data();
}
bool empty(const VmfNotificationWrapperOwner& owner) {
    return std::all_of(owner.parameters_4824.slots.begin(),
                       owner.parameters_4824.slots.end(),
                       [](const auto& value) {
                           return value.key[0] == 0 && value.value[0] == 0;
                       });
}
void clock_failure_is_terminal() {
    Clock clock;
    clock.fail = true;
    VmfNotificationChildState child;
    VmfNotificationChildEffectData data{child};
    VmfNotificationWrapperOwner owner;
    VmfNotificationBF80Inputs inputs;
    inputs.globals.owner_10e7eec = &owner;
    inputs.clock = &clock;
    VmfNotificationEffectBF80Context context{data};
    const auto first = context.run(inputs);
    check(!first && clock.calls == 1, "First clock error is reported");
    const auto again = context.run(inputs);
    check(!again && clock.calls == 1,
          "A faulted invocation must not repeat the clock call");
}
void selector_precedes_unknown_id() {
    Clock clock;
    VmfNotificationChildState child;
    VmfNotificationChildEffectData data{child};
    data.key_38 = 0U;
    VmfNotificationWrapperOwner owner;
    owner.selector_4820 = 3U;
    VmfNotificationBF80Inputs inputs;
    inputs.globals.owner_10e7eec = &owner;
    inputs.clock = &clock;
    VmfNotificationEffectBF80Context context{data};
    const auto wait = context.run(inputs);
    check(wait.has_value(), "Unknown ID must request data");
    check(owner.selector_4820 == 15U,
          "Selector15 is stored before waiting for IDs or the formatter");
}

void initial_guards_and_reentry() {
    Harness h;
    h.inputs.globals.owner_10e7eec.reset();
    Context context(h.data);
    const auto zero = take(context.run(h.inputs, 0));
    check(zero.status == Status::step_limit && zero.steps == 0 &&
              context.snapshot().steps == 0 && h.clock.calls == 0,
          "Budget0 preserves a pristine invocation");
    const auto unbound = take(context.run(h.inputs));
    check(unbound.status == Status::needs_owner && h.clock.calls == 0 &&
              context.snapshot().record_calls == 0,
          "Unknown initial owner waits before every dependency");
    const auto wait = take(context.run(h.inputs));
    check(wait.steps == 0 && wait.state.steps == unbound.state.steps,
          "A repeated wait reports zero local progress");
    h.inputs.globals.owner_10e7eec = nullptr;
    h.inputs.clock = nullptr;
    h.inputs.globals.text_owner_10e7528.reset();
    h.inputs.globals.db_owner_11ee900.reset();
    h.inputs.globals.group_table_11f2cdc.reset();
    h.data.key_38.reset();
    const auto done = take(context.run(h.inputs));
    check(done.status == Status::finished && done.state.skipped_null_owner &&
              done.state.clock_calls == 0 && done.state.buffer_clears == 0,
          "Known NULL initial owner ignores clock, key and tables");
    const auto again = take(context.run(h.inputs, 0));
    check(again.status == Status::finished && again.steps == 0,
          "Finished remains finished with budget0");

    Harness reentrant;
    Context outer(reentrant.data);
    bool rejected{};
    reentrant.clock.callback = [&] {
        const auto recursive = outer.run(reentrant.inputs);
        rejected = !recursive &&
                   recursive.error().code ==
                       VmfNotificationEffectBF80ErrorCode::invalid_context;
        outer.cancel_active_formatter();
        check(!outer.snapshot().faulted,
              "Reentrant abandonment cannot damage the outer invocation");
    };
    finish(outer, reentrant.inputs);
    check(rejected && reentrant.clock.calls == 1 &&
              outer.snapshot().ring_calls == 1,
          "Clock callback cannot reenter run or duplicate its effects");
    reentrant.clock.callback = {};
}

void full_path_and_budgets() {
    for (const std::size_t budget :
         {std::size_t{1}, std::size_t{3}, std::size_t{4096}}) {
        Harness h;
        Context context(h.data);
        bf80_allocations::calls = 0;
        bf80_allocations::enabled = true;
        const auto report = finish(context, h.inputs, budget);
        bf80_allocations::enabled = false;
        const auto state = report.state;
        check(state.clock_calls == 1 && state.record_calls == 1 &&
                  state.name_calls == 1 && state.group_calls == 1 &&
                  state.binding_calls == 2 && state.buffer_clears == 1 &&
                  state.selector_stores == 2 && state.formatter_begins == 1 &&
                  state.parameter_clears == 1 && state.ring_calls == 1,
              "Every actual helper executes once across budgeted resumes");
        check(state.name_bound && state.group_bound && state.body_formatted &&
                  state.text_cleared && state.ui_notified &&
                  !state.ui_skipped && !state.faulted,
              "Full notification records actual completion effects");
        check(
            std::string_view(context.body().data()) ==
                    "Driver Driver_A / Club Club_A" &&
                std::string_view(h.text.single.slots[0].data()) ==
                    "Driver Driver_A / Club Club_A" &&
                h.text.single.index == 1 && h.text.pair.index == 0,
            "Real named bindings/catalog formatter feed only the single ring");
        check(empty(h.owner) && h.owner.selector_4820 == 0U &&
                  h.formatter.snapshot().active_frames == 0,
              "Final reset/clear and formatter completion precede publication");
        check(bf80_allocations::calls == 0,
              "Prepared full path has no observed ordinary new calls");
        const auto again = take(context.run(h.inputs));
        check(again.status == Status::finished && again.steps == 0 &&
                  h.text.single.index == 1 && h.clock.calls == 1,
              "Finished run does not replay publication");
    }
}

void skipped_tables_and_known_nulls() {
    Harness zero;
    zero.data.key_38 = 0U;
    zero.inputs.globals.db_owner_11ee900.reset();
    zero.inputs.globals.group_table_11f2cdc.reset();
    zero.inputs.keys = {};
    zero.inputs.formatter = nullptr;
    Context skipped(zero.data);
    const auto ready = take(skipped.run(zero.inputs));
    check(ready.status == Status::needs_formatter &&
              ready.state.skipped_zero_key && ready.state.record_calls == 0 &&
              ready.state.binding_calls == 0 && zero.owner.selector_4820 == 15U,
          "Zero key skips all DB and literal-key bindings");

    Harness miss;
    miss.database.record_table.count = 0U;
    miss.database.record_table.rows.reset();
    miss.database.name_table = {};
    miss.inputs.globals.group_table_11f2cdc.reset();
    miss.inputs.keys = {};
    miss.inputs.formatter = nullptr;
    Context missing(miss.data);
    const auto result = take(missing.run(miss.inputs));
    check(result.status == Status::needs_formatter &&
              result.state.skipped_null_record &&
              result.state.record_calls == 1 && result.state.name_calls == 0 &&
              result.state.group_calls == 0,
          "Record miss ignores unavailable name/group tables and keys");

    Harness unknown;
    unknown.inputs.globals.db_owner_11ee900.reset();
    Context database(unknown.data);
    const auto need = take(database.run(unknown.inputs));
    check(need.status == Status::needs_db_owner &&
              need.state.captured_key_38 == 42U,
          "Unknown DB binding waits after the key capture");
    unknown.data.key_38 = 0U;
    unknown.inputs.globals.db_owner_11ee900 = nullptr;
    const auto invalid = database.run(unknown.inputs);
    check(!invalid &&
              invalid.error().code ==
                  VmfNotificationEffectBF80ErrorCode::invalid_binding &&
              database.snapshot().captured_key_38 == 42U,
          "Known NULL DB object is an error, never zero-key fallback or wait");

    Harness absent;
    absent.database.name_table.count = 0U;
    Context invalid_name(absent.data);
    const auto error = invalid_name.run(absent.inputs);
    check(!error &&
              error.error().code ==
                  VmfNotificationEffectBF80ErrorCode::db_lookup_failed &&
              invalid_name.snapshot().name_calls == 1 &&
              invalid_name.snapshot().binding_calls == 0,
          "NULL name row is invalid native0xC, not an empty text success");
    check(!invalid_name.run(absent.inputs) && absent.clock.calls == 1 &&
              invalid_name.snapshot().name_calls == 1,
          "Lookup failure is terminal and preserves call counts");

    Harness null_group;
    null_group.inputs.globals.group_table_11f2cdc = nullptr;
    Context invalid_group(null_group.data);
    const auto fault = invalid_group.run(null_group.inputs);
    check(!fault && fault.error().db_error &&
              fault.error().db_error->code ==
                  VmfNotificationDatabaseErrorCode::invalid_binding &&
              invalid_group.snapshot().name_bound &&
              slot(null_group.owner) == "Driver_A" &&
              invalid_group.snapshot().buffer_clears == 0,
          "Invalid group global retains already-bound name without rollback");
}

void key_record_and_name_captures() {
    Harness h;
    Context context(h.data);
    to_phase(context, h.inputs, Phase::db_lookup_name);
    h.data.key_38 = 99U;
    h.database.record_table.rows.reset();
    h.inputs.globals.db_owner_11ee900.reset();
    h.records[0].key_1f0 = 99U;
    h.groups[0].references_18_88.fill(&h.records[1]);
    finish(context, h.inputs, 1);
    check(std::string_view(context.body().data()) ==
                  "Driver Driver_A / Club Club_A" &&
              context.snapshot().captured_key_38 == 42U &&
              context.snapshot().record_calls == 1 &&
              context.snapshot().group_bound,
          "Name uses captured key/fixed DB; group uses same record's live key");

    Harness capture;
    capture.names[0].text_0c.reset();
    Context named(capture.data);
    const auto text_wait = take(named.run(capture.inputs));
    check(text_wait.status == Status::needs_name_text &&
              text_wait.state.name_calls == 1 &&
              text_wait.state.binding_calls == 0,
          "Name owner is captured before inline text wait");
    capture.inputs.globals.owner_10e7eec = &capture.second;
    capture.inputs.globals.db_owner_11ee900.reset();
    capture.names[0].text_0c = "Driver_A";
    capture.inputs.keys.key_f16220.reset();
    const auto key_wait = take(named.run(capture.inputs));
    check(key_wait.status == Status::needs_keys && key_wait.steps != 0,
          "Name text is captured before its key wait");
    capture.names[0].text_0c = "Rebound_elsewhere";
    capture.inputs.keys.key_f16220 = "NAME";
    const auto bound = take(named.run(capture.inputs, 1));
    check(bound.state.name_bound && slot(capture.owner) == "Driver_A" &&
              empty(capture.second) && bound.state.name_calls == 1,
          "Waits retain owner and inline backing without redoing lookup");
}

void group_text_and_owner_captures() {
    Harness h;
    Context context(h.data);
    to_phase(context, h.inputs, Phase::group_text);
    take(context.run(h.inputs, 1));
    h.groups[0].text_10 = std::string_view{"Changed_binding"};
    h.inputs.globals.owner_10e7eec.reset();
    const auto wait = take(context.run(h.inputs));
    check(wait.status == Status::needs_owner && wait.state.binding_calls == 1,
          "Group text is captured before its owner wait");
    h.inputs.globals.owner_10e7eec = &h.second;
    take(context.run(h.inputs, 1));
    h.inputs.globals.owner_10e7eec = &h.third;
    h.inputs.keys.key_f16214.reset();
    const auto key_wait = take(context.run(h.inputs));
    check(key_wait.status == Status::needs_keys,
          "Captured group owner survives a missing literal key");
    h.groups[0].text_10.reset();
    h.inputs.keys.key_f16214 = "GROUP";
    take(context.run(h.inputs, 1));
    check(context.snapshot().group_bound && slot(h.second) == "Club_A" &&
              empty(h.third) && slot(h.owner) == "Driver_A",
          "Group binder uses captured text and owner, with live backing");

    Harness null;
    null.groups[0].text_10.reset();
    Context no_value(null.data);
    const auto unknown = take(no_value.run(null.inputs));
    check(unknown.status == Status::needs_group_text &&
              unknown.state.group_calls == 1 &&
              unknown.state.binding_calls == 1,
          "Unknown group pointer text is not known NULL");
    null.groups[0].text_10.emplace(); // outer known, inner NULL
    to_phase(no_value, null.inputs, Phase::group_owner);
    null.inputs.globals.owner_10e7eec = nullptr;
    take(no_value.run(null.inputs, 1));
    take(no_value.run(null.inputs, 1));
    check(!no_value.snapshot().faulted && !no_value.snapshot().group_bound &&
              no_value.snapshot().binding_calls == 2 &&
              slot(null.owner) == "Driver_A",
          "Real716D30 ignores NULL value even when its captured owner is NULL");
    null.inputs.globals.owner_10e7eec = &null.owner;
    null.inputs.formatter = nullptr;
    const auto after = take(no_value.run(null.inputs));
    check(after.status == Status::needs_formatter &&
              after.state.group_calls == 1 && after.state.buffer_clears == 1,
          "NULL value does not requery group or replay its binding");
}

void format_capture_order_and_late_cleanup() {
    Harness h;
    h.data.key_38 = 0U;
    h.data.body_high_2c.reset();
    h.data.body_low_28.reset();
    h.inputs.formatter = nullptr;
    take(bind_vmf_notification_text(&h.owner.parameters_4824, "KEEP", "keep"));
    take(bind_vmf_notification_text(&h.second.parameters_4824, "LATE", "late"));
    Context context(h.data);
    const auto ids = take(context.run(h.inputs));
    check(ids.status == Status::needs_format_id &&
              h.owner.selector_4820 == 15U && ids.state.buffer_clears == 1 &&
              ids.state.selector_stores == 1 &&
              std::all_of(context.body().begin(), context.body().end(),
                          [](char value) { return value == 0; }),
          "Clear and selector15 precede all ID/service waits");
    h.owner.selector_4820 = 7U;
    h.data.body_high_2c = 2U;
    const auto low = take(context.run(h.inputs));
    check(low.status == Status::needs_format_id && h.owner.selector_4820 == 7U,
          "Waiting for low ID never repeats selector15");
    h.data.body_high_2c = 3U;
    h.data.body_low_28 = 0x101U;
    h.inputs.globals.owner_10e7eec = &h.second;
    const auto service = take(context.run(h.inputs));
    check(service.status == Status::needs_formatter &&
              service.state.buffer_clears == 1 &&
              service.state.selector_stores == 1,
          "Formatter wait keeps earlier owner/high and newly-read low ID");
    h.inputs.formatter = &h.formatter;
    to_phase(context, h.inputs, Phase::format_run);
    h.inputs.formatter = nullptr;
    to_phase(context, h.inputs, Phase::cleanup_owner);
    check(std::string_view(context.body().data()) == "Selected7 live low" &&
              h.owner.selector_4820 == 7U && h.second.selector_4820 != 15U,
          "Formatter retains actual begin owner/selector/IDs and selected "
          "service");
    take(context.run(h.inputs, 1));
    h.inputs.globals.owner_10e7eec = &h.third;
    finish(context, h.inputs, 1);
    check(h.second.selector_4820 == 0U && empty(h.second) &&
              slot(h.owner) == "keep" && h.owner.selector_4820 == 7U &&
              !h.third.selector_4820 && context.snapshot().selector_stores == 2,
          "Late cleanup uses reloaded captured owner, not format or newest "
          "owner");
    check(h.second.parameters_4824.slots[0].value[1] == 'a',
          "Real clear only resets first bytes, retaining value tails");
}

void cleanup_and_ring_boundaries() {
    Harness null_owner;
    Context invalid(null_owner.data);
    to_phase(invalid, null_owner.inputs, Phase::cleanup_owner);
    null_owner.inputs.globals.owner_10e7eec = nullptr;
    const auto failure = invalid.run(null_owner.inputs);
    check(!failure &&
              failure.error().code ==
                  VmfNotificationEffectBF80ErrorCode::invalid_binding &&
              invalid.snapshot().body_formatted &&
              invalid.snapshot().parameter_clears == 0 &&
              invalid.snapshot().ring_calls == 0 &&
              null_owner.owner.selector_4820 == 15U && !empty(null_owner.owner),
          "NULL cleanup owner faults after formatting without erasing prefix");
    check(!invalid.run(null_owner.inputs) && null_owner.clock.calls == 1,
          "Cleanup fault cannot replay formatter or clock");

    Harness h;
    Context context(h.data);
    to_phase(context, h.inputs, Phase::ui_ring_check);
    h.inputs.globals.text_owner_10e7528.reset();
    const auto wait = take(context.run(h.inputs));
    check(wait.status == Status::needs_text_owner && wait.state.text_cleared &&
              wait.state.ring_calls == 0 && h.text.single.index == 0,
          "Unknown ring owner waits after one reset and clear");
    h.inputs.globals.text_owner_10e7528 = &h.text;
    take(context.run(h.inputs, 1));
    h.inputs.globals.text_owner_10e7528 = &h.other_text;
    const auto published = finish(context, h.inputs, 1);
    check(
        published.state.ui_notified && h.text.single.index == 1 &&
            h.other_text.single.index == 0,
        "Captured ring owner receives index0 success despite input rebinding");

    Harness skip;
    skip.inputs.globals.text_owner_10e7528 = nullptr;
    skip.text.single.index = -1;
    Context skipped(skip.data);
    const auto done = finish(skipped, skip.inputs);
    check(done.state.ui_skipped && !done.state.ui_notified &&
              done.state.ring_calls == 0 && skip.text.single.index == -1,
          "Known NULL ring owner skips an unusable ring rather than requiring "
          "it");

    Harness bad;
    bad.text.single.index = 8;
    Context failed(bad.data);
    const auto ring_error = failed.run(bad.inputs);
    check(!ring_error &&
              ring_error.error().ring_error ==
                  VmfNotificationRingError::invalid_index &&
              failed.snapshot().text_cleared &&
              failed.snapshot().selector_stores == 2 &&
              failed.snapshot().ring_calls == 1 && bad.text.single.index == 8,
          "Ring error retains preceding selector/clear effects");
    check(!failed.run(bad.inputs) && failed.snapshot().ring_calls == 1,
          "Ring error is terminal without a second publication attempt");

    Harness repeated;
    for (unsigned index = 0; index < 10; ++index) {
        Context next(repeated.data);
        finish(next, repeated.inputs, 1);
        check(repeated.text.single.index ==
                  static_cast<std::int32_t>((index + 1) % 8),
              "Successive real effects wrap the eight-slot index");
    }
}

void cancellation_errors_and_foreign_leases() {
    Harness h;
    h.named.catalogs.reset();
    {
        Context context(h.data);
        const auto pending = take(context.run(h.inputs));
        check(pending.status == Status::formatting_pending &&
                  h.formatter.snapshot().active_frames == 1 &&
                  pending.state.formatter_begins == 1,
              "Actual formatter remains borrowed on a real catalog wait");
    }
    check(h.formatter.snapshot().active_frames == 0 && !h.formatter.run(1) &&
              h.owner.selector_4820 == 15U && !empty(h.owner),
          "Destruction drops own lease before buffers die, retaining prefix");
    h.named.catalogs = h.registry;
    {
        Context context(h.data);
        finish(context, h.inputs);
        check(context.snapshot().ring_calls == 1,
              "Same formatter can publish a real effect after abandonment");
    }

    Harness foreign;
    std::array<char, 32> external{};
    constexpr char literal[] = "Foreign invocation";
    take(foreign.formatter.begin_plain(
        nullptr, external,
        VmfNotificationFormatSource::borrowed(std::span{literal}),
        foreign.inputs.arguments));
    {
        Context contender(foreign.data);
        const auto refused = contender.run(foreign.inputs);
        check(!refused && refused.error().replace_error &&
                  foreign.formatter.snapshot().active_frames == 1,
              "A rejected begin never acquired or cancelled a foreign root");
    }
    check(foreign.formatter.snapshot().active_frames == 1,
          "Contender destruction does not cancel the foreign root");
    const auto completed = take(foreign.formatter.run());
    check(completed.status == VmfNotificationWrapperStatus::finished &&
              std::string_view(external.data()) == literal,
          "Foreign invocation still runs with its real destination");

    Harness error;
    Context failed(error.data);
    to_phase(failed, error.inputs, Phase::format_run);
    error.registry[0].table.records.reset();
    const auto result = failed.run(error.inputs);
    check(!result && result.error().replace_error &&
              error.formatter.snapshot().active_frames == 0 &&
              error.owner.selector_4820 == 15U && !empty(error.owner) &&
              failed.snapshot().parameter_clears == 0,
          "Formatter failure cancels its own lease without rolling back "
          "bindings");
    check(!failed.run(error.inputs) && failed.snapshot().formatter_begins == 1,
          "Formatter error does not replay begin");
    error.registry[0] = error.catalog.binding(15);
    Context reuse(error.data);
    finish(reuse, error.inputs);
    check(reuse.snapshot().ring_calls == 1,
          "A new effect can reuse the formatter after a terminal formatter "
          "error");

    Harness abandoned;
    abandoned.named.catalogs.reset();
    Context explicit_cancel(abandoned.data);
    take(explicit_cancel.run(abandoned.inputs));
    explicit_cancel.cancel_active_formatter();
    check(explicit_cancel.snapshot().faulted &&
              abandoned.formatter.snapshot().active_frames == 0 &&
              !explicit_cancel.run(abandoned.inputs),
          "Explicit abandonment is terminal rather than resuming an idle "
          "formatter");
}

void frame_lifecycle_and_callback_guards() {
    Harness h;
    h.named.catalogs.reset();
    VmfNotificationEffectBF80Frame frame(h.inputs);
    check(!frame.run(1), "An unleased frame refuses run");
    take(frame.begin(h.data));
    check(!frame.begin(h.data) &&
              take(frame.run(0)) == VmfNotificationEffectStatus::pending &&
              h.clock.calls == 0,
          "Exclusive lease and budget0 preserve pristine state");
    h.clock.callback = [&] {
        check(!frame.run(1) && !frame.begin(h.data),
              "Frame callback cannot reenter run/begin");
        frame.release();
        check(frame.snapshot().has_value(),
              "Reentrant release cannot destroy the live outer frame");
    };
    check(take(frame.run(4096)) == VmfNotificationEffectStatus::pending &&
              h.clock.calls == 1 && h.formatter.snapshot().active_frames == 1,
          "Outer frame survives rejected callback operations");
    h.clock.callback = {};
    frame.release();
    check(!frame.snapshot() && h.formatter.snapshot().active_frames == 0,
          "Release abandons the actual pending formatter before buffer death");
    h.named.catalogs = h.registry;
    take(frame.begin(h.data));
    check(take(frame.run(4096)) == VmfNotificationEffectStatus::finished &&
              frame.snapshot()->ui_notified,
          "Released frame can be leased again for a real notification");
    frame.release();
    h.clock.fail = true;
    take(frame.begin(h.data));
    check(!frame.run(4096) && frame.last_error() && frame.snapshot()->faulted,
          "Frame retains the actual nested failure");
    frame.release();
    check(frame.last_error() && !frame.snapshot(),
          "Failure diagnostics survive restitution");
    h.clock.fail = false;
    take(frame.begin(h.data));
    check(!frame.last_error(), "A successful new lease resets old diagnostics");
    frame.release();
}

void recursive_formatter_cancellation() {
    Harness h;
    h.catalog.add(0x300U, "\xc2\xa7REW_MAX\xc2\xa7");
    h.catalog.add(0x039d61b2U, "\xc2\xa7"
                               "COMPL_PCT\xc2\xa7/\xc2\xa7"
                               "1\xc2\xa7");
    h.registry[0] = h.catalog.binding(15);
    h.registry[2] = h.catalog.binding(0);
    h.data.body_low_28 = 0x300U;
    h.data.body_high_2c = 0U;
    struct Host final : VmfNotificationNamedHost {
        VmfNotificationState state;
        VmfNotificationProfile identity;
        std::function<void()> callback;
        std::expected<VmfNotificationState*, VmfNotificationStateError>
        notification_state() noexcept override {
            if (callback)
                callback();
            return &state;
        }
        std::expected<VmfNotificationProfile*, VmfNotificationStateError>
        profile() noexcept override {
            return &identity;
        }
        std::expected<VmfNotificationNews*, VmfNotificationStateError>
        news() noexcept override {
            return nullptr;
        }
    } host;
    auto locale = take(VmfMenuLocaleState::create("us", "us", 0));
    h.named.reward = 1000U;
    h.named.money.locale = &locale;
    h.named.host = &host;
    h.named.x87_control_word = std::uint16_t{0x037f};
    h.services.currency_owner = &h.owner;
    std::size_t rejected{};
    host.callback = [&] {
        if (!h.formatter.cancel())
            ++rejected;
    };
    VmfNotificationEffectBF80Frame frame(h.inputs);
    take(frame.begin(h.data));
    check(take(frame.run(4096)) == VmfNotificationEffectStatus::pending &&
              h.formatter.snapshot().active_frames == 2 &&
              h.formatter.frame_snapshot(1) && rejected == 2,
          "Actual recursive currency formatter retains two frames and rejects "
          "callback cancel");
    frame.release();
    check(
        h.formatter.snapshot().active_frames == 0 &&
            !h.formatter.frame_snapshot(1) && h.owner.selector_4820 == 15U,
        "BF80 cancellation releases all recursive references without rollback");
}

void actual_child_dispatch_composition() {
    Harness h;
    h.child.active_08 = std::uint8_t{1};
    h.child.mask_0c = 0U;
    h.child.limit_18 = 1U;
    h.child.id_low_30 = 0x52f820U;
    h.child.id_high_34 = 0U;
    auto display = take(VmfDisplayRuntime::create_with_variables(
        tdur::test::program(tdur::assets::VmfProfile::pc_2_8)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationDispatchInputs dispatch_inputs;
    dispatch_inputs.globals.movies = std::span{movies};
    dispatch_inputs.globals.name_f16204 = "/:before_notification";
    struct Module final : VmfNotificationChildEffects {
        VmfNotificationDispatchFrame dispatch;
        VmfNotificationEffectBF80Frame notification;
        VmfNotificationChildEffectData& data;
        std::size_t dispatch_begins{}, notification_begins{};
        Module(const VmfNotificationDispatchInputs& in,
               const VmfNotificationBF80Inputs& out,
               VmfNotificationChildEffectData& value)
            : dispatch(in), notification(out), data(value) {}
        std::expected<VmfNotificationChildEffectFrame*,
                      VmfNotificationStateError>
        begin_effect(VmfNotificationChildEffect effect,
                     VmfNotificationChildState& child) noexcept override {
            if (&child != &data.child)
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_context,
                    "Metadata identity mismatch in test adapter"});
            if (effect == VmfNotificationChildEffect::call_89e330) {
                ++dispatch_begins;
                return dispatch.begin(child);
            }
            if (effect == VmfNotificationChildEffect::call_89bf80) {
                ++notification_begins;
                const auto begun = notification.begin(data);
                if (!begun)
                    return std::unexpected(begun.error());
                return &notification;
            }
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Unexpected effect in actual BF80 composition"});
        }
    } module(dispatch_inputs, h.inputs, h.data);
    struct Evaluator final : VmfNotificationChildEvaluator {
        std::size_t calls{};
        std::expected<std::uint32_t, VmfNotificationStateError>
        evaluate(VmfNotificationChildState&,
                 std::uint32_t* output) noexcept override {
            ++calls;
            if (output)
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::invalid_context,
                    "89DD60 must pass NULL to evaluator"});
            return 0U;
        }
    } evaluator;
    VmfNotificationChildInputs inputs;
    inputs.evaluator = &evaluator;
    inputs.flag_16a2d2f = std::uint8_t{1};
    inputs.effects = &module;
    {
        auto child = VmfNotificationChildContext::create(h.child, 7U, 0);
        bool completed{};
        for (unsigned attempt = 0; attempt < 8192; ++attempt) {
            const auto result = take(child.run(inputs, 1));
            check(result.steps <= 1, "Actual child obeys one-step budget");
            if (result.status == VmfNotificationChildStatus::finished) {
                completed = true;
                break;
            }
        }
        check(completed && child.snapshot().completed_effects == 2 &&
                  child.snapshot().result == std::uint8_t{0},
              "Actual child completes dispatch then BF80, with native ignored "
              "returns");
    }
    const auto variable =
        take(display->variable_dictionary()->find(0, "before_notification"));
    check(variable && std::string_view(variable->value.text.data()) == "1" &&
              module.dispatch_begins == 1 && module.notification_begins == 1 &&
              evaluator.calls == 1 && !module.dispatch.snapshot() &&
              !module.notification.snapshot() && h.clock.calls == 1 &&
              h.text.single.index == 1 &&
              std::string_view(h.text.single.slots[0].data()) ==
                  "Driver Driver_A / Club Club_A",
          "Real dispatcher setter, notification DB/formatter/ring and frame "
          "release compose");
    h.named.catalogs.reset();
    {
        auto child = VmfNotificationChildContext::create(h.child, 7U, 0);
        bool pending{};
        for (unsigned attempt = 0; attempt < 8192; ++attempt) {
            take(child.run(inputs, 1));
            const auto frame = module.notification.snapshot();
            if (frame && frame->formatter_polls != 0 &&
                h.formatter.snapshot().active_frames == 1) {
                pending = true;
                break;
            }
        }
        check(pending && child.snapshot().completed_effects == 1 &&
                  !module.dispatch.snapshot() && h.text.single.index == 1,
              "Real child retains only BF80 while the catalog is unknown");
    }
    check(!module.notification.snapshot() &&
              h.formatter.snapshot().active_frames == 0 &&
              h.owner.selector_4820 == 15U && h.clock.calls == 2,
          "Child abandonment releases actual BF80 before its buffer dies");
    h.named.catalogs = h.registry;
    {
        auto child = VmfNotificationChildContext::create(h.child, 7U, 0);
        bool complete{};
        for (unsigned attempt = 0; attempt < 8192; ++attempt) {
            const auto report = take(child.run(inputs));
            if (report.status == VmfNotificationChildStatus::finished) {
                complete = true;
                break;
            }
        }
        check(complete && child.snapshot().completed_effects == 2 &&
                  h.text.single.index == 2 && module.dispatch_begins == 3 &&
                  module.notification_begins == 3 && evaluator.calls == 3 &&
                  !module.notification.snapshot(),
              "Same child, module frames and formatter publish after "
              "cancellation");
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "--selector-before-id")
        selector_precedes_unknown_id();
    else {
        clock_failure_is_terminal();
        selector_precedes_unknown_id();
        scenario = "guards";
        initial_guards_and_reentry();
        scenario = "full_path";
        full_path_and_budgets();
        scenario = "skip_and_NULL";
        skipped_tables_and_known_nulls();
        scenario = "key_name_capture";
        key_record_and_name_captures();
        scenario = "group_capture";
        group_text_and_owner_captures();
        scenario = "format_order";
        format_capture_order_and_late_cleanup();
        scenario = "cleanup_ring";
        cleanup_and_ring_boundaries();
        scenario = "cancellation";
        cancellation_errors_and_foreign_leases();
        scenario = "frame_guards";
        frame_lifecycle_and_callback_guards();
        scenario = "recursive_cancel";
        recursive_formatter_cancellation();
        scenario = "actual_child";
        actual_child_dispatch_composition();
    }
    std::cout << checks << " notification-bf80 checks passed\n";
}

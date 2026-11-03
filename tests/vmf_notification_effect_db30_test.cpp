#include "tdur/ui/vmf_notification_effect_db30.hpp"
#include "tdur/ui/vmf_notification_parent.hpp"
#include "vmf_synthetic_movie.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace db30_allocations {
bool enabled{};
std::size_t calls{};
} // namespace db30_allocations
void* operator new(std::size_t bytes) {
    if (db30_allocations::enabled)
        ++db30_allocations::calls;
    if (void* value = std::malloc(bytes == 0 ? 1 : bytes))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::ui;
using Phase = VmfNotificationDB30Phase;
using Status = VmfNotificationDB30Status;
std::size_t checks{};
const char* scenario = "setup";
void check(bool value, const char* detail) {
    ++checks;
    if (!value)
        throw std::runtime_error(detail);
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Required DB30 operation failed");
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}
struct Clock : VmfNotificationClock {
    VmfNotificationTime value{2026, 1, 2, 3, 4, 5, 6, 7};
    std::size_t calls{};
    std::function<void()> callback;
    std::expected<VmfNotificationTime, VmfNotificationStateError>
    get_local_time() noexcept override {
        ++calls;
        if (callback)
            callback();
        return value;
    }
};
struct Evaluator : VmfNotificationChildEvaluator {
    std::uint32_t result = 0xfffffffeU, output = 42;
    std::size_t calls{};
    std::uint32_t* argument{};
    bool fail{};
    std::function<void()> callback;
    std::expected<std::uint32_t, VmfNotificationStateError>
    evaluate(VmfNotificationChildState&,
             std::uint32_t* selected) noexcept override {
        ++calls;
        argument = selected;
        if (!selected)
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "89DB30 must pass a real output argument"});
        *selected = output;
        if (callback)
            callback();
        if (fail)
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Synthetic evaluator failure"});
        return result;
    }
};
struct Language : VmfNotificationLanguageSource {
    const std::array<char, 2> text{'u', 's'};
    std::size_t calls{}, fail_at{};
    std::function<void()> callback;
    std::expected<std::span<const char>, VmfNotificationRecordError>
    lookup() noexcept override {
        ++calls;
        if (callback)
            callback();
        if (calls == fail_at)
            return std::unexpected(VmfNotificationRecordError{
                VmfNotificationRecordErrorCode::language_dependency,
                "Synthetic language failure",
                {}});
        return std::span<const char>{text};
    }
};
struct Hook : VmfNotificationQueueHook {
    std::size_t calls{};
    bool fail{};
    std::function<void()> callback;
    std::expected<void, VmfNotificationQueueError>
    notify(std::uint32_t value) noexcept override {
        ++calls;
        if (callback)
            callback();
        if (fail || value != 0)
            return std::unexpected(VmfNotificationQueueError{
                VmfNotificationQueueErrorCode::callback,
                "Synthetic queue callback failure",
                {},
                0});
        return {};
    }
};
struct Host : VmfNotificationNewsHost {
    VmfNotificationNewsOwner* selected{};
    VmfNotificationState state;
    VmfNotificationProfile profile_value;
    std::size_t calls{};
    std::function<void()> callback;
    std::function<void()> state_callback;
    std::expected<VmfNotificationNewsOwner*, VmfNotificationStateError>
    notification_news_owner() noexcept override {
        ++calls;
        if (callback)
            callback();
        return selected;
    }
    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        if (state_callback)
            state_callback();
        return &state;
    }
    std::expected<VmfNotificationProfile*, VmfNotificationStateError>
    profile() noexcept override {
        return &profile_value;
    }
};
struct Catalog {
    std::vector<std::byte> records;
    std::vector<char> pool;
    void add(std::uint64_t id, std::string_view text) {
        const auto offset = static_cast<std::uint32_t>(pool.size());
        pool.insert(pool.end(), text.begin(), text.end());
        pool.push_back('\0');
        const std::array words{static_cast<std::uint32_t>(id),
                               static_cast<std::uint32_t>(id >> 32), offset,
                               0U};
        for (auto value : words)
            for (unsigned shift = 0; shift < 32; shift += 8)
                records.push_back(std::byte((value >> shift) & 255U));
    }
    VmfCatalogBinding binding(std::uint32_t selector) const {
        return {
            vmf_catalog_descriptor_offset(selector),
            {static_cast<std::int32_t>(records.size() / 16), records, pool}};
    }
};
// Only clock, evaluator, +30 getter, language/hook callbacks are fixtures.
// All text bindings, formatters, records, queues, rings and frame logic are
// real.
struct Harness {
    VmfNotificationChildState child;
    VmfNotificationChildEffectData data{child};
    VmfNotificationWrapperOwner owner;
    Clock clock;
    Evaluator evaluator;
    Language language;
    Hook hook;
    VmfNotificationQueueDomain domain;
    VmfNotificationNewsOwner news{domain,
                                  {&hook, nullptr, nullptr, nullptr, nullptr}};
    Host host;
    VmfNotificationTextOwner text;
    Catalog catalog;
    std::array<VmfCatalogBinding, 2> registry;
    VmfNotificationNamedInputs named;
    VmfNotificationFormatServices services{named};
    VmfNotificationFormatter formatter =
        take(VmfNotificationFormatter::create(services));
    VmfNotificationDB30Inputs inputs;
    Harness() {
        child.weight_14 = 0xffffffffU;
        child.limit_18 = 0x80000000U;
        data.title_low_20 = 0x100;
        data.title_high_24 = 0;
        data.body_low_28 = 0x200;
        data.body_high_2c = 0;
        catalog.add(0x100, "Title \xc2\xa7K_WEIGHT\xc2\xa7");
        catalog.add(0x200,
                    "Result \xc2\xa7K_RESULT\xc2\xa7; Output "
                    "\xc2\xa7K_OUTPUT\xc2\xa7; Limit \xc2\xa7K_LIMIT\xc2\xa7");
        registry = {catalog.binding(15), catalog.binding(7)};
        named.catalogs = registry;
        host.selected = &news;
        inputs.owner_10e7eec = &owner;
        inputs.host_114c15c = &host;
        inputs.text_owner_10e7528 = &text;
        inputs.clock = &clock;
        inputs.evaluator = &evaluator;
        inputs.formatter = &formatter;
        inputs.language = &language;
        inputs.keys = {"K_WEIGHT", "K_RESULT", "K_OUTPUT", "K_LIMIT"};
        for (auto& arg : inputs.arguments)
            arg.emplace(std::nullopt);
    }
};
void to_phase(VmfNotificationDB30Context& context,
              const VmfNotificationDB30Inputs& inputs, Phase phase) {
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        if (context.snapshot().phase == phase)
            return;
        const auto result = take(context.run(inputs, 1));
        check(result.steps == 1,
              "One-step nested progress consumes one parent step");
        check(result.status != Status::finished,
              "Target phase precedes finish");
    }
    throw std::runtime_error("DB30 phase bound exceeded");
}
VmfNotificationDB30Report finish(VmfNotificationDB30Context& context,
                                 const VmfNotificationDB30Inputs& inputs,
                                 std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        auto result = take(context.run(inputs, budget));
        check(result.steps <= budget, "Parent respects step budget");
        if (result.status == Status::finished)
            return result;
        check(result.status == Status::step_limit && result.steps != 0,
              "Fully supplied fixture makes progress until finished");
    }
    throw std::runtime_error("DB30 completion bound exceeded");
}
void guard_time_and_unknowns() {
    Harness h;
    VmfNotificationDB30Context context(h.child, h.data);
    check(take(context.run(h.inputs, 0)).steps == 0 && h.clock.calls == 0,
          "Zero budget performs no read, store or clock call");
    h.inputs.owner_10e7eec = nullptr;
    check(finish(context, h.inputs).state.skipped_null_owner &&
              h.clock.calls == 0,
          "Known NULL initial owner skips all mandatory dependencies");
    check(take(context.run(h.inputs, 0)).status == Status::finished,
          "Completed zero-budget invocation remains completed");
    for (auto time :
         {VmfNotificationTime{2026, 1, 2, 3, 4, 5, 6, 7},
          VmfNotificationTime{2199, 0xffff, 0x7fff, 0xeeee, 0xdddd}}) {
        const auto expected =
            ((std::uint32_t{time.year} % 100U) * 12U + time.month +
             (std::uint32_t{time.day_of_week} << 16)) *
                31U +
            time.day + (std::uint32_t{time.hour} << 16);
        check(time.datepack() == expected,
              "Native DWORD datepack retains high words and wrap");
    }
    Harness waiting;
    waiting.child.weight_14.reset();
    VmfNotificationDB30Context unknown(waiting.child, waiting.data);
    auto r = take(unknown.run(waiting.inputs));
    check(r.status == Status::needs_weight && waiting.clock.calls == 1 &&
              unknown.snapshot().binding_calls == 0,
          "Unknown weight waits instead of becoming zero");
    waiting.inputs.owner_10e7eec.reset();
    waiting.child.weight_14 = 0xffffffffU;
    r = take(unknown.run(waiting.inputs));
    check(r.status == Status::needs_owner &&
              std::string_view(
                  waiting.owner.parameters_4824.slots[0].value.data()) ==
                  "-1" &&
              waiting.clock.calls == 1,
          "Captured first owner survives weight wait without replaying clock");
    Harness limit;
    limit.child.limit_18.reset();
    VmfNotificationDB30Context missing_limit(limit.child, limit.data);
    check(take(missing_limit.run(limit.inputs)).status == Status::needs_limit &&
              missing_limit.snapshot().binding_calls == 3,
          "Unknown limit waits after three real bindings");
    VmfNotificationWindowsClock actual_clock;
    const auto os = take(actual_clock.get_local_time());
    check(os.year >= 2026 && os.month >= 1 && os.month <= 12 && os.day >= 1 &&
              os.day <= 31 && os.day_of_week <= 6 && os.hour < 24 &&
              os.minute < 60 && os.second <= 60 && os.milliseconds < 1000,
          "Concrete Windows clock returns populated host OS local time");
}
void actual_full_pipeline(std::size_t budget) {
    Harness h;
    VmfNotificationDB30Context context(h.child, h.data);
    const auto allocations = db30_allocations::calls;
    db30_allocations::enabled = true;
    const auto result = finish(context, h.inputs, budget);
    db30_allocations::enabled = false;
    check(db30_allocations::calls == allocations,
          "Prepared real DB30 pipeline uses no ordinary heap allocation on "
          "this path");
    check(result.state.clock_calls == 1 && result.state.evaluator_calls == 1 &&
              result.state.binding_calls == 4 &&
              result.state.buffer_clears == 1 &&
              result.state.selector_stores == 2 &&
              result.state.formatter_begins == 2 &&
              result.state.record_initializations == 1 &&
              result.state.record_calls == 1 && result.state.host_calls == 1 &&
              result.state.queue_calls == 1 && result.state.ring_calls == 1 &&
              result.state.parameter_clears == 1,
          "Every mandatory native call or prefix executes exactly once");
    const auto& record = h.news.queue.records[0];
    check(std::string_view(record.title_0a.data()) == "Title -1" &&
              std::string_view(record.body_9a.data()) ==
                  "Result -2; Output 42; Limit -2147483648" &&
              record.field_00 == 4 && record.field_04 == 1 &&
              record.field_08 ==
                  static_cast<std::uint16_t>(h.clock.value.datepack()) &&
              record.language_89c == 0x7573 &&
              record.descriptor_940 == 0xffffffffU &&
              record.text_95c[0] == '\0' && record.text_96c[0] == '\0',
          "Actual parameter/catalog/record/queue chain has native scalars and "
          "signed texts");
    check(h.text.pair.index == 1 &&
              std::string_view(h.text.pair.slots[0].first.data()) ==
                  "Title -1" &&
              std::string_view(h.text.pair.slots[0].second.data()) ==
                  "Result -2; Output 42; Limit -2147483648" &&
              h.domain.changed.load() == 1 && h.hook.calls == 1,
          "Actual double ring receives the produced text after real queue/hook "
          "effects");
    check(h.owner.selector_4820 == 0U &&
              h.owner.parameters_4824.slots[0].key[0] == '\0' &&
              h.owner.parameters_4824.slots[0].key[1] == '_' &&
              h.formatter.snapshot().active_frames == 0,
          "Final clear only zeroes leading bytes, selector is zero and "
          "formatter is released");
    auto* named = take(h.host.news());
    check(named && named->records &&
              named->records->data() == h.news.queue.records.data(),
          "Named +30 view and insertion owner expose the SAME authoritative "
          "records");
}
void callback_capture_and_late_fields() {
    Harness h;
    VmfNotificationWrapperOwner later;
    h.evaluator.callback = [&] { h.inputs.owner_10e7eec = &later; };
    VmfNotificationDB30Context context(h.child, h.data);
    to_phase(context, h.inputs, Phase::output_read);
    check(std::string_view(h.owner.parameters_4824.slots[1].value.data()) ==
                  "-2" &&
              later.parameters_4824.slots[0].key[0] == '\0',
          "Owner captured BEFORE evaluator is used after its global-changing "
          "callback");
    take(context.run(h.inputs, 1));
    *h.evaluator.argument = 99;
    to_phase(context, h.inputs, Phase::limit_read);
    check(std::string_view(later.parameters_4824.slots[0].value.data()) == "42",
          "Output DWORD read precedes owner wait and is retained as42, not "
          "later99");
    Harness ids;
    ids.data.title_low_20.reset();
    VmfNotificationDB30Context unknown(ids.child, ids.data);
    auto r = take(unknown.run(ids.inputs));
    check(r.status == Status::needs_id && ids.owner.selector_4820 == 15U &&
              r.state.buffer_clears == 1 && r.state.selector_stores == 1,
          "Unknown title low ID waits AFTER selector15 store and high ID "
          "capture");
    ids.data.title_high_24 = 99;
    ids.data.title_low_20 = 0x100;
    ids.inputs.owner_10e7eec.reset();
    ids.inputs.formatter.reset();
    r = take(unknown.run(ids.inputs));
    check(
        r.status == Status::needs_formatter && r.state.selector_stores == 1,
        "Missing formatter does not replay selector or recapture title owner");
    ids.inputs.formatter = &ids.formatter;
    to_phase(unknown, ids.inputs, Phase::body_high);
    check(std::string_view(unknown.title().data()) == "Title -1",
          "Captured high0 survives mutation of metadata to high99");
    ids.data.body_high_2c = 0;
    ids.data.body_low_28 = 0x100; // Same ID intentionally chosen late.
    VmfNotificationWrapperOwner body_owner;
    body_owner.selector_4820 = 7;
    take(bind_vmf_notification_text(&body_owner.parameters_4824, "K_WEIGHT",
                                    "77"));
    ids.inputs.owner_10e7eec = &body_owner;
    to_phase(unknown, ids.inputs, Phase::body_run);
    check(body_owner.selector_4820 == 7U,
          "Body owner reload does not invent a second selector15 store");
    ids.inputs.formatter.reset();
    to_phase(unknown, ids.inputs, Phase::reset_owner);
    check(std::string_view(unknown.body().data()) == "Title 77",
          "Body ID read late and real formatter retained after input reset");
    VmfNotificationWrapperOwner reset;
    ids.inputs.owner_10e7eec = &reset;
    to_phase(unknown, ids.inputs, Phase::record_populate);
    check(reset.selector_4820 == 0U && body_owner.selector_4820 == 7U,
          "Selector reset reloads another global owner and does not restore "
          "title owner");
}
void queue_ui_and_error_prefixes() {
    Harness h;
    VmfNotificationDB30Context context(h.child, h.data);
    to_phase(context, h.inputs, Phase::enqueue);
    h.inputs.host_114c15c.reset();
    h.inputs.language.reset();
    to_phase(context, h.inputs, Phase::ui_write);
    VmfNotificationTextOwner unused;
    h.inputs.text_owner_10e7528 = &unused;
    h.inputs.owner_10e7eec.reset();
    auto wait = take(context.run(h.inputs));
    check(wait.status == Status::needs_owner && h.text.pair.index == 1 &&
              unused.pair.index == 0 && h.host.calls == 1 && h.hook.calls == 1,
          "Captured queue/language/target survive rebinding without replay");
    VmfNotificationWrapperOwner final;
    take(bind_vmf_notification_text(&final.parameters_4824, "KEEP", "tail"));
    h.inputs.owner_10e7eec = &final;
    finish(context, h.inputs);
    check(
        final.parameters_4824.slots[0].key[0] == '\0' &&
            h.owner.parameters_4824.slots[0].key[0] != '\0',
        "Last clear samples live global only after queue and ring completion");
    Harness failure;
    failure.hook.fail = true;
    VmfNotificationDB30Context bad(failure.child, failure.data);
    const auto result = bad.run(failure.inputs);
    check(!result && result.error().queue_error &&
              bad.snapshot().queue_calls == 1 &&
              failure.domain.changed.load() == 1 && failure.hook.calls == 1 &&
              failure.news.queue.records[0].field_04 == 1 &&
              failure.text.pair.index == 0 &&
              bad.snapshot().parameter_clears == 0 && !bad.run(failure.inputs),
          "Hook failure keeps inserted record/dirty prefix and skips UI/final "
          "clear");
    Harness null_final;
    VmfNotificationDB30Context cleared(null_final.child, null_final.data);
    to_phase(cleared, null_final.inputs, Phase::final_owner);
    null_final.inputs.owner_10e7eec = nullptr;
    const auto invalid = cleared.run(null_final.inputs);
    check(!invalid &&
              invalid.error().code ==
                  VmfNotificationDB30ErrorCode::invalid_binding &&
              cleared.snapshot().ring_calls == 1 &&
              cleared.snapshot().parameter_clears == 0,
          "Known NULL final owner is an invalid native clear, not successful "
          "skip");
}
void cancellation_frames_and_reentry() {
    Harness h;
    h.named.catalogs.reset();
    VmfNotificationDB30Frame frame(h.inputs);
    check(!frame.run(1), "Unleased frame refuses execution");
    take(frame.begin(h.child, h.data));
    check(!frame.begin(h.child, h.data) &&
              take(frame.run(0)) == VmfNotificationEffectStatus::pending &&
              h.clock.calls == 0,
          "Exclusive lease and zero budget preserve pristine state");
    h.evaluator.callback = [&] {
        check(!frame.run(1) && !frame.begin(h.child, h.data),
              "Frame rejects recursive run/begin without damaging outer "
              "invocation");
        frame.release();
        check(frame.snapshot().has_value(),
              "Reentrant release cannot destroy live buffers");
    };
    check(take(frame.run(4096)) == VmfNotificationEffectStatus::pending &&
              h.formatter.snapshot().active_frames == 1,
          "Actual formatter remains borrowed during unresolved catalog wait");
    frame.release();
    check(!frame.snapshot() && h.formatter.snapshot().active_frames == 0 &&
              !h.formatter.run(1) && h.owner.selector_4820 == 15U,
          "Cancellation drops real formatter lease while preserving selector "
          "prefix");
    h.named.catalogs = h.registry;
    h.evaluator.callback = {};
    VmfNotificationDB30Context reused(h.child, h.data);
    finish(reused, h.inputs);
    check(reused.snapshot().ring_calls == 1,
          "Cancelled formatter can serve another real effect invocation");
    Harness foreign;
    foreign.named.catalogs.reset();
    std::array<char, 16> external{};
    take(foreign.formatter.begin_catalog(foreign.owner, external, 1,
                                         foreign.inputs.arguments));
    VmfNotificationDB30Context contender(foreign.child, foreign.data);
    const auto refused = contender.run(foreign.inputs);
    check(!refused && refused.error().formatter_error &&
              foreign.formatter.snapshot().active_frames == 1,
          "Failed begin never cancels a formatter root owned by someone else");
    take(foreign.formatter.cancel());
    Harness mismatch;
    VmfNotificationChildState other;
    check(!frame.begin(other, h.data),
          "Frame refuses mismatched child metadata identity");
    mismatch.evaluator.fail = true;
    VmfNotificationDB30Frame failed(mismatch.inputs);
    take(failed.begin(mismatch.child, mismatch.data));
    check(!failed.run(4096) && failed.last_error() &&
              failed.snapshot()->output_argument == 42U,
          "Evaluator error preserves the already-written output argument and "
          "cause");
    failed.release();
    check(failed.last_error() && !failed.snapshot(),
          "Frame diagnostics survive lease restitution");
}
void cancel_recursive_descendants() {
    Harness h;
    constexpr char reward[] = "\xc2\xa7REW_MAX\xc2\xa7";
    h.catalog.add(0x300, reward);
    h.catalog.add(0x039d61b2U, "\xc2\xa7"
                               "COMPL_PCT\xc2\xa7/\xc2\xa7"
                               "1\xc2\xa7");
    h.registry = {h.catalog.binding(15), h.catalog.binding(7)};
    h.data.title_low_20 = 0x300;
    h.named.reward = 1000U;
    auto locale = take(VmfMenuLocaleState::create("us", "us", 0));
    h.named.money.locale = &locale;
    h.named.host = &h.host;
    h.named.x87_control_word = std::uint16_t{0x037f};
    h.services.currency_owner = &h.owner;
    std::size_t rejected_cancels{};
    h.host.state_callback = [&] {
        if (!h.formatter.cancel())
            ++rejected_cancels;
    };
    VmfNotificationDB30Frame frame(h.inputs);
    take(frame.begin(h.child, h.data));
    check(take(frame.run(4096)) == VmfNotificationEffectStatus::pending &&
              h.formatter.snapshot().active_frames == 2 &&
              h.formatter.frame_snapshot(1) && rejected_cancels == 2,
          "Recursive currency formatting waits with two real frames; callback "
          "cancel is rejected");
    const auto selector = h.owner.selector_4820;
    frame.release();
    check(h.formatter.snapshot().active_frames == 0 &&
              !h.formatter.frame_snapshot(1) && !h.formatter.run(1) &&
              h.owner.selector_4820 == selector,
          "Abandonment releases recursive references without rollback or a "
          "dangling child frame");
    std::array<char, 32> output{};
    constexpr char literal[] = "Reusable";
    take(h.formatter.begin_plain(
        nullptr, output,
        VmfNotificationFormatSource::borrowed(std::span{literal}),
        h.inputs.arguments));
    auto result = take(h.formatter.run(4096));
    check(result.status == VmfNotificationWrapperStatus::finished &&
              std::string_view(output.data()) == "Reusable",
          "Same preallocated pool is reusable after recursive abandonment");
}
void actual_child_composition() {
    Harness h;
    h.child.active_08 = std::uint8_t{1};
    h.child.mask_0c = 0;
    h.child.limit_18 = 100;
    h.child.id_low_30 = 0x517180;
    h.child.id_high_34 = 0;
    struct DualEvaluator final : VmfNotificationChildEvaluator {
        std::size_t null_calls{}, output_calls{};
        std::expected<std::uint32_t, VmfNotificationStateError>
        evaluate(VmfNotificationChildState&,
                 std::uint32_t* output) noexcept override {
            if (!output) {
                ++null_calls;
                return 0;
            }
            ++output_calls;
            *output = 66;
            return 55;
        }
    } evaluator;
    h.inputs.evaluator = &evaluator;
    struct Module final : VmfNotificationChildEffects {
        VmfNotificationDB30Frame frame;
        const VmfNotificationChildEffectData& data;
        std::size_t begins{};
        Module(const VmfNotificationDB30Inputs& inputs,
               const VmfNotificationChildEffectData& metadata)
            : frame(inputs), data(metadata) {}
        std::expected<VmfNotificationChildEffectFrame*,
                      VmfNotificationStateError>
        begin_effect(VmfNotificationChildEffect effect,
                     VmfNotificationChildState& child) noexcept override {
            ++begins;
            if (effect != VmfNotificationChildEffect::call_89db30)
                return std::unexpected(VmfNotificationStateError{
                    VmfNotificationStateErrorCode::host_failure,
                    "Unexpected effect in real DB30 composition"});
            return frame.begin(child, data);
        }
    } module(h.inputs, h.data);
    VmfNotificationChildInputs inputs;
    inputs.evaluator = &evaluator;
    inputs.flag_16a2d2f = std::uint8_t{1};
    inputs.effects = &module;
    auto context = VmfNotificationChildContext::create(h.child, 7, 0);
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        const auto report = take(context.run(inputs, 4096));
        if (report.status == VmfNotificationChildStatus::finished)
            break;
        check(report.status == VmfNotificationChildStatus::effect_pending,
              "Real child yields only while the actual DB30 frame is pending");
    }
    check(context.snapshot().result == std::uint8_t{0} &&
              context.snapshot().completed_effects == 1 && module.begins == 1 &&
              evaluator.null_calls == 1 && evaluator.output_calls == 1 &&
              !module.frame.snapshot() && h.clock.calls == 1 &&
              h.hook.calls == 1 && h.text.pair.index == 1 &&
              std::string_view(h.news.queue.records[0].body_9a.data()) ==
                  "Result 55; Output 66; Limit 100",
          "Actual89DD60 invokes and releases actual89DB30, with distinct "
          "NULL/nonNULL evaluator calls and real queue/ring effects");
}
void parent_catalog_cancellation() {
    auto display = take(VmfDisplayRuntime::create_with_variables(
        tdur::test::program(tdur::assets::VmfProfile::pc_2_8)));
    const auto linked = take(VmfLinkedText::copy("\xc2\xa7"
                                                 "1\xc2\xa7"));
    const auto pool = *VmfValue::linked(linked).text_reference();
    std::array<std::byte, 16> rows{};
    for (unsigned index = 0; index < 4; ++index)
        rows[index] = std::byte((0x037bf702U >> (8U * index)) & 255U);
    std::array catalogs{VmfCatalogBinding{
        0x48,
        {1, std::span<const std::byte>{rows},
         std::span<const char>{pool.c_str(), pool.extent_bytes()}}}};
    std::array pools{VmfNotificationCatalogPoolBinding{0x48, pool}};
    VmfNotificationNamedInputs named;
    named.catalogs = catalogs;
    VmfNotificationFormatServices services(named);
    auto formatter = take(VmfNotificationFormatter::create(services));
    auto cache = take(VmfNotificationFormatCache::create());
    VmfNotificationWrapperOwner wrapper;
    wrapper.selector_4820 = 0;
    VmfNotificationState state;
    state.count_0c = 0;
    state.thresholds.fill(1U);
    VmfNotificationParentOwner owner(state);
    owner.flag_204 = std::uint8_t{1};
    owner.flag_205 = std::uint8_t{1};
    VmfNotificationManager manager{1U};
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationParentInputs inputs;
    inputs.globals.manager_10e72f0 = &manager;
    inputs.globals.movies = movies;
    inputs.globals.owner_10e7eec = &wrapper;
    inputs.globals.name_f16338 = "/:parent_cancellation";
    inputs.globals.named_inputs = named;
    inputs.globals.catalog_arguments.emplace(); // First argument unknown.
    inputs.globals.catalog_inputs.emplace();
    inputs.globals.catalog_inputs->pools = pools;
    inputs.globals.catalog_inputs->display.formatter = &formatter;
    inputs.globals.catalog_inputs->display.cache = &cache;
    VmfNotificationParentFrame frame(inputs);
    take(frame.begin(owner, 7, 0));
    for (unsigned attempt = 0; attempt < 8192; ++attempt) {
        take(frame.run(4096));
        if (formatter.snapshot().phase == VmfNotificationWrapperPhase::gather)
            break;
    }
    check(formatter.snapshot().active_frames == 1 && frame.snapshot(),
          "Real parent/catalog/display chain holds formatter while an argument "
          "waits");
    frame.release();
    const auto remaining = formatter.snapshot().active_frames;
    if (remaining)
        take(formatter
                 .cancel()); // Avoid leaving stale borrowed views on failure.
    check(remaining == 0, "Parent cancellation must release its display's "
                          "actual formatter lease");
    constexpr char text[] = "Reused after parent cancellation";
    (*inputs.globals.catalog_arguments)[0].emplace(
        VmfNotificationFormatSource::borrowed(std::span{text}));
    take(frame.begin(owner, 7, 0));
    auto status = VmfNotificationEffectStatus::pending;
    for (unsigned attempt = 0;
         attempt < 8192 && status != VmfNotificationEffectStatus::finished;
         ++attempt)
        status = take(frame.run(4096));
    check(status == VmfNotificationEffectStatus::finished &&
              formatter.snapshot().active_frames == 0,
          "Same parent and formatter complete real publication after "
          "cancellation");
    const auto variable =
        take(display->variable_dictionary()->find(0, "parent_cancellation"));
    check(variable && variable->value.linked &&
              variable->value.linked->text() == text,
          "Actual cache/setter publishes from the new invocation after reuse");
    frame.release();
}
} // namespace
int main() {
    try {
        scenario = "guard_time_and_unknowns";
        guard_time_and_unknowns();
        scenario = "actual_full_pipeline";
        actual_full_pipeline(4096);
        actual_full_pipeline(1);
        scenario = "callback_capture_and_late_fields";
        callback_capture_and_late_fields();
        scenario = "queue_ui_and_error_prefixes";
        queue_ui_and_error_prefixes();
        scenario = "cancellation_frames_and_reentry";
        cancellation_frames_and_reentry();
        scenario = "cancel_recursive_descendants";
        cancel_recursive_descendants();
        scenario = "actual_child_composition";
        actual_child_composition();
        scenario = "parent_catalog_cancellation";
        parent_catalog_cancellation();
        std::cout << checks << " notification-db30 checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        db30_allocations::enabled = false;
        std::cerr << scenario << ": " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

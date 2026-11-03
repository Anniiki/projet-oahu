#include "tdur/ui/vmf_notification_parent.hpp"
#include "vmf_synthetic_movie.hpp"

#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
using namespace tdur::ui;
using Phase = VmfNotificationParentPhase;
using Status = VmfNotificationParentStatus;
using Profile = tdur::assets::VmfProfile;
std::size_t checks{};
const char* scenario = "setup";
void check(bool condition, const char* detail) {
    ++checks;
    if (!condition)
        throw std::runtime_error(detail);
}
template <class T, class E> T take(std::expected<T, E> result) {
    check(result.has_value(), "Required parent operation failed");
    if constexpr (!std::is_void_v<T>)
        return *std::move(result);
}
struct Harness {
    VmfNotificationState state;
    VmfNotificationParentOwner owner{state};
    VmfNotificationManager manager{1U};
    VmfNotificationParentInputs inputs;
    Harness() {
        state.count_0c = 0;
        state.thresholds.fill(1U);
        owner.flag_204 = std::uint8_t{1};
        owner.flag_205 = std::uint8_t{1};
        owner.flag_3539 = std::uint8_t{1};
        owner.pending_208 = 0x80000000U;
        inputs.globals.manager_10e72f0 = &manager;
        inputs.globals.movies = std::span<const VmfMenuMovie>{};
    }
};
void to_phase(VmfNotificationParentContext& context,
              const VmfNotificationParentInputs& inputs, Phase phase) {
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        if (context.snapshot().phase == phase)
            return;
        const auto report = take(context.run(inputs, 1));
        check(report.steps == 1U, "One-step advance consumes exactly one step");
        check(report.status != Status::finished,
              "Target phase reached before completion");
    }
    throw std::runtime_error("Parent phase bound exceeded");
}
VmfNotificationParentReport finish(VmfNotificationParentContext& context,
                                   const VmfNotificationParentInputs& inputs,
                                   std::size_t budget = 4096) {
    for (unsigned attempt = 0; attempt < 1024; ++attempt) {
        const auto report = take(context.run(inputs, budget));
        check(report.steps <= budget, "Nested calls respect parent budget");
        if (report.status == Status::finished)
            return report;
        check(report.status == Status::step_limit ||
                  report.status == Status::child_pending ||
                  report.status == Status::service_8986b0_pending ||
                  report.status == Status::catalog_display_pending ||
                  report.status == Status::store_pending,
              "Fully supplied fixture has no unresolved input");
        check(report.steps != 0, "Pending fixture made measured progress");
    }
    throw std::runtime_error(
        "Parent completion bound exceeded at phase " +
        std::to_string(static_cast<int>(context.snapshot().phase)));
}
bool lock_available_elsewhere(std::recursive_mutex& mutex) {
    bool available{};
    std::thread other([&] {
        available = mutex.try_lock();
        if (available)
            mutex.unlock();
    });
    other.join();
    return available;
}
// Only native virtual getters/evaluator and remaining BF80/DB30 are synthetic.
// The parent, child, level/store, queue, toggle, catalog and setters are real.
struct Evaluator : VmfNotificationChildEvaluator {
    std::uint32_t value = 1;
    std::size_t calls{};
    std::function<void(VmfNotificationChildState&)> callback;
    std::expected<std::uint32_t, VmfNotificationStateError>
    evaluate(VmfNotificationChildState& child,
             std::uint32_t* output) noexcept override {
        if (output)
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "89DD60 must pass a NULL output pointer"});
        ++calls;
        if (callback)
            callback(child);
        return value;
    }
};
struct Effects : VmfNotificationChildEffects {
    std::array<VmfNotificationChildEffect, 8> trace{};
    std::size_t calls{};
    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin_effect(VmfNotificationChildEffect effect,
                 VmfNotificationChildState&) noexcept override {
        if (calls == trace.size())
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Synthetic trace capacity exceeded"});
        trace[calls++] = effect;
        return nullptr; // Declared synthetic synchronous effect completion.
    }
};
struct Host : VmfNotificationParentHost {
    VmfNotificationProgression progression;
    VmfNotificationState* state{};
    VmfNotificationIdentityOwner* identities{};
    std::size_t state_calls{}, progression_calls{}, identity_calls{};
    bool fail_progression{}, null_progression{};
    std::function<void()> callback;
    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        ++state_calls;
        return state;
    }
    std::expected<VmfNotificationProgression*, VmfNotificationStateError>
    notification_progression() noexcept override {
        ++progression_calls;
        if (callback)
            callback();
        if (fail_progression)
            return std::unexpected(VmfNotificationStateError{
                VmfNotificationStateErrorCode::host_failure,
                "Synthetic progression failure"});
        return null_progression ? nullptr : &progression;
    }
    std::expected<VmfNotificationIdentityOwner*, VmfNotificationStateError>
    notification_identities() noexcept override {
        ++identity_calls;
        if (identities)
            return identities;
        return std::unexpected(VmfNotificationStateError{
            VmfNotificationStateErrorCode::host_failure,
            "Unexpected identity query in parent test"});
    }
};
VmfNotificationChildState child(std::uint8_t active = 1,
                                std::uint32_t mask = 0) {
    VmfNotificationChildState result;
    result.active_08 = active;
    result.weight_14 = 0;
    result.mask_0c = mask;
    result.limit_18 = 1;
    result.id_low_30 = 0x517180;
    result.id_high_34 = 0;
    return result;
}
void guards_and_budgets() {
    Harness h;
    h.inputs.globals.manager_10e72f0.reset();
    h.owner.flag_204 = std::uint8_t{0};
    VmfNotificationParentContext context(h.owner, 2, 0);
    check(take(context.run(h.inputs, 0)).steps == 0U && !h.owner.field_2d4 &&
              context.snapshot().steps == 0U,
          "Zero budget causes no guard, lookup or prefix store");
    auto report = take(context.run(h.inputs));
    check(report.status == Status::needs_manager && report.steps == 1U,
          "Manager is captured even before the zero204 early return");
    h.owner.flag_204 = std::uint8_t{1};
    h.inputs.globals.manager_10e72f0 = &h.manager;
    report = take(context.run(h.inputs));
    check(report.status == Status::finished && report.state.result == 0U &&
              !h.owner.field_2d4,
          "Captured zero204 survives a manager wait and prevents mode store");
    check(take(context.run(h.inputs, 0)).status == Status::finished,
          "Completed zero-budget invocation remains completed");
    for (auto value : {0U, 0x12U, 0x18U}) {
        h.manager.state_44 = value;
        VmfNotificationParentContext guarded(h.owner, 2, 0);
        check(finish(guarded, h.inputs).state.result == 0U &&
                  !h.owner.field_2d4,
              "Disallowed manager states suppress synchronous work");
    }
    h.manager.state_44.reset();
    h.owner.flag_205 = std::uint8_t{0};
    VmfNotificationParentContext zero205(h.owner, 2, 0);
    check(finish(zero205, h.inputs).state.result == 0U,
          "Zero205 does not require unknown manager state");
    h.owner.flag_205 = std::uint8_t{1};
    h.inputs.globals.manager_10e72f0 = nullptr;
    VmfNotificationParentContext null_manager(h.owner, 2, 0);
    check(finish(null_manager, h.inputs).state.result == 0U,
          "Known NULL manager has the native early return");
    Harness supplied;
    VmfNotificationParentContext bounded(supplied.owner, 2, 0);
    const auto final = finish(bounded, supplied.inputs, 1);
    check(final.state.level_evaluations == 2U &&
              supplied.owner.field_2d4 == 0xffffffffU &&
              final.state.first_pass_iterations == 0U &&
              final.state.catalog_display_calls == 0U,
          "Both actual level calls complete under one-step budgets");
}
void deferred_prefixes_and_capture() {
    Harness h;
    h.owner.pending_208 = 99;
    VmfNotificationParentContext coalesced(h.owner, 7, 1);
    check(finish(coalesced, h.inputs).state.result == 0U &&
              h.owner.pending_208 == 99U &&
              lock_available_elsewhere(h.owner.mutex_1a8),
          "Coalesced request needs no queue and unlocks without stores");
    h.owner.pending_208 = 0x80000000U;
    VmfNotificationParentContext context(h.owner, 0x55, 1);
    auto waiting = take(context.run(h.inputs));
    check(waiting.status == Status::needs_queue &&
              h.owner.pending_208 == 0x80000000U &&
              !lock_available_elsewhere(h.owner.mutex_1a8),
          "Unknown queue waits inside real owner lock before208 store");
    bool rejected{};
    std::thread wrong_thread([&] {
        const auto result = context.run(h.inputs, 1);
        rejected =
            !result && result.error().code ==
                           VmfNotificationParentErrorCode::invalid_context;
    });
    wrong_thread.join();
    check(rejected && !context.snapshot().faulted &&
              !lock_available_elsewhere(h.owner.mutex_1a8),
          "Wrong-thread continuation cannot unlock or fault another thread's "
          "lock");
    VmfNotificationDeferredQueue first(0, {}), replacement(0, {});
    h.inputs.globals.queue_10e7238 = &first;
    to_phase(context, h.inputs, Phase::deferred_enqueue);
    h.inputs.globals.queue_10e7238 = &replacement;
    check(finish(context, h.inputs).state.result == 0U &&
              h.owner.pending_208 == 0x55U && first.count_314 == 1U &&
              first.slots_210[0] == &h.owner.deferred_token &&
              h.owner.deferred_token.flag_04 == 1U &&
              replacement.count_314 == 0U &&
              lock_available_elsewhere(h.owner.mutex_1a8),
          "Captured queue receives actual9B4490 insertion once and unlocks");
    h.owner.pending_208 = 0x80000000U;
    h.inputs.globals.queue_10e7238 = nullptr;
    VmfNotificationParentContext null_queue(h.owner, 0x66, 1);
    auto failed = null_queue.run(h.inputs);
    check(!failed && h.owner.pending_208 == 0x66U &&
              lock_available_elsewhere(h.owner.mutex_1a8) &&
              !null_queue.run(h.inputs, 0),
          "Known NULL queue faults after208 prefix and releases owner lock");
    for (auto count : {63U, 65U, 0xffffffffU}) {
        VmfNotificationDeferredQueue queue(count, {});
        h.owner.pending_208 = 0x80000000U;
        h.inputs.globals.queue_10e7238 = &queue;
        VmfNotificationParentContext queued(h.owner, count, 1);
        const auto result = queued.run(h.inputs);
        if (count == 0xffffffffU)
            check(!result && result.error().queue_error &&
                      result.error().queue_error->code ==
                          VmfNotificationDeferredErrorCode::invalid_count,
                  "Only an encountered invalid insertion index faults");
        else
            check(
                result && result->status == Status::finished &&
                    result->state.result == 0U,
                "Full guard including count65 returnsAL0 without index error");
        check(h.owner.pending_208 == count && queue.count_314 == count &&
                  lock_available_elsewhere(h.owner.mutex_1a8),
              "Full/error queues preserve208 prefix and release both locks");
    }
    h.owner.pending_208.reset();
    {
        VmfNotificationParentContext abandoned(h.owner, 8, 1);
        check(take(abandoned.run(h.inputs)).status ==
                      Status::needs_pending_208 &&
                  !lock_available_elsewhere(h.owner.mutex_1a8),
              "Pending-field wait also retains owner lock");
    }
    check(lock_available_elsewhere(h.owner.mutex_1a8),
          "Destruction on owning thread releases a suspended deferred lock");
}
void child_bindings_and_live_loops() {
    Harness h;
    auto value = child();
    std::array<VmfNotificationChildSlot, 1> slots{&value};
    h.state.count_0c = 1;
    h.state.children_08 = std::span{slots};
    Evaluator evaluator;
    VmfNotificationParentContext context(h.owner, 7, 0);
    auto report = take(context.run(h.inputs));
    check(report.status == Status::child_pending &&
              report.state.children_visited == 1U,
          "Unknown evaluator waits inside the already-created child");
    h.owner.flag_3539.reset();
    h.inputs.child_evaluator = &evaluator;
    report = finish(context, h.inputs, 1);
    check(report.state.result == 1U && evaluator.calls == 1U &&
              report.state.children_invoked == 1U &&
              report.state.first_pass_iterations == 1U,
          "Captured3539 survives wait; actual child supplies OR byte1");
    Harness separate;
    auto active = child();
    active.id_low_30.reset();
    active.id_high_34.reset();
    std::array<VmfNotificationChildSlot, 1> active_slots{&active};
    separate.state.count_0c = 1;
    separate.state.children_08 = std::span{active_slots};
    evaluator.value = 0;
    separate.inputs.child_evaluator = &evaluator;
    VmfNotificationParentContext distinct(separate.owner, 7, 0);
    report = take(distinct.run(separate.inputs));
    check(report.status == Status::child_pending,
          "Unknown16A2D2F is not replaced by known parent3539");
    separate.inputs.flag_16a2d2f = std::uint8_t{0};
    report = finish(distinct, separate.inputs);
    check(report.state.result == 0U && active.active_08 == std::uint8_t{1},
          "Independent zero global skips unknown IDs and unavailable effects");

    Harness unknown;
    std::array<VmfNotificationChildSlot, 1> unknown_slots{nullptr};
    unknown.state.count_0c = 1;
    unknown.state.children_08 = std::span{unknown_slots};
    VmfNotificationParentContext waiting(unknown.owner, 7, 0);
    to_phase(waiting, unknown.inputs, Phase::first_pass_init);
    unknown_slots[0].reset();
    report = take(waiting.run(unknown.inputs));
    check(report.status == Status::needs_child &&
              report.state.children_visited == 0U,
          "Unknown parent slot waits without inventing a NULL visit");
    unknown.state.count_0c = 0;
    unknown_slots[0] = nullptr;
    report = finish(waiting, unknown.inputs);
    check(report.state.children_visited == 1U &&
              report.state.first_pass_iterations == 1U,
          "Known NULL skips invocation and still reads live count afterward");

    Harness live;
    std::array<VmfNotificationChildSlot, 2> before{nullptr, nullptr};
    auto later = child();
    std::array<VmfNotificationChildSlot, 2> after{nullptr, &later};
    live.state.count_0c = 2;
    live.state.children_08 = std::span{before};
    evaluator.value = 1;
    live.inputs.child_evaluator = &evaluator;
    VmfNotificationParentContext iteration(live.owner, 7, 0);
    to_phase(iteration, live.inputs, Phase::first_pass_slot_advance);
    take(iteration.run(live.inputs, 1)); // Read count2, INC, signed compare.
    live.state.children_08.reset();
    check(take(iteration.run(live.inputs)).status == Status::needs_children,
          "Array base is requested anew at the next live slot");
    live.state.count_0c = 0;
    live.state.children_08 = std::span{after};
    report = finish(iteration, live.inputs);
    check(
        report.state.result == 1U && later.active_08 == std::uint8_t{0} &&
            report.state.first_pass_iterations == 2U,
        "Captured loop count survives base wait; replacement slot is invoked");
}
void transition_and_real_toggle(Profile profile) {
    Harness h;
    auto first = child(1, 2), second = child(1, 8);
    first.weight_14 = 50;
    std::array<VmfNotificationChildSlot, 2> slots{&first, &second};
    h.state.count_0c = 2;
    h.state.children_08 = std::span{slots};
    h.state.thresholds[4] = 40;
    auto display = take(
        VmfDisplayRuntime::create_with_variables(tdur::test::program(profile)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationToggleOwner toggle_owner{0xff};
    VmfNotificationToggleInputs toggle_inputs{std::span{movies}, {}};
    VmfNotificationToggleFrame toggle_frame(toggle_owner, toggle_inputs);
    VmfNotificationParentToggleService toggle(toggle_frame);
    Host initial_host, later_host;
    VmfNotificationState different;
    different.count_0c = 0;
    different.thresholds.fill(1U);
    different.thresholds[1] = 0;
    later_host.state = &different; // Actual889230 computes2, not previous5.
    Evaluator evaluator;
    Effects effects;
    h.inputs.child_evaluator = &evaluator;
    h.inputs.child_effects = &effects;
    h.inputs.service_8986b0 = &toggle;
    h.inputs.globals.host = &initial_host;
    initial_host.callback = [&] {
        h.inputs.globals.host = &later_host;
        h.owner.flag_3539 = std::uint8_t{0};
    };
    VmfNotificationParentContext context(h.owner, 2, 0);
    auto report = take(context.run(h.inputs));
    check(report.status == Status::service_8986b0_pending &&
              toggle_owner.flag_fa == 0U &&
              initial_host.progression_calls == 0U,
          "Actual8986B0 prefix completes before unresolved expression wait");
    h.inputs.service_8986b0.reset();
    toggle_inputs.expression_f0a8f8 = "/:parent_toggle";
    report = finish(context, h.inputs);
    const auto variable =
        take(display->variable_dictionary()->find(0, "parent_toggle"));
    check(report.state.initial_level == 0U &&
              report.state.recalculated_level == 5U &&
              report.state.service_8986b0_calls == 1U &&
              !toggle_frame.snapshot() && variable &&
              std::string_view(variable->value.text.data()) == "0",
          "Real toggle retained, polled, published and released once");
    check(initial_host.progression_calls == 1U &&
              initial_host.state_calls == 0U &&
              later_host.progression_calls == 0U &&
              later_host.state_calls == 1U &&
              initial_host.progression.destination.level_f028 == 2U &&
              !later_host.progression.destination.level_f028,
          "One +50 object receives actual889230 recomputation from new "
          "global+4C");
    check(h.owner.field_10 == std::uint8_t{1} &&
              report.state.second_pass_iterations == 2U &&
              effects.calls == 1U &&
              effects.trace[0] == VmfNotificationChildEffect::call_89db30 &&
              second.active_08 == std::uint8_t{0} && report.state.result == 1U,
          "Late3539 store, actual second mode8 child and OR are preserved");
    auto* counter = take(initial_host.notification_counter());
    check(counter == &initial_host.progression.counter,
          "Assignment projection returns counter of the same +50 progression");
}
void progression_failures() {
    for (bool null_result : {false, true}) {
        Harness h;
        auto value = child();
        value.weight_14 = 5;
        std::array<VmfNotificationChildSlot, 1> slots{&value};
        h.state.count_0c = 1;
        h.state.children_08 = std::span{slots};
        h.state.thresholds[4] = 2;
        Evaluator evaluator;
        Host host;
        host.fail_progression = !null_result;
        host.null_progression = null_result;
        VmfNotificationToggleOwner toggle_owner;
        VmfNotificationToggleInputs toggle_inputs{
            std::span<const VmfMenuMovie>{}, {}};
        VmfNotificationToggleFrame toggle_frame(toggle_owner, toggle_inputs);
        VmfNotificationParentToggleService toggle(toggle_frame);
        h.inputs.service_8986b0 = &toggle;
        h.inputs.child_evaluator = &evaluator;
        h.inputs.globals.host = &host;
        VmfNotificationParentContext context(h.owner, 7, 0);
        std::optional<VmfNotificationParentError> failure;
        for (unsigned attempts = 0; attempts < 1024 && !failure; ++attempts) {
            auto result = context.run(h.inputs);
            if (!result)
                failure = result.error();
        }
        check(failure.has_value() && host.progression_calls == 1U &&
                  !toggle_frame.snapshot() && !context.run(h.inputs),
              "Failed/NULL progression faults after actual toggle cleanup");
        if (!null_result)
            check(failure->code ==
                          VmfNotificationParentErrorCode::store_error &&
                      failure->state_error &&
                      failure->state_error->code ==
                          VmfNotificationStateErrorCode::host_failure,
                  "Getter's typed error survives parent propagation");
        check(
            !host.notification_counter(),
            "Counter projection does not fabricate a NULL/failed progression");
    }
}
void put32(std::array<std::byte, 16>& bytes, std::size_t at,
           std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = std::byte((value >> (8U * i)) & 255U);
}
void final_catalog_capture(Profile profile) {
    Harness h;
    auto display = take(
        VmfDisplayRuntime::create_with_variables(tdur::test::program(profile)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationWrapperOwner wrapper;
    wrapper.selector_4820 = 0;
    constexpr char text[] = "Parent notification";
    const auto linked = take(VmfLinkedText::copy_storage(std::span{text}));
    const auto reference = *VmfValue::linked(linked).text_reference();
    std::array<std::byte, 16> rows{};
    put32(rows, 0, 0x037bf702);
    std::array catalogs{VmfCatalogBinding{
        0x48,
        {1, std::span<const std::byte>{rows},
         std::span<const char>{reference.c_str(), reference.extent_bytes()}}}};
    std::array pools{VmfNotificationCatalogPoolBinding{0x48, reference}};
    h.inputs.globals.named_inputs.emplace();
    VmfNotificationFormatServices services(*h.inputs.globals.named_inputs);
    auto formatter = take(VmfNotificationFormatter::create(services));
    h.inputs.globals.movies = std::span{movies};
    h.inputs.globals.owner_10e7eec = &wrapper;
    VmfNotificationParentContext context(h.owner, 7, 0);
    check(take(context.run(h.inputs)).status == Status::needs_name,
          "UI selected actual display and owner before name wait");
    movies[0].display = nullptr;
    h.inputs.globals.movies.reset();
    h.inputs.globals.owner_10e7eec.reset();
    h.inputs.globals.name_f16338 = "/:parent_text";
    h.inputs.globals.named_inputs->catalogs = std::span{catalogs};
    h.inputs.globals.catalog_arguments.emplace();
    h.inputs.globals.catalog_inputs.emplace();
    auto report = take(context.run(h.inputs));
    check(report.status == Status::catalog_display_pending &&
              report.state.catalog_display_calls == 1U,
          "Actual718120 reaches pool wait after its real catalog lookup");
    put32(rows, 0, 0xdeadbeef);
    wrapper.selector_4820 = 9;
    h.inputs.globals.named_inputs->catalogs =
        std::span<const VmfCatalogBinding>{};
    h.inputs.globals.catalog_inputs->pools = std::span{pools};
    h.inputs.globals.catalog_inputs->display.formatter = &formatter;
    report = finish(context, h.inputs, 1);
    const auto found =
        take(display->variable_dictionary()->find(0, "parent_text"));
    check(report.state.result == 0U &&
              report.state.catalog_display_calls == 1U && found &&
              found->value.kind == VmfVariableKind::linked_text &&
              found->value.linked->text() == text,
          "Captured UI pointers and catalog match survive all binding changes");
}
void pool_validation_and_cleanup() {
    Harness h, other;
    using Pool = VmfNotificationParentServicePool;
    std::array binding{Pool::Binding{&h.state, &h.owner}};
    for (auto capacity : {0U, 33U}) {
        bool threw{};
        try {
            Pool invalid(h.inputs, binding, capacity);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "Pool capacity outside1..32 is rejected at preparation");
    }
    for (auto invalid_binding :
         {Pool::Binding{nullptr, &h.owner}, Pool::Binding{&h.state, nullptr},
          Pool::Binding{&other.state, &h.owner}}) {
        bool threw{};
        try {
            std::array invalid{invalid_binding};
            Pool pool(h.inputs, invalid);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "NULL or mismatched registry identity is rejected");
    }
    bool duplicate_rejected{};
    try {
        std::array duplicates{binding[0], binding[0]};
        Pool invalid(h.inputs, duplicates);
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    check(duplicate_rejected,
          "Duplicate authoritative state identity rejected");
    Pool pool(h.inputs, binding, 2);
    binding[0] = {&other.state, &other.owner};
    auto* first = take(pool.begin_parent(h.state, 7, 1));
    auto* second = take(pool.begin_parent(h.state, 7, 1));
    check(
        first != second && pool.active_leases() == 2U &&
            !pool.begin_parent(h.state, 7, 1) &&
            !pool.begin_parent(other.state, 7, 1),
        "Owned registry and independent recursive leases survive caller edits");
    check(take(first->run(0)) == VmfNotificationEffectStatus::pending &&
              h.owner.pending_208 == 0x80000000U,
          "Zero-budget leased parent does not start effects");
    check(take(first->run(100)) == VmfNotificationEffectStatus::pending &&
              !lock_available_elsewhere(h.owner.mutex_1a8),
          "Leased parent retains real deferred lock across queue wait");
    first->release();
    check(pool.active_leases() == 1U &&
              lock_available_elsewhere(h.owner.mutex_1a8),
          "Frame cancellation destroys actual invocation and releases lock");
    second->release();
    VmfNotificationParentFrame frame(h.inputs);
    check(!frame.run(1), "Unleased frame rejects execution");
    h.inputs.globals.queue_10e7238 = nullptr;
    take(frame.begin(h.owner, 77, 1));
    check(!frame.begin(h.owner, 3, 0) && !frame.run(100) && frame.last_error(),
          "Exclusive lease rejects overwrite and retains real typed failure");
    frame.release();
    check(!frame.snapshot() && frame.last_error() && !frame.run(1),
          "Release preserves diagnostics without permitting replay");
    take(frame.begin(h.owner, 5, 0));
    check(!frame.last_error(), "New invocation clears previous diagnostics");
    frame.release();
}
void dispatch_composition_and_cancellation() {
    Harness h;
    const tdur::crypto::XteaKey key{11, 22, 33, 44};
    std::array<VmfNotificationResetRecord, 640> records;
    for (auto& record : records)
        record.encoded_identity_70 =
            tdur::crypto::encrypt_xtea_words({0, 0}, key);
    VmfNotificationIdentityOwner identities{records, key};
    constexpr std::string_view name = "parent_fixture";
    std::array<VmfNotificationIdentityCatalogRow, 1> rows;
    rows[0].name_08 = std::optional{name};
    rows[0].quota_28 = 0;
    VmfNotificationIdentityCatalog catalog{1U, 0x1000U, std::span{rows}};
    Host host;
    host.identities = &identities;
    host.state = &h.state;
    std::array<tdur::crypto::XteaWords, 1> counters{
        tdur::crypto::encrypt_xtea_words({0x12345678U, 41U}, key)};
    host.progression.counter.address = 0U;
    host.progression.counter.encryption_base_18 = 0U;
    host.progression.counter.region =
        VmfNotificationCounterRegion{0xf120U, counters};
    std::array bindings{
        VmfNotificationParentServicePool::Binding{&h.state, &h.owner}};
    VmfNotificationParentServicePool pool(h.inputs, bindings, 2);
    VmfNotificationDispatchInputs inputs;
    inputs.globals.host = &host;
    inputs.globals.name_f161d0 = name;
    inputs.globals.hash_mode = std::uint8_t{0};
    inputs.catalog = &catalog;
    inputs.parent = &pool;
    auto value = child();
    value.id_low_30 = 0x5c1fe0;
    VmfNotificationDispatchContext dispatch(value);
    auto report = take(dispatch.run(inputs));
    check(report.status == VmfNotificationDispatchStatus::parent_pending &&
              pool.active_leases() == 1U && host.identity_calls == 2U &&
              host.state_calls == 1U && host.progression_calls == 1U &&
              tdur::crypto::decrypt_xtea_words(counters[0], key) ==
                  tdur::crypto::XteaWords{0x12345678U, 42U},
          "Real dispatch/assignment acquires a real parent lease, not a mock");
    h.inputs.globals.movies.reset();
    for (unsigned attempt = 0; attempt < 1024 && !h.owner.field_2d4.has_value();
         ++attempt)
        report = take(dispatch.run(inputs));
    check(h.owner.field_2d4 == 0xffffffffU &&
              h.owner.pending_208 == 0x80000000U && pool.active_leases() == 1U,
          "Actual parent receives synchronous mode7 and executes its prefix");
    inputs.parent.reset();
    inputs.globals.host.reset();
    h.inputs.globals.movies = std::span<const VmfMenuMovie>{};
    for (unsigned attempt = 0;
         attempt < 1024 &&
         report.status != VmfNotificationDispatchStatus::finished;
         ++attempt)
        report = take(dispatch.run(inputs));
    check(report.status == VmfNotificationDispatchStatus::finished &&
              report.state.result == std::uint8_t{1} &&
              report.state.parent_begins == 1U && pool.active_leases() == 0U &&
              host.identity_calls == 2U && host.state_calls == 1U,
          "Dispatch retains real parent through waits and releases it once");

    // Abandon an actual parent while it owns the actual8986B0 frame.
    auto display = take(VmfDisplayRuntime::create_with_variables(
        tdur::test::program(Profile::pc_2_8)));
    std::array movies{VmfMenuMovie{true, 1, 1, display.get()}};
    VmfNotificationToggleOwner toggle_owner;
    VmfNotificationToggleInputs toggle_inputs{std::span{movies}, {}};
    VmfNotificationToggleFrame toggle_frame(toggle_owner, toggle_inputs);
    VmfNotificationParentToggleService toggle(toggle_frame);
    auto changing = child();
    changing.weight_14 = 5;
    std::array<VmfNotificationChildSlot, 1> slots{&changing};
    h.state.count_0c = 1;
    h.state.children_08 = std::span{slots};
    h.state.thresholds[4] = 2;
    Evaluator evaluator;
    h.inputs.child_evaluator = &evaluator;
    h.inputs.service_8986b0 = &toggle;
    VmfNotificationParentFrame frame(h.inputs);
    take(frame.begin(h.owner, 7, 0));
    check(take(frame.run(4096)) == VmfNotificationEffectStatus::pending &&
              toggle_frame.snapshot(),
          "Parent reaches actual toggle wait with a live nested lease");
    frame.release();
    check(!frame.snapshot() && !toggle_frame.snapshot() &&
              toggle_owner.flag_fa == 0,
          "Parent cancellation releases nested toggle and preserves prefix");
    take(toggle_frame.begin(1));
    toggle_frame.release();
}
} // namespace
int main() {
    try {
        scenario = "guards_and_budgets";
        guards_and_budgets();
        scenario = "deferred_prefixes_and_capture";
        deferred_prefixes_and_capture();
        scenario = "child_bindings_and_live_loops";
        child_bindings_and_live_loops();
        scenario = "progression_failures";
        progression_failures();
        scenario = "pool_validation_and_cleanup";
        pool_validation_and_cleanup();
        scenario = "dispatch_composition_and_cancellation";
        dispatch_composition_and_cancellation();
        for (auto profile : {Profile::pc_2_8, Profile::xbox_2_5}) {
            scenario = "transition_and_real_toggle";
            transition_and_real_toggle(profile);
            scenario = "final_catalog_capture";
            final_catalog_capture(profile);
        }
        std::cout << checks << " notification-parent checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << scenario << ": " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

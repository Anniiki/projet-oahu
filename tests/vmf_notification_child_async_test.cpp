#include "tdur/ui/vmf_notification_child.hpp"

#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace tdur::ui;
using Context = VmfNotificationChildContext;
using Effect = VmfNotificationChildEffect;
using Status = VmfNotificationChildStatus;
using Error = VmfNotificationStateErrorCode;
std::size_t checks{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value)
        throw std::runtime_error(detail);
}
VmfNotificationStateError failure() {
    return {Error::host_failure, "Declared synthetic callee failure"};
}
struct Evaluator final : VmfNotificationChildEvaluator {
    std::size_t calls{};
    std::expected<std::uint32_t, VmfNotificationStateError>
    evaluate(VmfNotificationChildState&,
             std::uint32_t* output) noexcept override {
        ++calls;
        return output ? 0U : 10U;
    }
};
struct Frame final : VmfNotificationChildEffectFrame {
    std::size_t pending{}, polls{}, releases{}, last_budget{};
    bool fail{};
    std::function<void()> on_poll, on_finish, on_release;
    std::expected<VmfNotificationEffectStatus, VmfNotificationStateError>
    run(std::size_t budget) noexcept override {
        ++polls;
        last_budget = budget;
        if (on_poll)
            on_poll();
        if (fail)
            return std::unexpected(failure());
        if (pending) {
            --pending;
            return VmfNotificationEffectStatus::pending;
        }
        if (on_finish)
            on_finish();
        return VmfNotificationEffectStatus::finished;
    }
    void release() noexcept override {
        ++releases;
        if (on_release)
            on_release();
    }
};
struct Module final : VmfNotificationChildEffects {
    std::array<Frame, 3> frames;
    std::array<Effect, 8> trace{};
    std::array<bool, 3> immediate{}, fail_begin{};
    std::size_t calls{};
    std::uint8_t active_at_begin{};
    std::expected<VmfNotificationChildEffectFrame*, VmfNotificationStateError>
    begin_effect(Effect effect,
                 VmfNotificationChildState& child) noexcept override {
        trace.at(calls++) = effect;
        active_at_begin = child.active_08.value_or(std::uint8_t{0xff});
        const auto index = static_cast<std::size_t>(effect);
        if (fail_begin[index])
            return std::unexpected(failure());
        if (immediate[index])
            return nullptr;
        return &frames[index];
    }
    Frame& first() { return frames[0]; }
};
struct Fixture {
    // Destruction order is part of the protocol: context dies FIRST, while
    // its module, frame, arguments and child are still alive.
    VmfNotificationChildState child{std::uint8_t{1}, 10U,    7U, 20U,
                                    0x1234U,         0x5678U};
    Evaluator evaluator;
    Module module;
    VmfNotificationChildInputs inputs{&evaluator, std::uint8_t{1}, &module};
    Context context;
    explicit Fixture(std::uint8_t incoming = 0)
        : context(Context::create(child, 7, incoming)) {}
};
auto advance(Context& context, const VmfNotificationChildInputs& inputs,
             std::size_t budget = 4096) {
    const auto result = context.run(inputs, budget);
    check(result.has_value(), "Required child progression failed");
    return *result;
}
auto advance(Fixture& fixture, std::size_t budget = 4096) {
    return advance(fixture.context, fixture.inputs, budget);
}
bool invalid(Context& context, const VmfNotificationChildInputs& inputs) {
    const auto result = context.run(inputs);
    return !result && result.error().code == Error::invalid_context;
}
void ordering_and_capture() {
    Module replacement;
    Fixture f;
    f.module.first().pending = 2;
    auto report = advance(f);
    check(report.status == Status::effect_pending &&
              report.state.completed_effects == 0 &&
              report.state.completed_evaluations == 1 &&
              report.state.active_stores == 1 && f.module.active_at_begin == 1,
          "Actual evaluation/store precedes a pending effect, not completion");
    check(f.module.calls == 1 && f.module.first().polls == 1 &&
              f.module.frames[1].polls == 0,
          "89BF80 cannot start while89E330 is suspended");
    f.inputs.effects = &replacement;
    f.child.id_low_30 = 0x517180;
    f.child.id_high_34 = 0;
    report = advance(f);
    check(report.status == Status::effect_pending && f.module.calls == 1 &&
              f.module.first().polls == 2 && replacement.calls == 0,
          "Pending frame and selected effect/module survive input rebind");
    report = advance(f);
    check(report.status == Status::finished && report.state.result == 0 &&
              report.state.completed_effects == 2 && f.evaluator.calls == 1 &&
              f.module.calls == 2 && replacement.calls == 0,
          "Resume finishes the captured effect before its actual successor");
    check(f.module.trace[0] == Effect::call_89e330 &&
              f.module.trace[1] == Effect::call_89bf80 &&
              f.module.first().releases == 1 &&
              f.module.frames[1].releases == 1,
          "Effects retain native order and each lease is released once");
    check(advance(f).state.completed_effects == 2 && f.module.calls == 2,
          "Finished invocation cannot replay any effect");
}
void branch_results() {
    {
        Fixture f{1};
        f.module.first().pending = 1;
        f.module.first().on_finish = [&] { f.child.active_08.reset(); };
        check(advance(f).status == Status::effect_pending,
              "Nonzero incoming flag can suspend89E330");
        const auto done = advance(f);
        check(done.status == Status::finished && done.state.result == 0 &&
                  f.module.calls == 1 && f.module.frames[1].polls == 0,
              "Nonzero incoming skips89BF80 and the final active reread");
    }
    {
        Fixture f;
        f.child.id_low_30 = 0x517180;
        f.child.id_high_34 = 0;
        auto& frame = f.module.frames[2];
        frame.pending = 1;
        frame.on_finish = [&] { f.child.active_08 = std::uint8_t{0}; };
        check(advance(f).status == Status::effect_pending,
              "Sentinel89DB30 may suspend");
        const auto done = advance(f);
        check(done.state.result == 1 && f.module.calls == 1 &&
                  f.module.trace[0] == Effect::call_89db30 &&
                  frame.releases == 1,
              "89DB30 completes before the late actual active-field reread");
    }
    for (bool inactive : {false, true}) {
        Fixture f;
        if (inactive)
            f.child.active_08 = std::uint8_t{0};
        else
            f.child.mask_0c = 8;
        check(advance(f).state.result == 0 && f.module.calls == 0 &&
                  f.evaluator.calls == 0,
              "Native early guards do not request effect frames");
    }
}
void failures_and_bindings() {
    for (bool begin_failure : {false, true}) {
        Fixture f;
        f.module.fail_begin[0] = begin_failure;
        f.module.first().fail = !begin_failure;
        f.module.first().on_poll = [&] { f.child.weight_14 = 99U; };
        const auto result = f.context.run(f.inputs);
        check(!result && result.error().code == Error::host_failure &&
                  f.context.snapshot().faulted &&
                  f.context.snapshot().active_stores == 1 &&
                  f.context.snapshot().completed_effects == 0,
              "Begin/poll error preserves prefix and never completes effect");
        check(f.module.first().releases == (begin_failure ? 0U : 1U) &&
                  f.module.calls == 1 && invalid(f.context, f.inputs),
              "Error releases only acquired lease and prevents replay");
        if (!begin_failure)
            check(f.child.weight_14 == 99U,
                  "Failed effect's prefix is retained without rollback");
    }
    {
        Fixture f;
        f.inputs.effects.reset();
        check(advance(f).status == Status::needs_effects &&
                  f.context.snapshot().active_stores == 1 &&
                  f.module.calls == 0,
              "Unknown module is a wait after the native active store");
        f.inputs.effects = nullptr;
        const auto result = f.context.run(f.inputs);
        check(!result && result.error().code == Error::invalid_native_binding,
              "Confirmed NULL module is not a completed effect");
    }
    {
        VmfNotificationChildEffects unimplemented;
        Fixture f;
        f.inputs.effects = &unimplemented;
        const auto result = f.context.run(f.inputs);
        check(!result && f.context.snapshot().completed_effects == 0,
              "Default unimplemented invoke refuses rather than succeeding");
    }
}
void budgets_and_cleanup() {
    Fixture f;
    f.module.first().pending = 100;
    auto zero = advance(f, 0);
    check(zero.status == Status::step_limit && zero.steps == 0 &&
              f.evaluator.calls == 0 && f.module.calls == 0,
          "Zero root budget calls no service");
    check(advance(f, 1'000'000).status == Status::effect_pending &&
              f.module.first().polls == 1 && f.module.first().last_budget == 1,
          "One pending poll yields immediately regardless of root budget");
    zero = advance(f, 0);
    check(zero.steps == 0 && f.module.first().polls == 1 && f.module.calls == 1,
          "Zero budget while pending neither polls nor begins again");
    {
        auto moved = std::move(f.context);
        check(invalid(f.context, f.inputs) && f.module.first().releases == 0,
              "Move transfers pending lease and invalidates only the source");
        check(advance(moved, f.inputs).status == Status::effect_pending &&
                  f.module.calls == 1 && f.module.first().polls == 2,
              "Moved continuation resumes without replaying begin");
        bool refused{};
        f.module.first().on_release = [&] {
            refused = invalid(moved, f.inputs);
        };
        // The callback target remains live through its destructor body.
        f.module.first().on_poll = {};
        f.module.first().on_finish = {};
        // Check destruction immediately using a local optional, with a stable
        // counter outside the context and with all backing objects still live.
        std::optional<Context> final;
        final.emplace(std::move(moved));
        auto* final_context = &*final;
        f.module.first().on_release = [&] {
            refused = invalid(*final_context, f.inputs);
        };
        final.reset();
        check(refused && f.module.first().releases == 1,
              "Abandonment detaches lease and refuses reentry during cleanup");
        f.module.first().on_release = {};
    }
    check(f.module.first().releases == 1,
          "Destroyed moved-from contexts cannot release the transferred lease");
}
void assignments_and_reentry() {
    Fixture target;
    Fixture source;
    target.module.first().pending = source.module.first().pending = 10;
    check(advance(target).status == Status::effect_pending &&
              advance(source).status == Status::effect_pending,
          "Independent invocations own independent pending leases");
    bool target_refused{}, source_refused{};
    target.module.first().on_release = [&] {
        target_refused = invalid(target.context, target.inputs);
        source_refused = invalid(source.context, source.inputs);
    };
    target.context = std::move(source.context);
    check(
        target.module.first().releases == 1 &&
            source.module.first().releases == 0 && target_refused &&
            source_refused && invalid(source.context, source.inputs),
        "Assignment protects both contexts while replacing exactly one lease");
    check(advance(target.context, source.inputs).status ==
                  Status::effect_pending &&
              source.module.first().polls == 2 && source.module.calls == 1,
          "Assigned continuation resumes the source's frame and captures");
    // target's context now borrows source.module: abandon it before source's
    // backing is destroyed (source was declared after target).
    target.context = Context::create(target.child, 7, 0);
    check(source.module.first().releases == 1,
          "Overwriting a transferred continuation releases its actual module");
    target.module.first().on_release = {};

    Fixture active{1};
    Fixture spare;
    bool run_refused{}, move_refused{}, assignment_refused{};
    active.module.first().pending = 1;
    active.module.first().on_poll = [&] {
        run_refused = invalid(active.context, active.inputs);
        auto attempted = std::move(active.context);
        move_refused = invalid(attempted, active.inputs);
        active.context = std::move(spare.context);
        assignment_refused = spare.context.snapshot().steps == 0;
    };
    check(advance(active).status == Status::effect_pending && run_refused &&
              move_refused && assignment_refused && active.module.calls == 1,
          "Callee cannot reenter or move a running invocation");
    active.module.first().on_poll = {};
    check(advance(active).status == Status::finished &&
              active.module.first().releases == 1,
          "Rejected reentry/moves leave original invocation able to finish");
}
void synchronous_adapter() {
    struct Legacy final : VmfNotificationChildEffects {
        std::size_t calls{};
        std::expected<void, VmfNotificationStateError>
        invoke(Effect, VmfNotificationChildState& child) noexcept override {
            ++calls;
            child.weight_14 = 42U;
            return {};
        }
    } module;
    Fixture f;
    f.inputs.effects = &module;
    const auto done = advance(f);
    check(done.status == Status::finished &&
              done.state.completed_effects == 2 && module.calls == 2 &&
              f.child.weight_14 == 42U,
          "Default begin adapter executes each real legacy invoke once");
}
} // namespace
int main() {
    try {
        ordering_and_capture();
        branch_results();
        failures_and_bindings();
        budgets_and_cleanup();
        assignments_and_reentry();
        synchronous_adapter();
        std::cout << checks << " child-continuation checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

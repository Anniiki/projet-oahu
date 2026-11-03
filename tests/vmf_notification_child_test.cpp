#include "tdur/ui/vmf_notification_child.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
} // namespace allocation_fixture
void* operator new(std::size_t size) {
    if (allocation_fixture::enabled)
        ++allocation_fixture::calls;
    if (auto* value = std::malloc(size == 0 ? 1 : size))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::ui;
using Phase = VmfNotificationChildPhase;
using Status = VmfNotificationChildStatus;
using Effect = VmfNotificationChildEffect;
using Error = VmfNotificationStateErrorCode;
std::size_t checks{}, failures{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << detail << '\n';
    }
}
auto raw(VmfNotificationChildContext& context,
         const VmfNotificationChildInputs& inputs, std::size_t budget = 4096) {
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    auto result = context.run(inputs, budget);
    allocation_fixture::enabled = false;
    check(allocation_fixture::calls == 0,
          "no ordinary allocation in fixture run");
    return result;
}
template <class T> T take(std::expected<T, VmfNotificationStateError> value) {
    check(value.has_value(), "expected successful progression");
    if (!value)
        throw std::runtime_error(
            "required child notification operation failed");
    if constexpr (!std::is_void_v<T>)
        return std::move(*value);
}
auto run(VmfNotificationChildContext& context,
         const VmfNotificationChildInputs& inputs, std::size_t budget = 4096) {
    return take(raw(context, inputs, budget));
}
void to(VmfNotificationChildContext& context,
        const VmfNotificationChildInputs& inputs, Phase phase) {
    std::size_t iterations = 0;
    while (context.snapshot().phase != phase &&
           context.snapshot().phase != Phase::finished && iterations++ < 128)
        check(run(context, inputs, 1).status == Status::step_limit,
              "one-step progression reaches observation boundary");
    check(context.snapshot().phase == phase, "requested child phase");
}
struct Evaluator final : VmfNotificationChildEvaluator {
    std::uint32_t value = 5;
    std::size_t calls{};
    bool output_was_null{}, fail{}, clear_limit{}, reentry_refused{};
    std::optional<std::uint8_t> active_override;
    VmfNotificationChildContext* reenter{};
    std::expected<std::uint32_t, VmfNotificationStateError>
    evaluate(VmfNotificationChildState& child,
             std::uint32_t* output) noexcept override {
        ++calls;
        output_was_null = output == nullptr;
        if (clear_limit)
            child.limit_18.reset();
        if (active_override)
            child.active_08 = *active_override;
        if (reenter) {
            const auto nested = reenter->run({});
            reentry_refused =
                !nested && nested.error().code == Error::invalid_context;
        }
        if (fail)
            return std::unexpected(VmfNotificationStateError{
                Error::host_failure, "fixture evaluation failed"});
        return value;
    }
};
struct Effects final : VmfNotificationChildEffects {
    std::array<Effect, 4> calls{};
    std::size_t count{};
    std::optional<std::uint32_t> observed_low;
    std::optional<std::uint8_t> active_override;
    bool clear_active{}, fail{}, reentry_refused{};
    VmfNotificationChildContext* reenter{};
    std::expected<void, VmfNotificationStateError>
    invoke(Effect effect, VmfNotificationChildState& child) noexcept override {
        if (count < calls.size())
            calls[count] = effect;
        ++count;
        observed_low = child.id_low_30;
        if (active_override)
            child.active_08 = *active_override;
        if (clear_active)
            child.active_08.reset();
        if (reenter) {
            const auto nested = reenter->run({});
            reentry_refused =
                !nested && nested.error().code == Error::invalid_context;
        }
        if (fail)
            return std::unexpected(VmfNotificationStateError{
                Error::host_failure, "fixture effect failed"});
        return {};
    }
};
struct Fixture {
    VmfNotificationChildState child;
    Evaluator evaluator;
    Effects effects;
    VmfNotificationChildInputs inputs{&evaluator, std::uint8_t{0}, &effects};
    Fixture() {
        child.active_08 = std::uint8_t{1};
        child.weight_14 = 50U;
        child.mask_0c = 2U;
        child.limit_18 = 10U;
        child.id_low_30 = 0xffffffffU;
        child.id_high_34 = 0xffffffffU;
    }
    auto context(std::uint8_t incoming = 0, std::uint32_t mode = 7) {
        return VmfNotificationChildContext::create(child, mode, incoming);
    }
    void completed_generic() {
        evaluator.value = 10;
        child.id_low_30 = 0U;
        child.id_high_34 = 0U;
        inputs.flag_16a2d2f.reset();
    }
};
void initial_guards() {
    VmfNotificationChildState child;
    auto unknown = VmfNotificationChildContext::create(child, 0, 0);
    check(run(unknown, {}, 0).status == Status::step_limit &&
              unknown.snapshot().steps == 0,
          "zero budget reads no bindings");
    check(run(unknown, {}).status == Status::needs_active_flag,
          "unknown active does not become zero");
    child.active_08 = std::uint8_t{0};
    check(run(unknown, {}).state.result == std::uint8_t{0} &&
              unknown.snapshot().completed_evaluations == 0,
          "inactive guard skips all unknown services and fields");
    struct Sample {
        std::uint32_t mask, mode;
        bool pass;
    };
    for (auto sample :
         {Sample{0, 0, true}, Sample{2, 7, true}, Sample{7, 3, false},
          Sample{0xffffffff, 0xffffffff, true},
          Sample{0xffffffff, 0xfffffffe, false}}) {
        Fixture f;
        f.child.active_08 = std::uint8_t{255};
        f.child.mask_0c = sample.mask;
        auto c = f.context(0, sample.mode);
        check(run(c, f.inputs).status == Status::finished &&
                  f.evaluator.calls == (sample.pass ? 1U : 0U) &&
                  c.snapshot().active_stores == (sample.pass ? 1U : 0U),
              "full DWORD mask inclusion, including zero and high bits");
    }
    Fixture f;
    f.child.mask_0c.reset();
    auto c = f.context();
    check(run(c, {}).status == Status::needs_mask && f.evaluator.calls == 0,
          "mask precedes evaluator binding");
    f.child.mask_0c = 2U;
    check(run(c, {}).status == Status::needs_evaluator,
          "missing evaluator is not a successful zero result");
    VmfNotificationChildInputs invalid;
    invalid.evaluator = static_cast<VmfNotificationChildEvaluator*>(nullptr);
    const auto failure = raw(c, invalid);
    check(!failure && failure.error().code == Error::invalid_native_binding &&
              c.snapshot().active_stores == 0,
          "known NULL evaluator binding is refused before invocation");
}
void evaluation_and_prefix() {
    Fixture f;
    Evaluator replacement;
    replacement.value = 999;
    f.evaluator.clear_limit = true;
    f.evaluator.active_override = std::uint8_t{99};
    auto c = f.context();
    to(c, f.inputs, Phase::evaluate);
    auto changed = f.inputs;
    changed.evaluator = &replacement;
    check(run(c, changed).status == Status::needs_limit &&
              f.evaluator.calls == 1 && replacement.calls == 0 &&
              f.evaluator.output_was_null &&
              f.child.active_08 == std::uint8_t{99},
          "evaluator identity and effects retained before unknown limit");
    f.child.limit_18 = 6U;
    f.evaluator.value = 999;
    auto moved = std::move(c);
    check(!raw(c, {}), "move invalidates source child frame");
    check(
        run(moved, {}).status == Status::needs_global_flag &&
            f.child.active_08 == std::uint8_t{1} &&
            moved.snapshot().active_stores == 1 && f.evaluator.calls == 1,
        "retained return value is compared once and stored before global read");
    f.child.active_08 = std::uint8_t{0};
    VmfNotificationChildInputs global;
    global.flag_16a2d2f = std::uint8_t{0};
    check(run(moved, global).state.result == std::uint8_t{1},
          "computed branch survives wait but final active is reread");
    for (auto pair :
         {std::pair{10U, 10U}, std::pair{0x80000000U, 0x7fffffffU}}) {
        Fixture q;
        q.evaluator.value = pair.first;
        q.child.limit_18 = pair.second;
        q.inputs.flag_16a2d2f.reset();
        auto unsigned_compare = q.context(std::uint8_t{128});
        check(run(unsigned_compare, q.inputs).state.result == std::uint8_t{1} &&
                  q.child.active_08 == std::uint8_t{0} && q.effects.count == 0,
              "unsigned >= limit stores zero and bypasses unknown global");
    }
    Fixture bypass;
    bypass.child.id_low_30.reset();
    bypass.child.id_high_34.reset();
    bypass.inputs.effects.reset();
    auto early = bypass.context();
    check(
        run(early, bypass.inputs).state.result == std::uint8_t{0} &&
            early.snapshot().active_stores == 1,
        "computed nonzero with zero global returns without ID/effect binding");
}
void sentinel_and_generic_routes() {
    for (auto id : {std::pair{0xffffffffU, 0U}, std::pair{0x517180U, 1U}}) {
        Fixture f;
        f.completed_generic();
        f.child.id_low_30 = id.first;
        f.child.id_high_34 = id.second;
        auto c = f.context();
        check(run(c, f.inputs).state.result == std::uint8_t{0} &&
                  f.effects.count == 2 &&
                  f.effects.calls[0] == Effect::call_89e330 &&
                  f.effects.calls[1] == Effect::call_89bf80,
              "sentinel matching requires the complete 64-bit ID");
    }
    for (auto low : {0xffffffffU, 0x517180U}) {
        for (auto incoming :
             {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{128}}) {
            Fixture f;
            f.evaluator.value = 10;
            f.child.id_low_30 = low;
            f.child.id_high_34 = low == 0xffffffff ? 0xffffffffU : 0U;
            f.inputs.flag_16a2d2f.reset();
            auto c = f.context(incoming);
            const auto report = run(c, f.inputs);
            check(report.state.result == std::uint8_t{1} &&
                      f.effects.count == (incoming == 0 ? 1U : 0U) &&
                      (incoming != 0 ||
                       f.effects.calls[0] == Effect::call_89db30),
                  "both 64-bit sentinels route by the full incoming byte");
        }
    }
    for (auto incoming :
         {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{128}}) {
        Fixture f;
        f.completed_generic();
        f.effects.clear_active = true;
        auto c = f.context(incoming);
        check(
            run(c, f.inputs).state.result == std::uint8_t{0} &&
                f.effects.count == (incoming == 0 ? 2U : 1U) &&
                f.effects.calls[0] == Effect::call_89e330 &&
                (incoming != 0 || f.effects.calls[1] == Effect::call_89bf80) &&
                !f.child.active_08,
            "generic route ignores callee return and never rereads final "
            "active");
    }
    Fixture f;
    f.evaluator.value = 10;
    f.effects.active_override = std::uint8_t{128};
    auto reread = f.context();
    check(run(reread, f.inputs).state.result == std::uint8_t{0},
          "sentinel route rereads active changed by 89DB30");
    Fixture q;
    q.evaluator.value = 10;
    q.effects.clear_active = true;
    auto pending = q.context();
    check(run(pending, q.inputs).status == Status::needs_active_flag &&
              pending.snapshot().completed_effects == 1,
          "unknown final active waits after completed effect");
    q.child.active_08 = std::uint8_t{0};
    check(run(pending, {}).state.result == std::uint8_t{1} &&
              q.effects.count == 1,
          "final reread resumes without repeating 89DB30");
}
void retained_ids_and_module() {
    Fixture f;
    f.evaluator.value = 10;
    f.child.id_low_30 = 0x517180U;
    f.child.id_high_34.reset();
    auto c = f.context();
    check(run(c, f.inputs).status == Status::needs_id_high,
          "low ID captured before high ID binding");
    f.child.id_low_30 = 0U;
    f.child.id_high_34 = 0U;
    check(run(c, f.inputs).state.result == std::uint8_t{1} &&
              f.effects.calls[0] == Effect::call_89db30 &&
              f.effects.observed_low == 0U,
          "outer route uses captured low, callee receives live child data");
    Fixture q;
    q.completed_generic();
    q.inputs.effects.reset();
    auto selected = q.context();
    const auto missing = run(selected, q.inputs);
    check(missing.status == Status::needs_effects &&
              missing.state.pending_effect == Effect::call_89e330 &&
              missing.state.active_stores == 1,
          "missing module retains exact selected invocation after store");
    q.child.id_low_30 = 0x517180U;
    auto moved = std::move(selected);
    check(!raw(selected, {}), "pending effect move invalidates source");
    q.inputs.effects = &q.effects;
    to(moved, q.inputs, Phase::effect);
    Effects other;
    q.inputs.effects = &other;
    const auto done = run(moved, q.inputs);
    check(done.state.result == std::uint8_t{0} && q.evaluator.calls == 1 &&
              q.effects.count == 2 && other.count == 0 &&
              q.effects.calls[0] == Effect::call_89e330 &&
              q.effects.calls[1] == Effect::call_89bf80,
          "selected route and fixed module survive changed bindings and IDs");
    const auto steps = done.state.steps;
    q.child.active_08 = std::uint8_t{77};
    check(run(moved, {}).state.steps == steps &&
              q.child.active_08 == std::uint8_t{77} && q.effects.count == 2,
          "finished frame never replays its active store or services");
}
void faults_budgets_reentry() {
    Fixture f;
    f.evaluator.fail = true;
    f.evaluator.active_override = std::uint8_t{17};
    auto c = f.context();
    const auto error = raw(c, f.inputs);
    check(!error && error.error().code == Error::host_failure &&
              f.child.active_08 == std::uint8_t{17} &&
              c.snapshot().active_stores == 0 &&
              c.snapshot().completed_evaluations == 0,
          "failed evaluator preserves its own mutations, no invented later "
          "store");
    f.evaluator.fail = false;
    check(!raw(c, f.inputs) && f.evaluator.calls == 1,
          "evaluator fault is terminal without callback replay");
    Fixture q;
    q.completed_generic();
    q.effects.fail = true;
    q.effects.active_override = std::uint8_t{128};
    auto failed_effect = q.context();
    check(!raw(failed_effect, q.inputs) && q.effects.count == 1 &&
              failed_effect.snapshot().active_stores == 1 &&
              failed_effect.snapshot().completed_effects == 0 &&
              failed_effect.snapshot().pending_effect == Effect::call_89e330 &&
              q.child.active_08 == std::uint8_t{128},
          "effect error preserves both native prefix and callback mutation");
    check(!raw(failed_effect, q.inputs) && q.effects.count == 1,
          "failed effect never replays");
    Fixture n;
    n.completed_generic();
    n.inputs.effects = static_cast<VmfNotificationChildEffects*>(nullptr);
    auto null_module = n.context();
    const auto invalid = raw(null_module, n.inputs);
    check(!invalid && invalid.error().code == Error::invalid_native_binding &&
              null_module.snapshot().active_stores == 1 && n.effects.count == 0,
          "known NULL module is a fault after the active prefix");
    Fixture b;
    b.completed_generic();
    auto one = b.context();
    b.evaluator.reenter = &one;
    b.effects.reenter = &one;
    check(run(one, {}, 0).state.steps == 0 && b.evaluator.calls == 0,
          "zero budget calls nothing");
    VmfNotificationChildReport result{};
    std::size_t iterations = 0;
    do {
        result = run(one, b.inputs, 1);
        check(result.steps <= 1, "one-step budget never exceeded");
    } while (result.status == Status::step_limit && ++iterations < 128);
    check(result.status == Status::finished && b.evaluator.calls == 1 &&
              b.effects.count == 2 && b.evaluator.reentry_refused &&
              b.effects.reentry_refused && result.state.active_stores == 1,
          "real callbacks and same-frame reentry refusal survive budgeted "
          "execution");
}
void real_state_pipeline() {
    Fixture f;
    f.evaluator.value = 10;
    std::array<VmfNotificationChildSlot, 1> slots{&f.child};
    VmfNotificationState owner{1, std::span{slots}, {10U, 20U, 30U, 40U, 50U}};
    auto before = VmfNotificationStateContext::sum(owner);
    check(take(before.run()).state.result == 0U,
          "active child contributes zero before notification");
    auto child =
        f.context(std::uint8_t{1}); // Sentinel path needs no effect module.
    f.inputs.effects.reset();
    f.inputs.flag_16a2d2f.reset();
    check(
        run(child, f.inputs).state.result == std::uint8_t{1},
        "real child computation updates owned state without unknown services");
    auto after = VmfNotificationStateContext::sum(owner);
    check(take(after.run()).state.result == 50U,
          "actual existing sum consumes the changed active byte");
    struct Host final : VmfNotificationStateHost {
        VmfNotificationState* owner;
        std::size_t calls{};
        explicit Host(VmfNotificationState& state) : owner(&state) {}
        std::expected<VmfNotificationState*, VmfNotificationStateError>
        notification_state() noexcept override {
            ++calls;
            return owner;
        }
    } host(owner);
    VmfNotificationLevelDestination target{999U};
    auto store = VmfNotificationStoreContext::create(target);
    check(take(store.run(&host)).status ==
                  VmfNotificationStoreStatus::finished &&
              target.level_f028 == 5U && host.calls == 1 &&
              child.snapshot().completed_effects == 0,
          "real level and caller compose with child notification in owned "
          "pipeline");
}
} // namespace

int main() {
    try {
        initial_guards();
        evaluation_and_prefix();
        sentinel_and_generic_routes();
        retained_ids_and_module();
        faults_budgets_reentry();
        real_state_pipeline();
    } catch (const std::exception& error) {
        allocation_fixture::enabled = false;
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

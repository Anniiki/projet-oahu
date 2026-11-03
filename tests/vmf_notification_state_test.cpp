#include "tdur/ui/vmf_notification_state.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
} // namespace allocation_fixture
void* operator new(std::size_t size) {
    if (allocation_fixture::enabled)
        ++allocation_fixture::calls;
    if (auto* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {
using namespace tdur::ui;
using Phase = VmfNotificationStatePhase;
using Status = VmfNotificationStateStatus;
using StorePhase = VmfNotificationStorePhase;
using StoreStatus = VmfNotificationStoreStatus;
using Error = VmfNotificationStateErrorCode;
std::size_t checks{}, failures{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << detail << '\n';
    }
}
auto raw(VmfNotificationStateContext& context, std::size_t budget = 4096) {
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    auto result = context.run(budget);
    allocation_fixture::enabled = false;
    check(allocation_fixture::calls == 0,
          "no ordinary allocation in state run");
    return result;
}
auto raw(VmfNotificationStoreContext& context,
         std::optional<VmfNotificationStateHost*> host,
         std::size_t budget = 4096) {
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    auto result = context.run(host, budget);
    allocation_fixture::enabled = false;
    check(allocation_fixture::calls == 0,
          "no ordinary allocation in store run");
    return result;
}
template <class T> T take(std::expected<T, VmfNotificationStateError> result) {
    check(result.has_value(), "expected a successful binding/progression");
    if (!result)
        throw std::runtime_error("required notification operation failed");
    return std::move(*result);
}
auto run(VmfNotificationStateContext& context, std::size_t budget = 4096) {
    return take(raw(context, budget));
}
auto run(VmfNotificationStoreContext& context,
         std::optional<VmfNotificationStateHost*> host,
         std::size_t budget = 4096) {
    return take(raw(context, host, budget));
}
void to(VmfNotificationStateContext& context, Phase phase) {
    std::size_t tries = 0;
    while (context.snapshot().phase != phase &&
           context.snapshot().phase != Phase::finished && tries++ < 128)
        check(run(context, 1).status == Status::step_limit,
              "one-step progression reaches the requested boundary");
    check(context.snapshot().phase == phase, "requested state boundary");
}
struct Fixture {
    VmfNotificationChildState a{std::uint8_t{0}, 5U}, b{std::uint8_t{0}, 7U};
    std::array<VmfNotificationChildSlot, 3> slots{&a, &b, nullptr};
    VmfNotificationState owner{3, std::span{slots}, {10U, 20U, 30U, 40U, 50U}};
};
struct Host final : VmfNotificationStateHost {
    VmfNotificationState* owner{};
    std::size_t calls{};
    bool fail{}, reentry_refused{};
    VmfNotificationStoreContext* reenter{};
    std::expected<VmfNotificationState*, VmfNotificationStateError>
    notification_state() noexcept override {
        ++calls;
        if (reenter) {
            const auto nested = reenter->run(this);
            reentry_refused =
                !nested && nested.error().code == Error::invalid_context;
        }
        if (fail)
            return std::unexpected(VmfNotificationStateError{
                Error::host_failure, "fixture host failure"});
        return owner;
    }
};
void sum_guards_and_bindings() {
    VmfNotificationState unknown;
    auto c = VmfNotificationStateContext::sum(unknown);
    check(run(c, 0).status == Status::step_limit && c.snapshot().steps == 0,
          "zero budget touches no binding");
    check(run(c).status == Status::needs_count, "unknown count stays unknown");
    unknown.count_0c = 2;
    check(run(c).status == Status::needs_children &&
              c.snapshot().captured_count == 2,
          "count captured before requesting list");
    Fixture f;
    unknown.count_0c = -1;
    unknown.children_08 = std::span{f.slots};
    check(run(c).state.result == 12U && c.snapshot().slots_read == 2,
          "changing count after capture does not shorten the native loop");
    for (std::int32_t count :
         {0, -1, std::numeric_limits<std::int32_t>::min()}) {
        VmfNotificationState empty;
        empty.count_0c = count;
        auto sum = VmfNotificationStateContext::sum(empty);
        check(run(sum).state.result == 0U && sum.snapshot().slots_read == 0,
              "nonpositive count returns zero before array and thresholds");
        empty.thresholds[4] = 0U;
        auto level = VmfNotificationStateContext::level(empty);
        check(
            run(level).state.result == 5U &&
                level.snapshot().thresholds_read == 1,
            "zero sum still runs the level comparison; not forced level zero");
    }
    Fixture q;
    q.owner.count_0c = 1;
    q.slots[0].reset();
    auto fields = VmfNotificationStateContext::sum(q.owner);
    check(run(fields).status == Status::needs_child &&
              fields.snapshot().slots_read == 0,
          "unknown slot is not a known NULL slot");
    q.slots[0] = static_cast<VmfNotificationChildState*>(nullptr);
    check(run(fields).state.result == 0U && fields.snapshot().slots_read == 1,
          "known NULL child is skipped without field reads");
    for (auto active :
         {std::uint8_t{1}, std::uint8_t{128}, std::uint8_t{255}}) {
        Fixture skip;
        skip.a.active_08 = active;
        skip.a.weight_14.reset();
        auto sum = VmfNotificationStateContext::sum(skip.owner);
        check(run(sum).state.result == 7U && sum.snapshot().weights_read == 1,
              "every nonzero active byte skips unknown weight");
    }
}
void captures_moves_and_faults() {
    Fixture f;
    f.owner.count_0c = 1;
    f.a.active_08.reset();
    auto c = VmfNotificationStateContext::sum(f.owner);
    check(run(c).status == Status::needs_active_flag,
          "selected child awaits its active byte");
    f.slots[0] = &f.b;
    f.a.active_08 = std::uint8_t{0};
    f.a.weight_14.reset();
    check(run(c).status == Status::needs_weight,
          "selected first child remains retained after slot replacement");
    auto moved = std::move(c);
    check(!raw(c), "moved sum context is invalid");
    f.a.active_08 = std::uint8_t{255};
    f.a.weight_14 = 9U;
    check(run(moved).state.result == 9U && moved.snapshot().weights_read == 1,
          "active decision is not reread after waiting for weight");
    Fixture q;
    q.owner.count_0c = 2;
    auto list = VmfNotificationStateContext::sum(q.owner);
    to(list, Phase::child);
    VmfNotificationChildState alternative{std::uint8_t{0}, 999U};
    std::array<VmfNotificationChildSlot, 1> replacement{&alternative};
    q.owner.children_08 = std::span{replacement};
    q.owner.count_0c.reset();
    q.slots[1] = &alternative;
    check(run(list).state.result == 1004U && list.snapshot().slots_read == 2,
          "array base fixed once, but its remaining slots are read live");
    Fixture bad;
    bad.owner.count_0c = 2;
    bad.owner.children_08 = std::span{bad.slots}.first(1);
    auto fault = VmfNotificationStateContext::sum(bad.owner);
    const auto result = raw(fault);
    check(!result && result.error().code == Error::row_out_of_range &&
              fault.snapshot().sum == 5 && fault.snapshot().weights_read == 1,
          "out-of-range refusal happens at actual access after partial sum");
    check(!raw(fault) && fault.snapshot().sum == 5,
          "fault does not replay an earlier contribution");
}
void sums_and_levels() {
    struct SumCase {
        std::uint32_t a, b, expected;
    };
    for (auto sample :
         {SumCase{0xffffffff, 3, 2}, SumCase{0x80000000, 0x80000000, 0},
          SumCase{0, 0, 0}, SumCase{0x7fffffff, 1, 0x80000000},
          SumCase{0xffffffff, 0xffffffff, 0xfffffffe}}) {
        Fixture f;
        f.a.weight_14 = sample.a;
        f.b.weight_14 = sample.b;
        auto c = VmfNotificationStateContext::sum(f.owner);
        check(
            run(c).state.result == sample.expected,
            "sum uses DWORD wrapping arithmetic without signed overflow/clamp");
    }
    struct LevelCase {
        std::uint32_t sum, expected;
    };
    for (auto sample :
         {LevelCase{0, 0}, LevelCase{9, 0}, LevelCase{10, 1}, LevelCase{19, 1},
          LevelCase{20, 2}, LevelCase{30, 3}, LevelCase{40, 4},
          LevelCase{49, 4}, LevelCase{50, 5}, LevelCase{0xffffffff, 5}}) {
        Fixture f;
        f.owner.count_0c = 1;
        f.a.weight_14 = sample.sum;
        auto c = VmfNotificationStateContext::level(f.owner);
        check(run(c).state.result == sample.expected &&
                  c.snapshot().thresholds_read ==
                      (sample.expected == 0 ? 5U : 6U - sample.expected),
              "native threshold boundary, equality and short-circuit count");
    }
    Fixture unsorted;
    unsorted.owner.thresholds = {1000U, 1000U, 1000U, 1000U, 5U};
    auto c = VmfNotificationStateContext::level(unsorted.owner);
    check(run(c).state.result == 5U && c.snapshot().thresholds_read == 1,
          "nonmonotonic thresholds are not reordered");
    Fixture high;
    high.owner.count_0c = 1;
    high.a.weight_14 = 0x7fffffffU;
    high.owner.thresholds = {std::nullopt, std::nullopt, 0x7fffffffU,
                             0xffffffffU, 0x80000000U};
    auto unsigned_level = VmfNotificationStateContext::level(high.owner);
    check(run(unsigned_level).state.result == 3U &&
              unsigned_level.snapshot().thresholds_read == 3,
          "threshold comparison is unsigned and unused lower fields stay "
          "unknown");
    Fixture live;
    for (auto& threshold : live.owner.thresholds)
        threshold.reset();
    auto pending = VmfNotificationStateContext::level(live.owner);
    check(run(pending).status == Status::needs_threshold &&
              pending.snapshot().sum == 12 &&
              pending.snapshot().threshold_index == 4,
          "all contributions are computed before threshold binding");
    live.a.weight_14 = 400U;
    live.owner.thresholds[4] = 13U;
    check(run(pending).status == Status::needs_threshold &&
              pending.snapshot().threshold_index == 3,
          "later threshold read preserves prior comparison and sum");
    live.owner.thresholds[4] = 0U;
    live.owner.thresholds[3] = 12U;
    auto moved = std::move(pending);
    check(!raw(pending), "pending level move invalidates source");
    const auto result = run(moved);
    check(result.state.result == 4U && result.state.thresholds_read == 2 &&
              result.state.sum == 12,
          "move/resume does not rescan weights or reread an earlier threshold");
    live.owner.thresholds[3].reset();
    check(run(moved).state.steps == result.state.steps &&
              moved.snapshot().result == 4U,
          "finished computation never replays its reads");
}
void host_bindings_and_selected_state() {
    Fixture f, other;
    VmfNotificationLevelDestination target{909U};
    Host a, b;
    a.owner = &f.owner;
    b.owner = &other.owner;
    auto context = VmfNotificationStoreContext::create(target);
    check(run(context, {}).status == StoreStatus::needs_host && a.calls == 0,
          "unknown host does not become a successful or NULL service");
    check(run(context, &a, 1).state.phase == StorePhase::query && a.calls == 0,
          "host is captured before virtual invocation");
    f.owner.count_0c.reset();
    const auto pending = run(context, &b);
    check(pending.status == StoreStatus::state_pending &&
              pending.computation_status == Status::needs_count &&
              a.calls == 1 && b.calls == 0 && target.level_f028 == 909U,
          "actual selected virtual call runs once before waiting for returned "
          "state");
    a.owner = &other.owner;
    f.owner.count_0c = 3;
    auto moved = std::move(context);
    check(!raw(context, &a), "store move invalidates source");
    const auto done = run(moved, {});
    check(
        done.status == StoreStatus::finished && target.level_f028 == 1U &&
            done.state.completed_host_calls == 1 && done.state.stores == 1 &&
            a.calls == 1 && b.calls == 0,
        "returned owner and call prefix remain retained across wait and move");
    target.level_f028 = 777U;
    check(run(moved, {}).state.steps == done.state.steps &&
              target.level_f028 == 777U && a.calls == 1,
          "finished store does not overwrite a later target change");
    auto null_host = VmfNotificationStoreContext::create(target);
    const auto failure =
        raw(null_host, static_cast<VmfNotificationStateHost*>(nullptr));
    check(!failure && failure.error().code == Error::invalid_native_binding &&
              target.level_f028 == 777U,
          "known NULL global host is a dereference fault");
    for (bool fail : {true, false}) {
        Host host;
        host.fail = fail;
        auto rejected = VmfNotificationStoreContext::create(target);
        const auto result = raw(rejected, &host);
        check(!result &&
                  result.error().code ==
                      (fail ? Error::host_failure
                            : Error::invalid_native_binding) &&
                  host.calls == 1 && target.level_f028 == 777U &&
                  rejected.snapshot().completed_host_calls == (fail ? 0U : 1U),
              "failed virtual service or NULL returned owner preserves prior "
              "effects");
        check(!raw(rejected, &host) && host.calls == 1,
              "faulted store does not invoke its host again");
    }
}
void nested_budgets_reentry_and_fault() {
    Fixture empty;
    empty.owner.count_0c = 0;
    empty.owner.thresholds[4] = 0U;
    Host h;
    h.owner = &empty.owner;
    VmfNotificationLevelDestination target{90U};
    auto exact = VmfNotificationStoreContext::create(target);
    check(run(exact, &h, 0).status == StoreStatus::step_limit && h.calls == 0,
          "zero store budget invokes nothing");
    const auto boundary = run(exact, &h, 4);
    check(boundary.status == StoreStatus::step_limit && boundary.steps == 4 &&
              boundary.state.phase == StorePhase::store &&
              target.level_f028 == 90U && h.calls == 1,
          "nested completion at budget boundary does not perform the later "
          "store");
    check(run(exact, {}, 0).state.steps == 4 && target.level_f028 == 90U,
          "zero budget at final store preserves target");
    check(run(exact, {}, 1).status == StoreStatus::finished &&
              exact.snapshot().steps == 5 && target.level_f028 == 5U &&
              h.calls == 1,
          "remaining store consumes one step without repeating callback");
    Fixture f;
    Host host;
    host.owner = &f.owner;
    auto one = VmfNotificationStoreContext::create(target);
    host.reenter = &one;
    VmfNotificationStoreReport report{};
    std::size_t attempts = 0;
    do {
        report = run(one, &host, 1);
        check(report.steps <= 1, "shared one-step budget is never exceeded");
    } while (report.status == StoreStatus::step_limit && ++attempts < 128);
    check(report.status == StoreStatus::finished && target.level_f028 == 1U &&
              host.calls == 1 && host.reentry_refused &&
              report.state.stores == 1,
          "real host call, reentrance refusal and nested budget resume finish "
          "once");
    Fixture bad;
    bad.owner.count_0c = 2;
    bad.owner.children_08 = std::span{bad.slots}.first(1);
    Host bad_host;
    bad_host.owner = &bad.owner;
    target.level_f028 = 321U;
    auto broken = VmfNotificationStoreContext::create(target);
    const auto result = raw(broken, &bad_host);
    const auto snapshot = broken.snapshot();
    check(!result && result.error().code == Error::row_out_of_range &&
              bad_host.calls == 1 && snapshot.computation &&
              snapshot.computation->sum == 5 && snapshot.stores == 0 &&
              snapshot.steps == 2 + snapshot.computation->steps &&
              target.level_f028 == 321U,
          "nested fault retains host prefix, partial sum, consumed steps and "
          "target");
    check(!raw(broken, &bad_host) && bad_host.calls == 1,
          "nested fault is terminal without host replay");
}
} // namespace

int main() {
    try {
        sum_guards_and_bindings();
        captures_moves_and_faults();
        sums_and_levels();
        host_bindings_and_selected_state();
        nested_budgets_reentry_and_fault();
    } catch (const std::exception& error) {
        allocation_fixture::enabled = false;
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

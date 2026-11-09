#include "tdur/game/startup_mode_queue.hpp"

#include <cstdio>
#include <limits>
#include <vector>

using namespace tdur::game;
using namespace tdur::game::startup;
namespace {
int checks = 0, failures = 0;
void check(bool value, const char* text, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %d: %s\n", line, text);
    }
}
#define CHECK(value) check((value), #value, __LINE__)
constexpr auto ok = ModeCallOutcome::native_returned;
constexpr auto no = ModeCallOutcome::modern_refusal;
ModeWorkPtr item() {
    return std::make_shared<ModeWorkItem>(ModeWorkItem{0x80});
}
struct Ports : ModeQueuePorts {
    std::function<ModeCallOutcome(ModeLockRef)> enter, leave;
    std::function<ModeCallOutcome(ModeSignalRef)> signal;
    ModeYield yield;
    std::vector<GameModeRuntime*> entered, left, signalled;
    int depth = 0, max_depth = 0;
    ModeCallOutcome enter_606b60(ModeLockRef ref) override {
        CHECK(ref.owner && ref.offset == 0x318);
        entered.push_back(ref.owner);
        const auto result = enter ? enter(ref) : ok;
        if (result == ok) {
            ++depth;
            if (depth > max_depth)
                max_depth = depth;
        }
        return result;
    }
    ModeCallOutcome leave_606b90(ModeLockRef ref) override {
        CHECK(ref.owner && ref.offset == 0x318);
        left.push_back(ref.owner);
        const auto result = leave ? leave(ref) : ok;
        if (result == ok)
            --depth;
        return result;
    }
    ModeYield load_yield_9ec158() override { return yield; }
    ModeCallOutcome process_9b4710(GameModeRuntime&) override {
        CHECK(false); // The binding must execute its reconstructed body.
        return no;
    }
    ModeCallOutcome signal_606d40(ModeSignalRef ref) override {
        CHECK(ref.owner && ref.offset == 0x1c8);
        signalled.push_back(ref.owner);
        return signal ? signal(ref) : ok;
    }
};
void remove_positions() {
    for (const int count : {1, 2, 4, 63, 64})
        for (int position = 0; position < count; ++position) {
            GameModeRuntime owner;
            Ports p;
            owner.pending_314 = count;
            for (auto& slot : owner.work_slots_210)
                slot = item();
            const auto before = owner.work_slots_210;
            p.leave = [&](ModeLockRef ref) {
                CHECK(ref.owner == &owner);
                CHECK(owner.pending_314 == count - 1);
                CHECK(before[position]->queued_04 == 0);
                return ok;
            };
            CHECK(remove_mode_work_9b4400(owner, before[position], p) ==
                  ModeRemoveOutcome::removed);
            for (int slot = 0; slot < 65; ++slot) {
                const auto expected = slot >= position && slot < count
                                          ? before[slot + 1]
                                          : before[slot];
                CHECK(owner.work_slots_210[slot] == expected);
                CHECK(before[slot]->queued_04 == (slot == position ? 0 : 0x80));
            }
            CHECK(p.depth == 0 && p.entered.size() == 1 && p.left.size() == 1);
        }
    GameModeRuntime owner;
    Ports p;
    auto work = item(), end = item();
    owner.work_slots_210[0] = work;
    owner.work_slots_210[1] = work;
    owner.work_slots_210[2] = end;
    owner.pending_314 = 2;
    CHECK(remove_mode_work_9b4400(owner, work, p) ==
          ModeRemoveOutcome::removed);
    CHECK(owner.work_slots_210[0] == work && owner.work_slots_210[1] == end);
    CHECK(owner.pending_314 == 1 && work->queued_04 == 0);
}
void remove_boundaries() {
    for (const auto count :
         {std::numeric_limits<std::int32_t>::min(), -1, 0, 65}) {
        GameModeRuntime owner;
        Ports p;
        owner.pending_314 = count;
        const auto work = item();
        CHECK(remove_mode_work_9b4400(owner, work, p) ==
              ModeRemoveOutcome::not_found);
        CHECK(owner.pending_314 == count && work->queued_04 == 0x80);
        CHECK(p.depth == 0 && p.left.size() == 1);
    }
    for (const auto count :
         {65, 66, std::numeric_limits<std::int32_t>::max()}) {
        GameModeRuntime owner;
        Ports p;
        auto work = item();
        owner.work_slots_210[0] = work;
        owner.pending_314 = count;
        CHECK(remove_mode_work_9b4400(owner, work, p) ==
              ModeRemoveOutcome::refused_extent);
        CHECK(owner.pending_314 == count && owner.work_slots_210[0] == work);
        CHECK(p.depth == 1 && p.left.empty());
    }
    GameModeRuntime owner;
    Ports p;
    owner.pending_314 = 1;
    auto end = item();
    owner.work_slots_210[1] = end;
    CHECK(remove_mode_work_9b4400(owner, {}, p) ==
          ModeRemoveOutcome::refused_item);
    CHECK(owner.pending_314 == 0 && owner.work_slots_210[0] == end);
    CHECK(p.left.empty() && end->queued_04 == 0x80);

    Ports q;
    auto work = item();
    owner.pending_314 = 1;
    owner.work_slots_210[0] = work;
    q.enter = [](ModeLockRef) { return no; };
    CHECK(remove_mode_work_9b4400(owner, work, q) ==
          ModeRemoveOutcome::refused_enter);
    CHECK(owner.pending_314 == 1 && q.left.empty() && work->queued_04 == 0x80);
    q.enter = {};
    q.leave = [](ModeLockRef) { return no; };
    CHECK(remove_mode_work_9b4400(owner, work, q) ==
          ModeRemoveOutcome::refused_leave);
    CHECK(owner.pending_314 == 0 && work->queued_04 == 0);
    CHECK(remove_mode_work_9b4400(owner, work, q) ==
          ModeRemoveOutcome::refused_leave);

    // Entry effects are read before lookup. The argument keeps the object
    // alive even when shifting removes the last reference from the queue.
    Ports r;
    owner.work_slots_210 = {};
    owner.work_slots_210[0] = item();
    std::weak_ptr<ModeWorkItem> weak = owner.work_slots_210[0];
    owner.pending_314 = -1;
    r.enter = [](ModeLockRef ref) {
        ref.owner->pending_314 = 1;
        return ok;
    };
    r.leave = [&](ModeLockRef) {
        CHECK(!weak.expired());
        CHECK(weak.lock()->queued_04 == 0);
        return ok;
    };
    CHECK(remove_mode_work_9b4400(owner, owner.work_slots_210[0], r) ==
          ModeRemoveOutcome::removed);
    CHECK(weak.expired());
}
void process_states() {
    for (int count : {-1, 0, 1, 64})
        for (int flag : {0, 1, 2, 0x80}) {
            GameModeRuntime owner;
            GameModeRuntime* live = &owner;
            Ports p;
            const auto previous = item(), first = item();
            owner.current_work_200 = previous;
            owner.work_slots_210[0] = first;
            owner.pending_314 = count;
            owner.completion_208 = static_cast<std::uint8_t>(flag);
            p.signal = [&](ModeSignalRef ref) {
                CHECK(ref.owner == &owner && owner.completion_208 == 0);
                CHECK(owner.current_work_200 ==
                      (count > 0 ? first : ModeWorkPtr{}));
                return ok;
            };
            CHECK(process_mode_work_9b4710(owner, live, p) ==
                  ModeProcessOutcome::native_return);
            const bool ran = flag == 1;
            CHECK(p.signalled.size() == (ran ? 1U : 0U));
            CHECK(owner.pending_314 == (ran && count > 0 ? count - 1 : count));
            CHECK(owner.current_work_200 == (!ran        ? previous
                                             : count > 0 ? first
                                                         : ModeWorkPtr{}));
            CHECK(owner.completion_208 == (ran ? 0 : flag));
            CHECK(first->queued_04 == (ran && count > 0 ? 0 : 0x80));
        }
}
void live_reloads_and_refusals() {
    GameModeRuntime a, b;
    GameModeRuntime* live = &a;
    Ports p;
    auto first = item(), replacement = item(), end = item();
    a.pending_314 = 1;
    a.completion_208 = 1;
    a.work_slots_210[0] = first;
    b.pending_314 = 1;
    b.work_slots_210[0] = replacement;
    b.work_slots_210[1] = end;
    p.signal = [&](ModeSignalRef ref) {
        CHECK(ref.owner == &a && a.current_work_200 == first);
        a.current_work_200 = replacement;
        live = &b;
        return ok;
    };
    CHECK(process_mode_work_9b4710(a, live, p) ==
          ModeProcessOutcome::native_return);
    CHECK(a.pending_314 == 1 && a.work_slots_210[0] == first &&
          first->queued_04 == 0x80);
    CHECK(b.pending_314 == 0 && b.work_slots_210[0] == end &&
          replacement->queued_04 == 0);
    CHECK(p.entered == std::vector<GameModeRuntime*>{&b});
    // Native false from removal still returns normally.
    a.completion_208 = 1;
    CHECK(process_mode_work_9b4710(a, live, p) ==
          ModeProcessOutcome::native_return);
    CHECK(b.pending_314 == 0);

    a.completion_208 = 1;
    p.signal = [](ModeSignalRef) { return no; };
    const auto enters = p.entered.size();
    CHECK(process_mode_work_9b4710(a, live, p) ==
          ModeProcessOutcome::refused_signal);
    CHECK(a.current_work_200 == first && a.completion_208 == 0 &&
          p.entered.size() == enters);
    a.completion_208 = 1;
    p.signal = [&](ModeSignalRef) {
        live = nullptr;
        return ok;
    };
    CHECK(process_mode_work_9b4710(a, live, p) ==
          ModeProcessOutcome::refused_owner);
    CHECK(a.current_work_200 == first && a.completion_208 == 0);
    // The no-work tail never reads the global owner after signal.
    a.pending_314 = 0;
    a.completion_208 = 1;
    CHECK(process_mode_work_9b4710(a, live, p) ==
          ModeProcessOutcome::native_return);
    CHECK(!a.current_work_200);
    a.pending_314 = 1;
    a.completion_208 = 1;
    p.signal = [&](ModeSignalRef) {
        live = &b;
        return ok;
    };
    p.enter = [](ModeLockRef) { return no; };
    CHECK(process_mode_work_9b4710(a, live, p) ==
          ModeProcessOutcome::refused_remove);
}
void actual_drain_composition() {
    for (std::uint32_t one : {0U, 1U, 0x100U}) {
        GameModeRuntime owner;
        GameModeRuntime* live = &owner;
        Ports p;
        owner.pending_314 = 3;
        owner.completion_208 = 1;
        for (int i = 0; i < 4; ++i)
            owner.work_slots_210[i] = item();
        const auto original = owner.work_slots_210;
        p.signal = [](ModeSignalRef ref) {
            ref.owner->completion_208 = 1;
            return ok;
        };
        ModeQueueBinding binding(live, p);
        CHECK(drain_mode_9b4ba0(owner, one, {4, 0}, binding) ==
              ModeDrainOutcome::native_return);
        const int done = (one & 0xffU) ? 1 : 3;
        CHECK(owner.pending_314 == 3 - done && p.depth == 0 &&
              p.max_depth == 2);
        CHECK(owner.current_work_200 == original[done - 1]);
        CHECK(owner.work_slots_210[0] == original[done]);
        for (int i = 0; i < 4; ++i)
            CHECK(original[i]->queued_04 == (i < done ? 0 : 0x80));
        CHECK(binding.last_process == ModeProcessOutcome::native_return);
    }
    GameModeRuntime owner;
    GameModeRuntime* live = &owner;
    Ports p;
    owner.pending_314 = 1;
    owner.completion_208 = 1;
    owner.work_slots_210[0] = item();
    p.signal = [](ModeSignalRef) { return no; };
    ModeQueueBinding binding(live, p);
    CHECK(drain_mode_9b4ba0(owner, 0, {1, 0}, binding) ==
          ModeDrainOutcome::refused_process);
    CHECK(owner.pending_314 == 1 && owner.completion_208 == 0 &&
          p.left.empty());
    CHECK(binding.last_process == ModeProcessOutcome::refused_signal);
}
} // namespace
int main() {
    remove_positions();
    remove_boundaries();
    process_states();
    live_reloads_and_refusals();
    actual_drain_composition();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}

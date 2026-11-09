#include "tdur/game/startup_world_sync.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

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

struct QueuePorts : LoadingQueuePorts {
    LoadingQueue* owner = nullptr;
    std::function<ModeCallOutcome(LoadRequest&)> hook;
    std::size_t signals = 0;
    LoadRequest* seen = nullptr;
    ModeCallOutcome signal_606d40(LoadingMemberRef member,
                                  LoadRequest& request) override {
        ++signals;
        seen = &request;
        CHECK(member.owner == owner && member.offset == 0x56558);
        CHECK(owner->active_56594 == 1 && owner->finished_56595 == 0);
        CHECK(
            &owner->requests_1c[std::get<std::size_t>(owner->current_56590)] ==
            &request);
        return hook ? hook(request) : ok;
    }
};
void pending_flags() {
    LoadingQueue q;
    CHECK(pending_loads_9a2a20(q) == 0);
    for (std::size_t index = 0; index < 32; ++index)
        for (std::uint8_t pending :
             {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}})
            for (std::uint8_t settled :
                 {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}}) {
                q.requests_1c[index].pending_414 = pending;
                q.requests_1c[index].settled_528 = settled;
                CHECK(pending_loads_9a2a20(q) ==
                      (pending != 0 && settled == 0 ? 1 : 0));
                q.requests_1c[index] = {};
            }
    q.requests_1c[0].pending_414 = 1;
    q.requests_1c[31].pending_414 = 0x80;
    q.requests_1c[0].settled_528 = 1;
    CHECK(pending_loads_9a2a20(q) == 1);
    q.requests_1c[31].settled_528 = 2;
    CHECK(pending_loads_9a2a20(q) == 0);
}
void selection_order() {
    for (std::size_t current = 0; current < 33; ++current)
        for (std::size_t candidate = 1; candidate < 32; ++candidate) {
            LoadingQueue q;
            QueuePorts p;
            p.owner = &q;
            if (current < 32)
                q.current_56590 = current;
            q.requests_1c[0].pending_414 = 1;
            q.requests_1c[candidate].pending_414 = 0x80;
            q.finished_56595 = 0x80;
            q.byte_572e4 = 77;
            CHECK(pump_loads_9a2bd0(q, p) == LoadingPumpOutcome::native_return);
            const auto expected = current < 32 ? candidate : 0;
            CHECK(std::get<std::size_t>(q.current_56590) == expected);
            CHECK(p.seen == &q.requests_1c[expected] && p.signals == 1);
            CHECK(q.byte_572e4 == 77 && pending_loads_9a2a20(q) == 1);
        }
    LoadingQueue q;
    QueuePorts p;
    p.owner = &q;
    q.current_56590 = std::size_t{31};
    q.finished_56595 = 9;
    q.byte_572e4 = 11;
    CHECK(pump_loads_9a2bd0(q, p) == LoadingPumpOutcome::native_return);
    CHECK(p.signals == 0 && q.active_56594 == 0 && q.finished_56595 == 9);
    CHECK(std::get<std::size_t>(q.current_56590) == 31);
    q.requests_1c[0].pending_414 = 1;
    q.requests_1c[1].pending_414 = 1;
    q.requests_1c[1].settled_528 = 0x80;
    CHECK(pump_loads_9a2bd0(q, p) == LoadingPumpOutcome::native_return);
    CHECK(p.seen == &q.requests_1c[0]);
    auto copy = q;
    copy.active_56594 = 0;
    copy.current_56590 = std::size_t{1};
    auto moved = std::move(copy);
    p.owner = &moved;
    CHECK(pump_loads_9a2bd0(moved, p) == LoadingPumpOutcome::native_return);
    CHECK(p.seen == &moved.requests_1c[0] && p.seen != &q.requests_1c[0]);
    moved.active_56594 = 0;
    moved.current_56590 = std::make_shared<LoadRequest>();
    moved.requests_1c[1].settled_528 = 0;
    CHECK(pump_loads_9a2bd0(moved, p) == LoadingPumpOutcome::native_return);
    CHECK(std::get<std::size_t>(moved.current_56590) == 0);
    moved.active_56594 = 0;
    moved.current_56590 = std::size_t{32};
    CHECK(pump_loads_9a2bd0(moved, p) == LoadingPumpOutcome::refused_current);
    CHECK(moved.active_56594 == 0);
    moved.current_56590 = std::monostate{};
    p.hook = [](LoadRequest&) { return no; };
    CHECK(pump_loads_9a2bd0(moved, p) == LoadingPumpOutcome::refused_signal);
    CHECK(moved.active_56594 == 1 && moved.finished_56595 == 0);
}
void completion() {
    for (std::uint8_t active : {std::uint8_t{1}, std::uint8_t{0x80}})
        for (std::uint8_t settled :
             {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}})
            for (bool installed : {false, true}) {
                LoadingQueue q;
                QueuePorts p;
                p.owner = &q;
                q.active_56594 = active;
                q.byte_572e4 = 99;
                q.current_56590 = std::size_t{17};
                auto& r = q.requests_1c[17];
                r.pending_414 = 7;
                r.settled_528 = settled;
                std::size_t calls = 0;
                if (installed)
                    r.callback_524 = [&](LoadRequest& record,
                                         std::span<const std::uint32_t> words,
                                         std::uint32_t count) {
                        ++calls;
                        CHECK(&record == &r && words.size() == 1 &&
                              words[0] == 4 && count == 1);
                        CHECK(q.active_56594 == 0 && q.finished_56595 == 0 &&
                              q.byte_572e4 == 0);
                        q.current_56590 = std::size_t{2};
                        r.callback_524 = {};
                        return ok;
                    };
                CHECK(pump_loads_9a2bd0(q, p) ==
                      LoadingPumpOutcome::native_return);
                CHECK(calls == 0 && p.signals == 0 && q.byte_572e4 == 99 &&
                      q.active_56594 == active);
                q.finished_56595 = 0x80;
                CHECK(pump_loads_9a2bd0(q, p) ==
                      LoadingPumpOutcome::native_return);
                CHECK(calls == (installed && settled == 0 ? 1U : 0U));
                CHECK(q.active_56594 == 0 && q.finished_56595 == 0 &&
                      q.byte_572e4 == 0);
                CHECK(r.pending_414 == 7 && r.settled_528 == settled);
            }
    LoadingQueue q;
    QueuePorts p;
    p.owner = &q;
    q.active_56594 = 1;
    q.finished_56595 = 1;
    q.byte_572e4 = 9;
    CHECK(pump_loads_9a2bd0(q, p) == LoadingPumpOutcome::refused_current);
    CHECK(q.active_56594 == 0 && q.finished_56595 == 0 && q.byte_572e4 == 0);
    std::weak_ptr<LoadRequest> weak;
    {
        auto external = std::make_shared<LoadRequest>();
        weak = external;
        external->callback_524 = [&](LoadRequest&,
                                     std::span<const std::uint32_t> words,
                                     std::uint32_t count) {
            q.current_56590 = std::monostate{};
            CHECK(!weak.expired() && words[0] == 4 && count == 1);
            return no;
        };
        q.current_56590 = external;
    }
    q.active_56594 = 1;
    q.finished_56595 = 1;
    CHECK(pump_loads_9a2bd0(q, p) == LoadingPumpOutcome::refused_callback);
    CHECK(weak.expired() && q.active_56594 == 0);
}

struct ModePorts : ModeDrainPorts {
    GameModeRuntime* owner = nullptr;
    std::vector<std::string> events;
    std::function<void(std::string_view)> hook;
    std::string fail;
    std::size_t lookups = 0, yielded = 0;
    ModeYield dispatch;
    ModePorts() {
        dispatch = [this](std::uint32_t value) {
            CHECK(value == 0);
            ++yielded;
            if (yielded == 3)
                owner->completion_208 = 1;
            return step("yield");
        };
    }
    ModeCallOutcome step(std::string_view name) {
        events.emplace_back(name);
        if (hook)
            hook(name);
        return name == fail ? no : ok;
    }
    ModeCallOutcome enter_606b60(ModeLockRef ref) override {
        CHECK(ref.owner == owner && ref.offset == 0x318);
        return step("enter");
    }
    ModeYield load_yield_9ec158() override {
        ++lookups;
        return dispatch;
    }
    ModeCallOutcome process_9b4710(GameModeRuntime& r) override {
        CHECK(&r == owner);
        if (!hook)
            --r.pending_314;
        return step("process");
    }
    ModeCallOutcome leave_606b90(ModeLockRef ref) override {
        CHECK(ref.owner == owner && ref.offset == 0x318);
        return step("leave");
    }
};
void mode_paths() {
    for (std::uint32_t argument : {0U, 1U, 0x100U, 0x180U})
        for (std::int32_t count : {-2, 0, 1, 3})
            for (std::uint8_t ready :
                 {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}}) {
                GameModeRuntime owner;
                owner.pending_314 = count;
                owner.completion_208 = ready;
                ModePorts p;
                p.owner = &owner;
                CHECK(drain_mode_9b4ba0(owner, argument, {9, 4}, p) ==
                      ModeDrainOutcome::native_return);
                const bool one = (argument & 0xFFU) != 0;
                const auto processed = count > 0 ? (one ? 1 : count) : 0;
                CHECK(std::count(p.events.begin(), p.events.end(), "process") ==
                      processed);
                CHECK(owner.pending_314 == count - processed && p.lookups == 1);
                CHECK(p.yielded ==
                      ((count > 0 || !one) && ready != 1 ? 3U : 0U));
                CHECK(p.events.front() == "enter" &&
                      p.events.back() == "leave");
            }
    GameModeRuntime r;
    r.pending_314 = 3;
    r.completion_208 = 1;
    ModePorts p;
    p.owner = &r;
    std::size_t calls = 0;
    p.hook = [&](std::string_view event) {
        if (event == "enter")
            r.pending_314 = 5;
        if (event == "process") {
            ++calls;
            r.pending_314 = calls == 1 ? 7 : r.pending_314 - 1;
        }
    };
    CHECK(drain_mode_9b4ba0(r, 1, {9, 0}, p) ==
          ModeDrainOutcome::native_return);
    CHECK(calls == 4 && r.pending_314 == 4);
    r.pending_314 = 1;
    p.hook = [&](std::string_view event) {
        if (event == "process")
            r.pending_314 = -1;
    };
    CHECK(drain_mode_9b4ba0(r, 0, {1, 0}, p) ==
          ModeDrainOutcome::native_return);
    CHECK(r.pending_314 == -1);
    r.pending_314 = 0;
    r.completion_208 = 0;
    p.hook = {};
    p.yielded = 0;
    p.dispatch = [&](std::uint32_t value) {
        CHECK(value == 0);
        ++p.yielded;
        p.dispatch = [](std::uint32_t) {
            CHECK(false);
            return no;
        };
        if (p.yielded == 2)
            r.completion_208 = 1;
        return ok;
    };
    CHECK(drain_mode_9b4ba0(r, 0, {0, 2}, p) ==
          ModeDrainOutcome::native_return);
    CHECK(p.yielded == 2);
}
void mode_refusals() {
    for (const auto* event : {"enter", "process", "yield", "leave"}) {
        GameModeRuntime r;
        r.pending_314 = 1;
        r.completion_208 = 0;
        ModePorts p;
        p.owner = &r;
        p.fail = event;
        const auto outcome = drain_mode_9b4ba0(r, 0, {1, 4}, p);
        const auto expected = std::string_view(event) == "enter"
                                  ? ModeDrainOutcome::refused_enter
                              : std::string_view(event) == "process"
                                  ? ModeDrainOutcome::refused_process
                              : std::string_view(event) == "yield"
                                  ? ModeDrainOutcome::refused_yield
                                  : ModeDrainOutcome::refused_leave;
        CHECK(outcome == expected && p.events.back() == event);
        CHECK(p.lookups == (std::string_view(event) == "enter" ? 0U : 1U));
        if (std::string_view(event) != "leave")
            CHECK(std::count(p.events.begin(), p.events.end(), "leave") == 0);
    }
    GameModeRuntime r;
    r.pending_314 = 2;
    r.completion_208 = 1;
    ModePorts p;
    p.owner = &r;
    p.hook = [](std::string_view) {};
    CHECK(drain_mode_9b4ba0(r, 0, {2, 0}, p) ==
          ModeDrainOutcome::refused_process_budget);
    CHECK(r.pending_314 == 2 && p.events.size() == 3);
    p.events.clear();
    p.hook = {};
    r.pending_314 = 0;
    r.completion_208 = 0x80;
    CHECK(drain_mode_9b4ba0(r, 0, {0, 2}, p) ==
          ModeDrainOutcome::refused_yield_budget);
    CHECK(p.yielded == 2 && r.completion_208 == 0x80 &&
          p.events.back() == "yield");
    p.dispatch = {};
    CHECK(drain_mode_9b4ba0(r, 0, {0, 1}, p) ==
          ModeDrainOutcome::refused_yield);
    r.completion_208 = 1;
    CHECK(drain_mode_9b4ba0(r, 0, {0, 0}, p) ==
          ModeDrainOutcome::native_return);
}

struct Inner : WorldLoaderPorts {
    std::function<ModeCallOutcome()> finish;
    WorldPathFormatter load_formatter_9ec378() override { return {}; }
    ModeCallOutcome reset_area_709200(AreaLoadPayload&) override { return no; }
    ModeCallOutcome load_area_709590(AreaLoadPayload&,
                                     const AreaLoadCall&) override {
        return no;
    }
    ModeCallOutcome reset_sector_705cb0(SectorLoadPayload&) override {
        return no;
    }
    ModeCallOutcome load_sector_706030(SectorLoadPayload&,
                                       const SectorLoadCall&) override {
        return no;
    }
    std::optional<std::uint8_t> pending_9a2a20(NativeIdentity) override {
        CHECK(false);
        return {};
    }
    ModeCallOutcome pump_9a2bd0(NativeIdentity) override {
        CHECK(false);
        return no;
    }
    ModeCallOutcome finish_609a20(NativeIdentity owner,
                                  std::uint32_t argument) override {
        CHECK(owner.va == 0x100AF5C && argument == 0);
        return finish ? finish() : no;
    }
    ModeCallOutcome finish_9b4ba0(GameModeRuntime&, std::uint32_t) override {
        CHECK(false);
        return no;
    }
    ModeCallOutcome free_674e0c(Allocation) override { return no; }
};
void composition() {
    LoadingQueue q;
    QueuePorts p;
    p.owner = &q;
    ModePorts mode;
    Inner inner;
    GameModeRuntime original, replacement;
    auto* current = &original;
    mode.owner = &replacement;
    replacement.pending_314 = 2;
    q.requests_1c[4].pending_414 = 1;
    std::size_t completions = 0, finishes = 0;
    q.requests_1c[4].callback_524 = [&](LoadRequest& request,
                                        std::span<const std::uint32_t> words,
                                        std::uint32_t count) {
        CHECK(words[0] == 4 && count == 1);
        ++completions;
        request.settled_528 = 1;
        return ok;
    };
    p.hook = [&](LoadRequest&) {
        q.finished_56595 = 1;
        return ok;
    };
    inner.finish = [&] {
        ++finishes;
        CHECK(completions == 1);
        current = &replacement;
        return ok;
    };
    WorldSyncBinding binding(inner, q, p, mode, {2, 3});
    CHECK(drain_world_9a4930({0x10E80C0}, current, 2, binding) ==
          WorldDrainOutcome::native_return);
    CHECK(p.signals == 1 && completions == 1 && finishes == 1);
    CHECK(replacement.pending_314 == 0 && replacement.completion_208 == 1);
    CHECK(original.pending_314 == 0 && original.completion_208 == 0);
    CHECK(binding.last_pump == LoadingPumpOutcome::native_return &&
          binding.last_mode == ModeDrainOutcome::native_return);
    CHECK(!binding.pending_9a2a20({0x999}));
    CHECK(binding.pump_9a2bd0({0x999}) == no);
    q.requests_1c[4].settled_528 = 0;
    q.active_56594 = 0;
    CHECK(drain_world_9a4930({0x10E80C0}, current, 1, binding) ==
          WorldDrainOutcome::refused_budget);
    CHECK(finishes == 1 && q.active_56594 == 1 && q.finished_56595 == 1);
    q.requests_1c[4].callback_524 = [](LoadRequest&,
                                       std::span<const std::uint32_t>,
                                       std::uint32_t) { return no; };
    CHECK(drain_world_9a4930({0x10E80C0}, current, 1, binding) ==
          WorldDrainOutcome::refused_pump);
    CHECK(finishes == 1 &&
          binding.last_pump == LoadingPumpOutcome::refused_callback);
}
} // namespace
int main() {
    pending_flags();
    selection_order();
    completion();
    mode_paths();
    mode_refusals();
    composition();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

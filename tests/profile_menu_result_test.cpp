// Contract tests for the result path of the profile menu scene: listener
// callback 007C47B0, processing 007C5A90 and the consuming segment of
// 007C5B60, connected to the command layer. Ports share one event log
// and snapshot the live scene they receive, so call order, write order
// around calls, scene identity and reentrant writes are checked from the
// outside.
#include "tdur/game/profile_menu_result.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace tdur::game;

int failures = 0;
int checks = 0;

void expect(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label.c_str());
    }
}

using Log = std::vector<std::string>;

bool same(const Log& log, std::initializer_list<const char*> expected) {
    if (log.size() != expected.size())
        return false;
    std::size_t index = 0;
    for (const char* item : expected)
        if (log[index++] != item)
            return false;
    return true;
}

std::string hex(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%X", value);
    return buffer;
}

// Sentinels for fields that a case must leave alone.
constexpr std::uint32_t initial_state = 7;
constexpr std::uint8_t initial_flag_34 = 0x5A;
constexpr std::uint8_t initial_flag_35 = 0xA5;
constexpr std::uint8_t initial_menu_pending = 0x33;
constexpr ProfileIdentity initial_selected = 0x0000123400005678ull;

struct Snapshot {
    const ProfileMenuScene* scene = nullptr;
    std::uint32_t state = 0;
    std::uint8_t flag_34 = 0;
    std::uint8_t flag_35 = 0;
    std::uint8_t pending = 0;
    std::uint32_t result = 0;
    std::uint32_t context = 0;
    std::uint8_t menu_pending = 0;
};

Snapshot snapshot(const ProfileMenuScene& scene) {
    return {&scene,
            scene.state,
            scene.flag_34,
            scene.flag_35,
            scene.listener.pending,
            scene.listener.result,
            scene.listener.context,
            scene.menu.pending};
}

using Hook = std::function<void(ProfileMenuScene&)>;

struct PortRecorder {
    Log* log = nullptr;
    std::vector<Snapshot> seen;
    Hook hook;

    void record(const std::string& event, ProfileMenuScene& scene) {
        log->push_back(event);
        seen.push_back(snapshot(scene));
        if (hook)
            hook(scene);
    }
};

// A missing call yields an empty snapshot, so the check fails instead of
// throwing.
Snapshot seen_at(const PortRecorder& port, std::size_t index) {
    return index < port.seen.size() ? port.seen[index] : Snapshot{};
}

struct FakeShared final : ProfileResultSharedPort, PortRecorder {
    void shared_service(ProfileMenuScene& scene) override {
        record("shared", scene);
    }
};

struct FakeContextTwo final : ProfileResultContextTwoPort, PortRecorder {
    void context_two_service(ProfileMenuScene& scene) override {
        record("two", scene);
    }
};

struct FakeAction final : ProfileSceneActionPort, PortRecorder {
    std::uint32_t last_mode = 0;
    void mode_action(std::uint32_t mode, ProfileMenuScene& scene) override {
        last_mode = mode;
        record("action(" + std::to_string(mode) + ")", scene);
    }
};

// 0099B3F0 receives no scene; the rig tells it which scene to snapshot.
struct FakeContextFive final : ProfileResultContextFivePort, PortRecorder {
    ProfileMenuScene* scene = nullptr;
    bool tag_ok = false;
    void context_five_call(ProfileReferencedTag tag, std::uint32_t first,
                           std::uint32_t second, std::uint32_t third) override {
        tag_ok = tag == ProfileReferencedTag::context_five;
        record("five(" + std::to_string(first) + "," + std::to_string(second) +
                   "," + std::to_string(third) + ")",
               *scene);
    }
};

// Command-layer ports, for the coexistence cases.
struct FakeRank final : ProfileRankPort {
    std::int32_t menu_rank() override { return 1; }
};

struct FakeService final : ProfileServicePort {
    ProfileIdentity identity = 0;
    ProfileIdentity current = 0;
    void slot00(std::uint32_t, std::uint32_t) override {}
    ProfileIdentity slot04_current_identity() override { return current; }
    std::string slot08_text() override { return {}; }
    std::string slot30_name_at(std::int32_t) override { return "Alpha"; }
    void slot40_name(std::string_view) override {}
    ProfileIdentity slot44_identity_of(std::string_view) override {
        return identity;
    }
};

struct FakeCreation final : ProfileCreationPort {
    std::uint8_t leave = 0;
    void create_profile(ProfileMenuFields& menu) override {
        menu.pending = leave;
    }
};

struct Rig {
    Log log;
    ProfileMenuScene scene;
    FakeShared shared;
    FakeContextTwo two;
    FakeAction action;
    FakeContextFive five;

    Rig() {
        shared.log = &log;
        two.log = &log;
        action.log = &log;
        five.log = &log;
        five.scene = &scene;
        scene.state = initial_state;
        scene.flag_34 = initial_flag_34;
        scene.flag_35 = initial_flag_35;
        scene.menu.pending = initial_menu_pending;
        scene.menu.selected = initial_selected;
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    ProfileResultPorts ports() { return {shared, two, action, five}; }
    void arm(std::uint8_t pending, std::uint32_t result,
             std::uint32_t context) {
        scene.listener.pending = pending;
        scene.listener.result = result;
        scene.listener.context = context;
    }
    void process() { profile_result_process(scene, ports()); }
    bool consume() { return profile_result_consume(scene, ports()); }

    bool flags_untouched() const {
        return scene.flag_34 == initial_flag_34 &&
               scene.flag_35 == initial_flag_35;
    }
    bool flags_set() const { return scene.flag_34 == 1 && scene.flag_35 == 1; }
    bool menu_untouched() const {
        return scene.menu.pending == initial_menu_pending &&
               scene.menu.selected == initial_selected;
    }
    bool listener_is(std::uint8_t pending, std::uint32_t result,
                     std::uint32_t context) const {
        return scene.listener.pending == pending &&
               scene.listener.result == result &&
               scene.listener.context == context;
    }
};

// What a port saw: the live scene, the cleared pending byte, the values the
// processing read, and the scene fields before any write of the case.
bool saw_before_writes(const Rig& rig, const Snapshot& seen,
                       std::uint32_t context) {
    return seen.scene == &rig.scene && seen.pending == 0 && seen.result == 0 &&
           seen.context == context && seen.state == initial_state &&
           seen.flag_34 == initial_flag_34 && seen.flag_35 == initial_flag_35 &&
           seen.menu_pending == initial_menu_pending;
}

void callback_cases() {
    {
        Rig rig;
        rig.arm(0, 0xAAAA, 0xBBBB);
        profile_result_callback(rig.scene, 0x11, 0x22, 0x33);
        expect(rig.listener_is(1, 0x11, 0x33),
               "callback: pending 1, result first, context third");
        expect(rig.log.empty(), "callback: no port call");
        expect(rig.scene.state == initial_state && rig.flags_untouched() &&
                   rig.menu_untouched(),
               "callback: scene, flags and profile pending untouched");
    }
    {
        Rig low;
        Rig high;
        profile_result_callback(low.scene, 5, 0, 9);
        profile_result_callback(high.scene, 5, 0xFFFFFFFF, 9);
        expect(low.listener_is(1, 5, 9) && high.listener_is(1, 5, 9),
               "callback: second argument ignored");
    }
    {
        Rig rig;
        rig.arm(0x80, 1, 1);
        profile_result_callback(rig.scene, 0xFFFFFFFF, 3, 0x80000000);
        expect(rig.listener_is(1, 0xFFFFFFFF, 0x80000000),
               "callback: full DWORDs, byte rewritten to 1");
        profile_result_callback(rig.scene, 0, 0, 2);
        expect(rig.listener_is(1, 0, 2), "callback: last call wins");
    }
}

void accepted_cases() {
    {
        Rig rig;
        rig.arm(1, 0, 0);
        rig.process();
        expect(rig.log.empty(), "ctx 0: no call");
        expect(rig.flags_set() && rig.scene.state == 2,
               "ctx 0: flags, state 2");
        expect(rig.listener_is(0, 0, 0), "ctx 0: only pending cleared");
        expect(rig.menu_untouched(), "ctx 0: profile pending untouched");
    }
    {
        Rig rig;
        rig.arm(1, 0, 1);
        rig.process();
        expect(same(rig.log, {"shared"}), "ctx 1: shared service once");
        expect(saw_before_writes(rig, seen_at(rig.shared, 0), 1),
               "ctx 1: service sees live scene, pending 0, no write yet");
        expect(rig.flags_set() && rig.scene.state == 3,
               "ctx 1: flags and state 3 after the call");
        expect(rig.listener_is(0, 0, 1) && rig.menu_untouched(),
               "ctx 1: listener values kept, profile pending untouched");
    }
    {
        Rig rig;
        rig.arm(1, 0, 2);
        rig.process();
        expect(same(rig.log, {"shared", "two"}),
               "ctx 2: shared then context 2 service");
        expect(saw_before_writes(rig, seen_at(rig.shared, 0), 2) &&
                   saw_before_writes(rig, seen_at(rig.two, 0), 2),
               "ctx 2: both see the live scene with pending 0");
        expect(rig.scene.state == initial_state && rig.flags_untouched() &&
                   rig.menu_untouched(),
               "ctx 2: no write of its own");
        expect(rig.listener_is(0, 0, 2), "ctx 2: listener values kept");
    }
    {
        Rig rig;
        rig.scene.state = 3;
        rig.arm(1, 0, 4);
        rig.process();
        expect(same(rig.log, {"action(1)"}), "ctx 4 state 3: mode 1");
        const Snapshot seen = seen_at(rig.action, 0);
        expect(seen.scene == &rig.scene && seen.pending == 0 && seen.state == 3,
               "ctx 4 state 3: live scene, pending 0");
        expect(rig.scene.state == 3 && rig.flags_untouched() &&
                   rig.menu_untouched() && rig.listener_is(0, 0, 4),
               "ctx 4 state 3: no write of its own");
    }
    for (const std::uint32_t state :
         {0u, 1u, 2u, 4u, 7u, 0x103u, 0x80000003u, 0xFFFFFFFFu}) {
        Rig rig;
        rig.scene.state = state;
        rig.arm(1, 0, 4);
        rig.process();
        const std::string name = "ctx 4 state " + hex(state) + ": ";
        expect(same(rig.log, {"action(2)"}), name + "mode 2");
        expect(seen_at(rig.action, 0).scene == &rig.scene, name + "live scene");
        expect(rig.scene.state == state && rig.flags_untouched(),
               name + "no write of its own");
    }
    {
        Rig rig;
        rig.arm(1, 0, 5);
        rig.process();
        expect(same(rig.log, {"five(0,1,0)"}), "ctx 5: one call (0, 1, 0)");
        expect(rig.five.tag_ok, "ctx 5: referenced tag");
        expect(saw_before_writes(rig, seen_at(rig.five, 0), 5),
               "ctx 5: call before any write, pending 0");
        expect(rig.scene.state == 1 && rig.flags_set(),
               "ctx 5: state 1 and flags after the call");
        expect(rig.listener_is(0, 0, 5) && rig.menu_untouched(),
               "ctx 5: listener values kept, profile pending untouched");
    }
}

void nonzero_result_cases() {
    for (const std::uint32_t result : {1u, 2u, 0x80000000u, 0xFFFFFFFFu}) {
        // Contexts 0, 1 and 2 test the result (7C5AAE, 7C5AD4, 7C5AF2).
        for (const std::uint32_t context : {0u, 1u, 2u}) {
            Rig rig;
            rig.arm(1, result, context);
            rig.process();
            const std::string name =
                "refused ctx " + hex(context) + " res " + hex(result) + ": ";
            expect(rig.log.empty(), name + "no call");
            expect(rig.scene.state == initial_state && rig.flags_untouched() &&
                       rig.menu_untouched(),
                   name + "no write");
            expect(rig.listener_is(0, result, context),
                   name + "pending cleared, values kept");
        }
        Rig rig;
        rig.arm(1, result, 4);
        rig.process();
        const std::string name = "refused ctx 4 res " + hex(result) + ": ";
        expect(rig.log.empty(), name + "no call");
        expect(rig.scene.state == 0, name + "state 0");
        expect(rig.flags_untouched() && rig.menu_untouched(),
               name + "flags and profile pending untouched");
        expect(rig.listener_is(0, result, 4), name + "pending cleared");

        // Context 5 does not test the result (7C5B31 JNZ, then 7C5B33 PUSH).
        Rig five;
        five.arm(1, result, 5);
        five.process();
        const std::string five_name = "ctx 5 res " + hex(result) + ": ";
        expect(same(five.log, {"five(0,1,0)"}) && five.five.tag_ok,
               five_name + "call (tag, 0, 1, 0) despite the result");
        const Snapshot seen = seen_at(five.five, 0);
        expect(seen.pending == 0 && seen.result == result &&
                   seen.state == initial_state &&
                   seen.flag_34 == initial_flag_34 &&
                   seen.flag_35 == initial_flag_35,
               five_name + "call before any write");
        expect(five.scene.state == 1 && five.flags_set() &&
                   five.menu_untouched(),
               five_name + "state 1 and flags after the call");
        expect(five.listener_is(0, result, 5),
               five_name + "pending cleared, values kept");
    }
}

void unknown_contexts() {
    for (const std::uint32_t context :
         {3u, 6u, 0x100u, 0x101u, 0x80000000u, 0xFFFFFFFFu}) {
        for (const std::uint32_t result : {0u, 1u}) {
            Rig rig;
            rig.arm(1, result, context);
            rig.process();
            const std::string name =
                "unknown ctx " + hex(context) + " res " + hex(result) + ": ";
            expect(rig.log.empty(), name + "no call");
            expect(rig.scene.state == initial_state && rig.flags_untouched() &&
                       rig.menu_untouched(),
                   name + "no write");
            expect(rig.listener_is(0, result, context),
                   name + "only pending cleared");
        }
    }
}

void reentrant_cases() {
    {
        // A callback during the context 1 service: its three writes survive,
        // the context 1 writes still follow, and the next consumption sees
        // the new result.
        Rig rig;
        rig.arm(1, 0, 1);
        rig.shared.hook = [](ProfileMenuScene& scene) {
            profile_result_callback(scene, 9, 0, 4);
        };
        rig.process();
        expect(rig.listener_is(1, 9, 4), "reentrant ctx 1: callback kept");
        expect(rig.scene.state == 3 && rig.flags_set(),
               "reentrant ctx 1: dispatch not revised");
        expect(rig.consume(), "reentrant ctx 1: next consumption runs");
        expect(rig.scene.state == 0 && same(rig.log, {"shared"}),
               "reentrant ctx 1: new result 9 refused by ctx 4");
        expect(rig.listener_is(0, 9, 4), "reentrant ctx 1: pending cleared");
    }
    {
        // Writes made by the service before the context 1 writes are
        // overwritten by them, as the native writes follow the call.
        Rig rig;
        rig.arm(1, 0, 1);
        rig.shared.hook = [](ProfileMenuScene& scene) {
            scene.state = 9;
            scene.flag_34 = 0;
            scene.flag_35 = 0;
            scene.listener.result = 5;
        };
        rig.process();
        expect(rig.scene.state == 3 && rig.flags_set(),
               "service writes before ctx 1 writes: overwritten");
        expect(rig.listener_is(0, 5, 1),
               "direct result write kept, pending stays 0");
    }
    {
        Rig rig;
        rig.arm(1, 0, 2);
        rig.shared.hook = [](ProfileMenuScene& scene) {
            profile_result_callback(scene, 0, 0, 0);
        };
        rig.two.hook = [](ProfileMenuScene& scene) {
            scene.state = 4;
            scene.flag_35 = 1;
            scene.flag_34 = 1;
        };
        rig.process();
        expect(same(rig.log, {"shared", "two"}),
               "reentrant ctx 2: second service still called");
        expect(seen_at(rig.two, 0).pending == 1,
               "reentrant ctx 2: second service sees the new byte");
        expect(rig.listener_is(1, 0, 0) && rig.scene.state == 4 &&
                   rig.flags_set(),
               "reentrant ctx 2: port writes kept");
        expect(rig.consume() && rig.scene.state == 2,
               "reentrant ctx 2: next consumption takes ctx 0");
    }
    {
        // 007C5650 mode 2 may call the listener with (0, 0, 2) (7C5964).
        Rig rig;
        rig.arm(1, 0, 4);
        rig.action.hook = [&rig](ProfileMenuScene& scene) {
            if (rig.action.last_mode == 2)
                profile_result_callback(scene, 0, 0, 2);
        };
        rig.process();
        expect(rig.listener_is(1, 0, 2) && rig.scene.state == initial_state,
               "reentrant ctx 4: callback kept");
        expect(rig.consume(), "reentrant ctx 4: consumed again");
        expect(same(rig.log, {"action(2)", "shared", "two"}),
               "reentrant ctx 4: then ctx 2 services");
    }
    {
        Rig rig;
        rig.arm(1, 0, 4);
        rig.action.hook = [](ProfileMenuScene& scene) { scene.state = 3; };
        rig.process();
        expect(same(rig.log, {"action(2)"}) && rig.scene.state == 3,
               "ctx 4: state read before the call, port write kept");
    }
    {
        Rig rig;
        rig.arm(1, 0, 5);
        rig.five.hook = [](ProfileMenuScene& scene) {
            profile_result_callback(scene, 0, 0, 0);
        };
        rig.process();
        expect(rig.listener_is(1, 0, 0), "reentrant ctx 5: callback kept");
        expect(rig.scene.state == 1 && rig.flags_set(),
               "reentrant ctx 5: ctx 5 writes follow");
        expect(rig.consume() && rig.scene.state == 2,
               "reentrant ctx 5: next consumption takes ctx 0");
    }
}

void consume_cases() {
    {
        // Zero byte: no effect at all, whatever the stored values and the
        // profile pending byte.
        Rig rig;
        rig.arm(0, 0, 0);
        expect(!rig.consume(), "consume 0: false");
        expect(rig.log.empty() && rig.scene.state == initial_state &&
                   rig.flags_untouched() && rig.menu_untouched() &&
                   rig.listener_is(0, 0, 0),
               "consume 0: no effect despite profile pending 0x33");
    }
    for (const std::uint8_t pending :
         {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{0x80},
          std::uint8_t{0xFF}}) {
        Rig rig;
        rig.arm(pending, 0, 0);
        const std::string name = "consume " + hex(pending) + ": ";
        expect(rig.consume(), name + "true");
        expect(rig.scene.state == 2 && rig.flags_set(), name + "processed");
        expect(rig.listener_is(0, 0, 0), name + "pending cleared");
        expect(!rig.consume() && rig.scene.state == 2,
               name + "second consumption has no effect");
    }
    {
        Rig rig;
        rig.arm(1, 7, 1);
        expect(rig.consume() && rig.log.empty() &&
                   rig.scene.state == initial_state && rig.listener_is(0, 7, 1),
               "consume refused result: true, pending cleared only");
    }
    {
        // Same values as the absolute writes of 0074DEA0 (1, 0, 5) on the
        // observed instance; here on the owned scene.
        Rig rig;
        rig.arm(1, 0, 5);
        expect(rig.consume() && same(rig.log, {"five(0,1,0)"}) &&
                   rig.scene.state == 1,
               "consume (1, 0, 5): ctx 5 call and state 1");
    }
    {
        // Callback then consumption with a nonzero result in context 5.
        Rig rig;
        profile_result_callback(rig.scene, 0x80000000, 123, 5);
        expect(rig.consume() && same(rig.log, {"five(0,1,0)"}) &&
                   rig.scene.state == 1 && rig.flags_set() &&
                   rig.listener_is(0, 0x80000000, 5),
               "consume (1, 0x80000000, 5): ctx 5 call and state 1");
    }
}

struct DispatchRig : Rig {
    FakeRank rank;
    FakeService service;
    FakeCreation creation;

    bool run(ProfileMenuCommand command) {
        const ProfileMenuDispatchPorts ports{rank, service, creation, action};
        return profile_menu_dispatch(command, scene, ports);
    }
};

void coexistence_cases() {
    {
        // psel with another identity: command gate, mode 4, live scene. The
        // listener is not touched and a set profile pending byte is not a
        // result to consume.
        DispatchRig rig;
        rig.service.identity = 0x0000000200000006ull;
        rig.service.current = 0x0000000200000005ull;
        rig.arm(0, 0x77, 0x88);
        expect(rig.run(ProfileMenuCommand::select), "psel: true");
        expect(same(rig.log, {"action(4)"}) && rig.scene.state == 4,
               "psel: command gate unchanged");
        expect(rig.scene.menu.pending == 1, "psel: profile pending 1");
        expect(rig.listener_is(0, 0x77, 0x88), "psel: listener untouched");
        expect(!rig.consume() && same(rig.log, {"action(4)"}),
               "psel: profile pending is not the listener byte");
    }
    {
        // Mode 4 action calling the listener with (0, 0, 0), as on the
        // 7C57DC path; then the result is consumed. Selection survives.
        DispatchRig rig;
        rig.service.identity = 0x0000000300000005ull;
        rig.service.current = 0x0000000200000005ull;
        rig.action.hook = [&rig](ProfileMenuScene& scene) {
            if (rig.action.last_mode == 4)
                profile_result_callback(scene, 0, 0, 0);
        };
        rig.run(ProfileMenuCommand::select);
        expect(rig.listener_is(1, 0, 0) && rig.scene.menu.pending == 1,
               "psel callback: both bytes set, distinct");
        expect(rig.consume(), "psel callback: consumed");
        expect(rig.scene.state == 2 && rig.flags_set(),
               "psel callback: ctx 0 writes");
        expect(rig.scene.menu.pending == 1 &&
                   rig.scene.menu.selected == 0x0000000300000005ull,
               "psel callback: selection and profile pending kept");
    }
    {
        // pnew that leaves 0: no gate; an armed result is unaffected and
        // its processing leaves the profile pending byte at 0.
        DispatchRig rig;
        rig.creation.leave = 0;
        rig.arm(1, 0, 1);
        rig.run(ProfileMenuCommand::create);
        expect(rig.log.empty() && rig.listener_is(1, 0, 1),
               "pnew 0: listener untouched");
        expect(rig.consume() && same(rig.log, {"shared"}) &&
                   rig.scene.state == 3,
               "pnew 0: ctx 1 processed afterwards");
        expect(rig.scene.menu.pending == 0, "pnew 0: profile pending kept 0");
    }
    {
        // pnew that leaves 1, then ctx 1 and ctx 4 chain: state 3 written by
        // ctx 1 selects mode 1 on the same action port.
        DispatchRig rig;
        rig.creation.leave = 1;
        rig.run(ProfileMenuCommand::create);
        rig.arm(1, 0, 1);
        expect(rig.consume(), "chain: ctx 1 consumed");
        profile_result_callback(rig.scene, 0, 0, 4);
        expect(rig.consume(), "chain: ctx 4 consumed");
        expect(same(rig.log, {"action(4)", "shared", "action(1)"}),
               "chain: gate, ctx 1 service, then mode 1");
        expect(rig.scene.menu.pending == 1, "chain: profile pending kept");
    }
    {
        DispatchRig rig;
        rig.service.identity = 5;
        rig.service.current = 6;
        rig.arm(1, 3, 4);
        rig.run(ProfileMenuCommand::remove);
        expect(rig.listener_is(1, 3, 4) && rig.scene.state == initial_state,
               "pdel: listener and state untouched");
        expect(rig.consume() && rig.scene.state == 0,
               "pdel: result consumed afterwards");
    }
}

} // namespace

int main() {
    callback_cases();
    accepted_cases();
    nonzero_result_cases();
    unknown_contexts();
    reentrant_cases();
    consume_cases();
    coexistence_cases();
    std::printf("profile_menu_result: %d checks, %d failures\n", checks,
                failures);
    return failures == 0 ? 0 : 1;
}

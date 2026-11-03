// Contract tests for the profile command layer of router 007C5DE0 (pnew,
// psel, pdel) over the core. Ports share one event log and record the
// live scene they see at each call, so order, identity of the state and
// freshness of the pending byte are checked from the outside.
#include "tdur/game/profile_menu_dispatch.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace tdur::game;

int failures = 0;
int checks = 0;

void expect(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
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

struct FakeService final : ProfileServicePort {
    Log* log = nullptr;
    ProfileIdentity identity = 0;
    ProfileIdentity current = 0;

    void slot00(std::uint32_t, std::uint32_t) override {
        log->push_back("slot00");
    }
    ProfileIdentity slot04_current_identity() override {
        log->push_back("slot04");
        return current;
    }
    std::string slot08_text() override {
        log->push_back("slot08");
        return {};
    }
    std::string slot30_name_at(std::int32_t index) override {
        log->push_back("slot30(" + std::to_string(index) + ")");
        return "Alpha";
    }
    void slot40_name(std::string_view name) override {
        log->push_back("slot40(" + std::string(name) + ")");
    }
    ProfileIdentity slot44_identity_of(std::string_view name) override {
        log->push_back("slot44(" + std::string(name) + ")");
        return identity;
    }
};

struct FakeRank final : ProfileRankPort {
    Log* log = nullptr;
    std::int32_t menu_rank() override {
        log->push_back("rank");
        return 1;
    }
};

// What the creation port leaves in the pending byte. untouched keeps the
// value it found, so the gate must follow that value too.
enum class CreationResult { fail, succeed, untouched };

struct FakeCreation final : ProfileCreationPort {
    Log* log = nullptr;
    CreationResult result = CreationResult::succeed;
    const ProfileMenuFields* seen = nullptr;
    std::uint8_t pending_on_entry = 0xEE;

    void create_profile(ProfileMenuFields& menu) override {
        log->push_back("create");
        seen = &menu;
        pending_on_entry = menu.pending;
        if (result == CreationResult::fail)
            menu.pending = 0;
        else if (result == CreationResult::succeed)
            menu.pending = 1;
    }
};

struct FakeAction final : ProfileSceneActionPort {
    Log* log = nullptr;
    const ProfileMenuScene* expected_scene = nullptr;
    int calls = 0;
    std::uint32_t mode = 0;
    bool same_scene = false;
    std::uint32_t state_at_call = 0;
    std::uint8_t pending_at_call = 0;
    ProfileIdentity selected_at_call = 0;
    // Optional writes made by the callback itself.
    bool rewrite = false;
    std::uint32_t new_state = 0;
    std::uint8_t new_pending = 0;

    void mode_action(std::uint32_t value, ProfileMenuScene& scene) override {
        log->push_back("action(" + std::to_string(value) + ")");
        ++calls;
        mode = value;
        same_scene = &scene == expected_scene;
        state_at_call = scene.state;
        pending_at_call = scene.menu.pending;
        selected_at_call = scene.menu.selected;
        if (rewrite) {
            scene.state = new_state;
            scene.menu.pending = new_pending;
        }
    }
};

struct Rig {
    Log log;
    ProfileMenuScene scene;
    FakeService service;
    FakeRank rank;
    FakeCreation creation;
    FakeAction action;

    Rig() {
        service.log = &log;
        rank.log = &log;
        creation.log = &log;
        action.log = &log;
        action.expected_scene = &scene;
        scene.state = 7;
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    bool run(ProfileMenuCommand command) {
        const ProfileMenuDispatchPorts ports{rank, service, creation, action};
        return profile_menu_dispatch(command, scene, ports);
    }
};

void tag_cases() {
    using C = ProfileMenuCommand;
    expect(profile_menu_command_from_tag("pnew") == C::create, "tag: pnew");
    expect(profile_menu_command_from_tag("psel") == C::select, "tag: psel");
    expect(profile_menu_command_from_tag("pdel") == C::remove, "tag: pdel");
    expect(profile_menu_command_from_tag("pdel:7") == C::remove,
           "tag: bytes after four ignored");
    expect(!profile_menu_command_from_tag("pne"), "tag: short");
    expect(!profile_menu_command_from_tag(""), "tag: empty");
    expect(!profile_menu_command_from_tag("PNEW"), "tag: case");
    expect(!profile_menu_command_from_tag("pnex"), "tag: last byte");
    expect(!profile_menu_command_from_tag("prof"), "tag: other command");
    expect(!profile_menu_command_from_tag(std::string_view("pse\0", 4)),
           "tag: embedded NUL");
    expect(!profile_menu_command_from_tag("pne\xF7"), "tag: high byte");
    expect(!profile_menu_command_from_tag("\xF0nel"), "tag: high first");
}

void select_unchanged_current() {
    Rig rig;
    rig.service.identity = 0x0000000200000005ull;
    rig.service.current = 0x0000000200000005ull;
    rig.scene.menu.pending = 1; // left by an earlier command
    expect(rig.run(ProfileMenuCommand::select), "psel same: returns true");
    expect(same(rig.log, {"rank", "slot30(0)", "slot44(Alpha)", "slot04"}),
           "psel same: lookup only, no action");
    expect(rig.scene.menu.pending == 0, "psel same: fresh pending 0");
    expect(rig.scene.menu.selected == 0x0000000200000005ull,
           "psel same: selected current");
    expect(rig.scene.state == 7, "psel same: scene state untouched");
    expect(rig.action.calls == 0, "psel same: gate closed");
}

void select_gate(const char* name, ProfileIdentity identity,
                 ProfileIdentity current, std::int32_t mode) {
    Rig rig;
    rig.service.identity = identity;
    rig.service.current = current;
    rig.scene.menu.mode = mode;
    const bool handled = rig.run(ProfileMenuCommand::select);
    const std::string prefix = std::string("psel ") + name + ": ";
    auto check = [&](bool ok, const char* what) {
        expect(ok, (prefix + what).c_str());
    };
    check(handled, "returns true");
    check(same(rig.log,
               {"rank", "slot30(0)", "slot44(Alpha)", "slot04", "action(4)"}),
          "evaluation then one action");
    check(rig.action.calls == 1 && rig.action.mode == 4, "mode 4 once");
    check(rig.action.same_scene, "callback gets the live scene");
    check(rig.action.state_at_call == 4, "state 4 before the action");
    check(rig.action.pending_at_call == 1, "callback sees pending 1");
    check(rig.action.selected_at_call == identity,
          "callback sees the new selection");
    check(rig.scene.state == 4, "state 4 after");
    check(rig.scene.menu.pending == 1, "pending retained");
    check(rig.scene.menu.selected == identity, "selected kept");
}

void create_cases() {
    {
        Rig rig;
        rig.scene.menu.pending = 1; // stale value from an earlier command
        rig.creation.result = CreationResult::fail;
        expect(rig.run(ProfileMenuCommand::create), "pnew fail: true");
        expect(same(rig.log, {"create"}), "pnew fail: creation only");
        expect(rig.creation.seen == &rig.scene.menu,
               "pnew fail: port gets the live menu");
        expect(rig.creation.pending_on_entry == 1,
               "pnew fail: port sees the current byte");
        expect(rig.scene.menu.pending == 0, "pnew fail: pending 0");
        expect(rig.scene.state == 7, "pnew fail: state untouched");
    }
    {
        Rig rig;
        rig.creation.result = CreationResult::succeed;
        expect(rig.run(ProfileMenuCommand::create), "pnew ok: true");
        expect(same(rig.log, {"create", "action(4)"}),
               "pnew ok: creation then action");
        expect(rig.action.same_scene, "pnew ok: live scene");
        expect(rig.action.state_at_call == 4, "pnew ok: state 4 first");
        expect(rig.action.pending_at_call == 1, "pnew ok: pending seen 1");
        expect(rig.scene.menu.pending == 1, "pnew ok: pending retained");
        expect(rig.scene.state == 4, "pnew ok: state 4");
    }
    {
        Rig rig;
        rig.scene.menu.pending = 1;
        rig.creation.result = CreationResult::untouched;
        rig.run(ProfileMenuCommand::create);
        expect(same(rig.log, {"create", "action(4)"}),
               "pnew untouched 1: gate follows the byte");
        expect(rig.scene.menu.pending == 1, "pnew untouched 1: retained");
    }
    {
        Rig rig;
        rig.creation.result = CreationResult::untouched;
        rig.run(ProfileMenuCommand::create);
        expect(same(rig.log, {"create"}), "pnew untouched 0: no action");
        expect(rig.scene.state == 7, "pnew untouched 0: state kept");
    }
}

void remove_cases() {
    for (const bool differs : {true, false}) {
        Rig rig;
        rig.service.identity = differs ? 0x0000000100000005ull : 5;
        rig.service.current = 5;
        rig.scene.menu.pending = 1;
        rig.scene.menu.selected = 0xABCDull;
        rig.scene.menu.mode = 2;
        const bool handled = rig.run(ProfileMenuCommand::remove);
        expect(handled, "pdel: true");
        if (differs)
            expect(same(rig.log, {"rank", "slot30(0)", "slot44(Alpha)",
                                  "slot04", "slot40(Alpha)"}),
                   "pdel other: removal request only");
        else
            expect(
                same(rig.log, {"rank", "slot30(0)", "slot44(Alpha)", "slot04"}),
                "pdel current: no request");
        expect(rig.action.calls == 0, "pdel: no continuation");
        expect(rig.creation.seen == nullptr, "pdel: no creation");
        expect(rig.scene.state == 7, "pdel: scene state untouched");
        expect(rig.scene.menu.pending == 1, "pdel: pending untouched");
        expect(rig.scene.menu.selected == 0xABCDull,
               "pdel: selection untouched");
        expect(rig.scene.menu.mode == 2, "pdel: mode untouched");
    }
}

void callback_cases() {
    // The callback writes the live scene; nothing is restored afterwards.
    Rig rig;
    rig.creation.result = CreationResult::succeed;
    rig.action.rewrite = true;
    rig.action.new_state = 9;
    rig.action.new_pending = 0;
    rig.run(ProfileMenuCommand::create);
    expect(rig.action.state_at_call == 4, "callback: saw state 4");
    expect(rig.scene.state == 9, "callback: its state write survives");
    expect(rig.scene.menu.pending == 0, "callback: its pending write survives");

    // Any nonzero byte opens the gate, not only 1.
    Rig direct;
    direct.scene.menu.pending = 0x80;
    profile_menu_continue(direct.scene, direct.action);
    expect(direct.action.calls == 1 && direct.scene.state == 4,
           "continue: nonzero byte opens");
    expect(direct.scene.menu.pending == 0x80, "continue: byte not cleared");
    Rig closed;
    profile_menu_continue(closed.scene, closed.action);
    expect(closed.action.calls == 0 && closed.scene.state == 7,
           "continue: zero has no effect");
}

void unknown_command() {
    Rig rig;
    rig.scene.menu.pending = 1;
    const bool handled = rig.run(static_cast<ProfileMenuCommand>(0x70726F66));
    expect(!handled, "unknown: false");
    expect(rig.log.empty(), "unknown: no port call");
    expect(rig.scene.state == 7 && rig.scene.menu.pending == 1,
           "unknown: no write");
}

} // namespace

int main() {
    tag_cases();
    select_unchanged_current();
    select_gate("low half", 0x0000000200000006ull, 0x0000000200000005ull, 0);
    select_gate("high half", 0x0000000300000005ull, 0x0000000200000005ull, 0);
    select_gate("mode 1", 0x0000000200000005ull, 0x0000000200000005ull, 1);
    select_gate("mode -1", 0x0000000200000005ull, 0x0000000200000005ull, -1);
    create_cases();
    remove_cases();
    callback_cases();
    unknown_command();
    std::printf("profile_menu_dispatch: %d checks, %d failures\n", checks,
                failures);
    return failures == 0 ? 0 : 1;
}

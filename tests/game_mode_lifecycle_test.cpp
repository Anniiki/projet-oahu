#include "tdur/game/game_mode_lifecycle.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

namespace {
using namespace tdur::game;
int checks = 0;
int failures = 0;
void check(bool value, const char* label, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << line << ": " << label << '\n';
    }
}
#define CHECK(value) check((value), #value, __LINE__)

struct Member final : ModeSubsystem {
    int id;
    GameModeRuntime& state;
    std::vector<std::string>& trace;
    bool initialized = true;
    GameModeRuntime* seen_owner = nullptr;
    std::function<void()> forward, reverse, shutdown;
    Member(int number, GameModeRuntime& owner, std::vector<std::string>& log)
        : id(number), state(owner), trace(log) {}
    std::string number() const { return std::to_string(id); }
    ModeInitOutcome initialize(GameModeRuntime& owner) override {
        seen_owner = &owner;
        trace.push_back("i" + number() + ":" +
                        std::to_string(state.subsystem_count));
        return initialized ? ModeInitOutcome::initialized
                           : ModeInitOutcome::native_rejected;
    }
    void on_forward_pass() override {
        trace.push_back("f" + number());
        if (forward)
            forward();
    }
    void on_reverse_pass() override {
        trace.push_back("r" + number());
        if (reverse)
            reverse();
    }
    void on_shutdown() override {
        trace.push_back("s" + number() + ":" +
                        std::to_string(state.subsystem_count));
        if (shutdown)
            shutdown();
    }
};

struct Ports final : ModeLifecyclePorts {
    GameModeRuntime& state;
    std::vector<std::string> trace;
    std::vector<std::shared_ptr<Member>> members;
    std::uint32_t gate = 0x100; // Full DWORD is nonzero; its low byte is zero.
    bool completed = true;
    std::function<void()> end_effect;
    explicit Ports(GameModeRuntime& owner) : state(owner) {
        for (int i = 0; i < 13; ++i)
            members.push_back(std::make_shared<Member>(i, state, trace));
    }
    std::uint32_t startup_gate() override {
        trace.push_back("gate");
        return gate;
    }
    ModeSubsystemPtr boot_subsystem(std::size_t index) override {
        return members.at(index);
    }
    ModeCallOutcome begin_initialization(ModeSubsystem& member) override {
        const auto& m = static_cast<Member&>(member);
        trace.push_back("b" + m.number() + ":" +
                        std::to_string(member.startup_status) + ":" +
                        std::to_string(state.subsystem_count));
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome end_initialization(ModeSubsystem& member) override {
        const auto& m = static_cast<Member&>(member);
        trace.push_back("e" + m.number() + ":" +
                        std::to_string(state.subsystem_count));
        if (end_effect)
            end_effect();
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome configure_startup(GameModeRuntime& owner,
                                      ModeSubsystem& service) override {
        CHECK(&owner == &state);
        CHECK(&service == members[4].get());
        trace.push_back("config:" + std::to_string(state.subsystem_count));
        return ModeCallOutcome::native_returned;
    }
    ModeLifecycleStatus complete_startup(GameModeRuntime& owner) override {
        CHECK(&owner == &state);
        trace.push_back("finish");
        request_game_mode(owner.mode, 24);
        return completed ? ModeLifecycleStatus::success
                         : ModeLifecycleStatus::rejected;
    }
};

void phase_cases(const char* filename) {
    // Independently retained from the primary jump table, listings and strings.
    std::ifstream input(filename);
    CHECK(input.good());
    std::string line;
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::uint32_t mode = 0;
        std::string branch, label;
        CHECK(static_cast<bool>(row >> mode >> branch));
        std::getline(row >> std::ws, label);
        const auto phase = game_phase_for_mode(mode);
        if (label == "<default>") {
            CHECK(!phase);
        } else {
            CHECK(phase.has_value());
            if (phase) {
                CHECK(static_cast<std::uint32_t>(phase->phase) == mode);
                CHECK(phase->native_name == label);
            }
        }
        ++rows;
    }
    CHECK(rows == 30);
    for (const auto invalid : {0U, 31U, 0x80000000U, 0xFFFFFFFFU})
        CHECK(!game_phase_for_mode(invalid));
    CHECK(game_phase_for_mode(18)->phase == GamePhase::video_bumper);
    CHECK(game_phase_for_mode(0x18)->phase == GamePhase::login);
}

void mode_selection() {
    GameModeState state{24, 18, 0};
    CHECK(request_game_mode(state, 24));
    CHECK(state.current == 24 && state.previous == 18);
    CHECK(request_game_mode(state, 0x80000000U));
    CHECK(state.current == 0x80000000U && state.previous == 24);
    for (const auto guard : std::array<std::uint8_t, 4>{1, 2, 128, 255}) {
        state.blocked = guard;
        CHECK(!request_game_mode(state, 18));
        CHECK(state.current == 0x80000000U && state.previous == 24);
    }
}

void startup_order() {
    GameModeRuntime state;
    Ports ports(state);
    CHECK(initialize_mode_subsystems(state, ports) ==
          ModeLifecycleStatus::success);
    std::vector<std::string> expected{"gate"};
    for (int i = 0; i < 13; ++i) {
        if (i == 12)
            expected.push_back("config:12");
        const auto n = std::to_string(i);
        expected.push_back("b" + n + ":-1:" + n);
        expected.push_back("i" + n + ":" + n);
        expected.push_back("e" + n + ":" + n);
        CHECK(ports.members[i]->seen_owner == &state);
    }
    expected.push_back("finish");
    for (int i = 0; i < 13; ++i)
        expected.push_back("f" + std::to_string(i));
    CHECK(ports.trace == expected);
    CHECK(state.subsystem_count == 13);
    CHECK(state.mode.current == 24);

    ports.trace.clear();
    ports.gate = 0;
    CHECK(initialize_mode_subsystems(state, ports) ==
          ModeLifecycleStatus::rejected);
    CHECK(state.subsystem_count == 13);
    CHECK(ports.trace == std::vector<std::string>{"gate"});
}

void startup_failures_are_still_registered() {
    for (std::uint32_t failed = 0; failed < 13; ++failed) {
        GameModeRuntime state;
        Ports ports(state);
        ports.members[failed]->initialized = false;
        CHECK(initialize_mode_subsystems(state, ports) ==
              ModeLifecycleStatus::rejected);
        CHECK(state.subsystem_count == failed + 1);
        CHECK(ports.trace.back() ==
              "e" + std::to_string(failed) + ":" + std::to_string(failed));
        CHECK(std::find(ports.trace.begin(), ports.trace.end(), "finish") ==
              ports.trace.end());
        ports.trace.clear();
        CHECK(shutdown_mode_subsystems(state) == ModeLifecycleStatus::success);
        std::vector<std::string> expected;
        for (auto left = failed + 1; left > 0; --left) {
            const auto n = std::to_string(left - 1);
            expected.push_back("s" + n + ":" + n);
        }
        CHECK(ports.trace == expected);
        CHECK(state.subsystem_count == closed_subsystem_count);
    }
    GameModeRuntime state;
    Ports ports(state);
    ports.completed = false;
    CHECK(initialize_mode_subsystems(state, ports) ==
          ModeLifecycleStatus::rejected);
    CHECK(state.subsystem_count == 13 && state.mode.current == 24);
    CHECK(ports.trace.back() == "finish");
}

void live_passes() {
    GameModeRuntime state;
    Ports ports(state);
    state.subsystem_slots = {ports.members[0], ports.members[1],
                             ports.members[2]};
    state.subsystem_count = 3;
    ports.members[2]->reverse = [&] {
        state.subsystem_slots.push_back(ports.members[3]);
        state.subsystem_count = 4;
    };
    CHECK(reset_mode_subsystems(state) == ModeLifecycleStatus::success);
    CHECK(ports.trace ==
          (std::vector<std::string>{"r2", "r1", "r0", "f0", "f1", "f2", "f3"}));
    ports.trace.clear();
    state.subsystem_count = 3;
    ports.members[2]->reverse = [&] {
        state.subsystem_count = 0;
        state.subsystem_slots[0] = ports.members[4];
    };
    CHECK(reset_mode_subsystems(state) == ModeLifecycleStatus::success);
    CHECK(ports.trace == (std::vector<std::string>{"r2", "r1", "r4"}));

    ports.trace.clear();
    state.subsystem_slots = {ports.members[0]};
    state.subsystem_count = 1;
    ports.members[0]->forward = [&] {
        state.subsystem_slots.push_back(ports.members[1]);
        ++state.subsystem_count;
    };
    CHECK(forward_mode_subsystems(state) == ModeLifecycleStatus::success);
    CHECK(ports.trace == (std::vector<std::string>{"f0", "f1"}));
    ports.trace.clear();
    ports.members[0]->forward = [&] { state.subsystem_count = 0; };
    CHECK(forward_mode_subsystems(state) == ModeLifecycleStatus::success);
    CHECK(ports.trace == std::vector<std::string>{"f0"});
}

void shutdown_live_registration() {
    GameModeRuntime state;
    Ports ports(state);
    state.subsystem_slots = {ports.members[0], ports.members[1]};
    state.subsystem_count = 2;
    ports.members[1]->shutdown = [&] {
        CHECK(state.subsystem_count == 1);
        CHECK(register_mode_subsystem(state, ports.members[3], ports) ==
              ModeLifecycleStatus::success);
    };
    CHECK(shutdown_mode_subsystems(state) == ModeLifecycleStatus::success);
    CHECK(ports.trace == (std::vector<std::string>{"s1:1", "b3:-1:1", "i3:1",
                                                   "e3:1", "s3:1", "s0:0"}));
    CHECK(state.subsystem_count == closed_subsystem_count);
    CHECK(shutdown_mode_subsystems(state) ==
          ModeLifecycleStatus::invalid_registry);
}

void final_increment_after_end() {
    for (const auto count :
         std::array<std::uint32_t, 2>{closed_subsystem_count, 5}) {
        for (const auto initialized : {false, true}) {
            GameModeRuntime state;
            Ports ports(state);
            ports.members[0]->initialized = initialized;
            ports.end_effect = [&] { state.subsystem_count = count; };
            const auto result =
                register_mode_subsystem(state, ports.members[0], ports);
            CHECK(result == (initialized ? ModeLifecycleStatus::success
                                         : ModeLifecycleStatus::rejected));
            CHECK(state.subsystem_count == count + 1U);
            if (count == 5)
                CHECK(forward_mode_subsystems(state) ==
                      ModeLifecycleStatus::invalid_registry);
        }
    }
}

void invalid_registry() {
    GameModeRuntime state;
    Ports ports(state);
    CHECK(register_mode_subsystem(state, {}, ports) ==
          ModeLifecycleStatus::invalid_registry);
    state.subsystem_count = 1;
    CHECK(forward_mode_subsystems(state) ==
          ModeLifecycleStatus::invalid_registry);
    CHECK(reset_mode_subsystems(state) ==
          ModeLifecycleStatus::invalid_registry);
    CHECK(shutdown_mode_subsystems(state) ==
          ModeLifecycleStatus::invalid_registry);
    CHECK(ports.trace.empty());
    state.subsystem_count = 0;
    CHECK(shutdown_mode_subsystems(state) == ModeLifecycleStatus::success);
    CHECK(state.subsystem_count == closed_subsystem_count);
    CHECK(initialize_mode_subsystems(state, ports) ==
          ModeLifecycleStatus::success);
    CHECK(state.subsystem_count == 13);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    phase_cases(argv[1]);
    mode_selection();
    startup_order();
    startup_failures_are_still_registered();
    live_passes();
    shutdown_live_registration();
    final_increment_after_end();
    invalid_registry();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

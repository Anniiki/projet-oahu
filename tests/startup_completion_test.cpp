#include "tdur/game/startup_completion.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace tdur::game;
using namespace tdur::game::startup;
static_assert(std::is_abstract_v<DerivedStartupCompletion>);
int checks = 0, failures = 0;
void check(bool value, const char* label, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %d: %s\n", line, label);
    }
}
#define CHECK(value) check((value), #value, __LINE__)

// Only unresolved bodies are witnesses. Registry and helper calls use the
// product implementations; expected service identities come from primary ASM.
struct Service final : StartupServiceBase {
    int id;
    std::vector<std::string>& trace;
    ModeInitOutcome result = ModeInitOutcome::initialized;
    std::function<void()> after_init;
    Service(int number, std::vector<std::string>& log)
        : id(number), trace(log) {}
    ModeInitOutcome initialize(GameModeRuntime& owner) override {
        trace.push_back("init" + std::to_string(id));
        CHECK(startup_status == -1);
        if (result != ModeInitOutcome::modern_refusal)
            CHECK(initialize_9b3f80(*this, owner) == InitOutcome::completed);
        if (after_init)
            after_init();
        return result;
    }
    void on_forward_pass() override { CHECK(false); }
    void on_reverse_pass() override { CHECK(false); }
    void on_shutdown() override { CHECK(false); }
};

struct HelperGlobal final : HelperContextSlot {
    HelperContext context;
    HelperContext* load() override { return &context; }
};
struct NoAllocation final : AllocatorPort {
    Allocation allocate_slot20(std::uint32_t, std::uint32_t, std::uint32_t,
                               std::uint32_t) override {
        CHECK(false);
        return {};
    }
};
struct Lifecycle final : ModeLifecyclePorts {
    HelperGlobal global;
    NoAllocation allocator;
    std::vector<std::string>& trace;
    explicit Lifecycle(std::vector<std::string>& log) : trace(log) {}
    std::uint32_t startup_gate() override {
        CHECK(false);
        return 0;
    }
    ModeSubsystemPtr boot_subsystem(std::size_t) override {
        CHECK(false);
        return {};
    }
    ModeCallOutcome begin_initialization(ModeSubsystem& member) override {
        trace.push_back("begin");
        CHECK(initialization_helper_9b40c0(
                  dynamic_cast<StartupServiceBase&>(member), global,
                  allocator) == HelperOutcome::inactive);
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome end_initialization(ModeSubsystem& member) override {
        trace.push_back("end");
        CHECK(initialization_helper_9b4060(
                  dynamic_cast<StartupServiceBase&>(member), global,
                  allocator) == HelperOutcome::inactive);
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome configure_startup(GameModeRuntime&,
                                      ModeSubsystem&) override {
        CHECK(false);
        return ModeCallOutcome::modern_refusal;
    }
    ModeLifecycleStatus complete_startup(GameModeRuntime&) override {
        CHECK(false);
        return ModeLifecycleStatus::modern_refusal;
    }
};

struct Calls final : StartupCompletionPorts {
    GameModeRuntime& owner;
    StartupCompletionGlobals& globals;
    std::vector<std::string>& trace;
    StartupPathCopy copy_slot;
    std::optional<std::uint32_t> display_result = 0;
    std::string refused;
    int copy_loads = 0, copies = 0, allocations = 0, constructions = 0;
    int loops = 0, destroys = 0;
    int null_allocation = -1, short_allocation = -1, null_constructor = -1;
    std::vector<std::byte> storage = std::vector<std::byte>(0x5C50);
    StartupLoadingScenePtr latest;
    std::vector<std::uint32_t> seen_scene_fields;
    std::function<void(int)> after_copy, on_loop, on_destroy;
    std::function<void(const std::string&)> on_call;
    Calls(GameModeRuntime& runtime, StartupCompletionGlobals& state,
          std::vector<std::string>& log)
        : owner(runtime), globals(state), trace(log) {
        copy_slot = [&](std::span<char> destination, NativeIdentity path,
                        std::uint32_t count) {
            trace.push_back("copy");
            CHECK(destination.size() == 260 && count == 259);
            CHECK(std::all_of(destination.begin(), destination.end(),
                              [](char c) { return c == 0; }));
            CHECK(path.va == (copies == 0 ? 0xF0B3BCU : 0xF0B398U));
            std::fill_n(destination.begin(), count, 'p');
            const int number = copies++;
            if (after_copy)
                after_copy(number);
            return returned("copy");
        };
    }
    ModeCallOutcome returned(const std::string& name) {
        if (on_call)
            on_call(name);
        return refused == name ? ModeCallOutcome::modern_refusal
                               : ModeCallOutcome::native_returned;
    }
    StartupPathCopy load_copy_9ec36c() override {
        ++copy_loads;
        return copy_slot;
    }
    ModeCallOutcome call_71e8c0(NativeIdentity receiver, NativeIdentity arg1,
                                NativeIdentity arg2,
                                std::uint32_t arg3) override {
        CHECK(receiver.va == 0xF0B3AC && arg1.va == 0xF073C0 &&
              arg2.va == 0xF0B3B4 && arg3 == 0);
        CHECK(globals.byte_f995a8 == 1);
        CHECK(globals.mode_f995a4 == (copies == 1 ? 18U : 24U));
        CHECK(globals.path_f994a0.back() == '\0');
        trace.push_back("format");
        return returned("format");
    }
    Allocation allocate_1699104_slot20(std::uint32_t size, std::uint32_t a2,
                                       std::uint32_t a3,
                                       std::uint32_t a4) override {
        CHECK(size == 0x5C50 && a2 == 0 && a3 == 0 && a4 == 0);
        trace.push_back("allocate");
        const int index = allocations++;
        if (index == null_allocation)
            return {};
        return {storage.data(),
                index == short_allocation ? 4U : storage.size()};
    }
    StartupLoadingScenePtr construct_71eb80(Allocation block) override {
        CHECK(block.data == storage.data() && block.capacity == storage.size());
        trace.push_back("construct");
        if (constructions++ == null_constructor)
            return {};
        latest = std::make_shared<StartupLoadingScene>();
        latest->field_2c = 0xDEAD;
        return latest;
    }
    ModeCallOutcome call_97f260() override {
        CHECK(latest != nullptr);
        seen_scene_fields.push_back(latest->field_2c);
        trace.push_back("loop");
        const int index = loops++;
        if (on_loop)
            on_loop(index);
        return returned("loop");
    }
    ModeCallOutcome scene_slot0(StartupLoadingScene& scene,
                                std::uint32_t arg1) override {
        CHECK(&scene == latest.get() && arg1 == 1);
        trace.push_back("destroy");
        const int index = destroys++;
        if (on_destroy)
            on_destroy(index);
        return returned("destroy");
    }
    std::optional<std::uint32_t> call_5066e0(NativeIdentity receiver,
                                             std::uint32_t arg1) override {
        CHECK(receiver.va == 0xFAC4F0 && arg1 == 1);
        trace.push_back("display");
        return display_result;
    }
    ModeCallOutcome call_9b0190(NativeIdentity receiver) override {
        CHECK(receiver.va == 0xFA4068 && globals.dword_fa9cd4 == 0);
        trace.push_back("prepare");
        return returned("prepare");
    }
    ModeCallOutcome call_9a4930(NativeIdentity receiver) override {
        CHECK(receiver.va == 0x10E80C0);
        trace.push_back("drain");
        return returned("drain");
    }
    ModeCallOutcome call_98e090(NativeIdentity argument) override {
        CHECK(argument.va == 0x1140360);
        trace.push_back("config0");
        return returned("config0");
    }
    ModeCallOutcome call_98dbb0(NativeIdentity argument) override {
        CHECK(argument.va == 0x1140360 && owner.subsystem_count == 49);
        trace.push_back("config1");
        return returned("config1");
    }
    ModeCallOutcome call_98da20(NativeIdentity argument) override {
        CHECK(argument.va == 0x1140360 && owner.subsystem_count == 49);
        trace.push_back("config2");
        return returned("config2");
    }
};

struct Bench {
    GameModeRuntime owner;
    std::vector<std::string> trace;
    StartupCompletionState state{24};
    StartupCompletionGlobals globals;
    StartupGlobals shared;
    Object143F520 first, second;
    DerivedStartupServices services;
    Lifecycle lifecycle{trace};
    Calls calls{owner, globals, trace};
    Bench() {
        for (int i = 0; i < 13; ++i)
            owner.subsystem_slots.push_back(
                std::make_shared<Service>(-1, trace));
        owner.subsystem_count = 13;
        owner.mode = {3, 2, 0};
        shared.global_143f520 = &first;
        globals.object_faa5fc = &second;
        globals.dword_fa9cd4 = 55;
        globals.path_f994a0.fill('s');
        for (std::size_t i = 0; i < services.size(); ++i)
            services[i] = std::make_shared<Service>(static_cast<int>(i), trace);
    }
    StartupCompletionContext context() {
        return {state, globals, shared, services, calls};
    }
    ModeLifecycleStatus run() {
        return complete_startup_97c8f0(owner, lifecycle, context());
    }
    Service& service(std::size_t i) {
        return static_cast<Service&>(*services.at(i));
    }
};

void identities(const char* fixture) {
    std::ifstream input(fixture);
    CHECK(input.good());
    std::string row;
    std::size_t index = 0;
    while (std::getline(input, row)) {
        std::istringstream values(row);
        std::uint32_t site = 0, object = 0;
        CHECK(static_cast<bool>(values >> std::hex >> site >> object));
        CHECK(site >= 0x97CAF9 && site <= 0x97CD8F);
        if (index < derived_startup_service_count)
            CHECK(derived_startup_service_identities()[index].va == object);
        ++index;
    }
    CHECK(index == derived_startup_service_count);
}

void successful_order() {
    Bench b;
    b.service(37).after_init = [&] { b.shared.global_143f520 = &b.second; };
    CHECK(b.run() == ModeLifecycleStatus::success);
    std::vector<std::string> expected{"display", "prepare", "drain", "config0"};
    for (int i = 0; i < 38; ++i) {
        if (i == 36)
            expected.insert(expected.end(), {"config1", "config2"});
        expected.insert(expected.end(),
                        {"begin", "init" + std::to_string(i), "end"});
        CHECK(b.owner.subsystem_slots[13 + i] == b.services[i]);
        CHECK(b.services[i]->owner_10 == &b.owner);
    }
    CHECK(b.trace == expected);
    CHECK(b.owner.subsystem_count == 51);
    CHECK(b.globals.object_faa5fc == &b.second);
    CHECK(b.owner.mode.current == 24 && b.owner.mode.previous == 3);
    CHECK(b.calls.copy_loads == 0 && b.calls.allocations == 0);
}

void display_and_blocked_modes() {
    for (const auto flag : std::array<std::uint8_t, 3>{0, 1, 255}) {
        Bench b;
        b.globals.byte_11405e4 = flag;
        b.state.preferred_mode_e8 = flag == 0 ? 2U : 19U;
        b.calls.display_result = 4;
        CHECK(b.run() == ModeLifecycleStatus::rejected);
        CHECK(b.trace == std::vector<std::string>{"display"});
        CHECK(b.globals.dword_fa9cd4 == 55);
        CHECK(b.owner.subsystem_count == 13);
        CHECK(b.globals.object_faa5fc == &b.second);
        CHECK(b.owner.mode.current == b.state.preferred_mode_e8);
    }
    Bench refused;
    refused.calls.display_result.reset();
    CHECK(refused.run() == ModeLifecycleStatus::modern_refusal);
    CHECK(refused.trace == std::vector<std::string>{"display"});
    for (const auto guard : std::array<std::uint8_t, 4>{1, 2, 128, 255}) {
        Bench b;
        b.owner.mode.blocked = guard;
        CHECK(b.run() == ModeLifecycleStatus::success);
        CHECK(b.owner.mode.current == 3 && b.owner.mode.previous == 2);
    }
}

void movie_paths_and_live_state() {
    Bench b;
    b.globals.byte_11405e4 = 1;
    b.calls.after_copy = [&](int index) {
        if (index == 0) {
            CHECK(b.owner.mode.current == 18 && b.owner.mode.previous == 3);
            b.owner.mode.current = 7;
            // The native reuses its captured function pointer for copy 2.
            b.calls.copy_slot = [](auto, auto, auto) {
                CHECK(false);
                return ModeCallOutcome::modern_refusal;
            };
        }
    };
    b.calls.on_call = [&](const std::string& name) {
        if (name == "format" && b.calls.copies == 1)
            CHECK(b.owner.mode.current == 18 && b.owner.mode.previous == 7);
    };
    b.calls.on_loop = [&](int index) {
        if (index == 0) {
            b.owner.mode = {4, 3, 1};
            b.globals.byte_11405e4 = 0; // Does not cancel the second movie.
        }
    };
    b.calls.on_destroy = [&](int index) {
        if (index == 1) {
            CHECK(b.owner.mode.current == 4);
            b.owner.mode.blocked = 0;
        }
    };
    CHECK(b.run() == ModeLifecycleStatus::success);
    CHECK(b.calls.seen_scene_fields == (std::vector<std::uint32_t>{0, 0x30}));
    CHECK(b.calls.copy_loads == 1 && b.calls.copies == 2);
    CHECK(b.calls.allocations == 2 && b.calls.destroys == 2);
    CHECK(b.owner.mode.current == 24 && b.owner.mode.previous == 4);
    CHECK(b.globals.mode_f995a4 == 24 && b.globals.byte_f995a8 == 1);
    CHECK(std::find(b.trace.begin(), b.trace.end(), "display") ==
          b.trace.end());
    const std::vector<std::string> prefix{
        "copy",    "format",  "allocate", "construct", "loop",
        "destroy", "copy",    "format",   "allocate",  "construct",
        "loop",    "destroy", "prepare",  "drain",     "config0"};
    CHECK(std::equal(prefix.begin(), prefix.end(), b.trace.begin()));
}

void partial_service_outcomes() {
    for (std::size_t failed = 0; failed < 38; ++failed) {
        for (auto outcome : {ModeInitOutcome::native_rejected,
                             ModeInitOutcome::modern_refusal}) {
            Bench b;
            b.service(failed).result = outcome;
            const bool returned = outcome == ModeInitOutcome::native_rejected;
            CHECK(b.run() == (returned ? ModeLifecycleStatus::rejected
                                       : ModeLifecycleStatus::modern_refusal));
            CHECK(b.owner.subsystem_count == 13 + failed + (returned ? 1 : 0));
            CHECK(b.owner.subsystem_slots[13 + failed] == b.services[failed]);
            CHECK(b.trace.back() ==
                  (returned ? "end" : "init" + std::to_string(failed)));
            CHECK(b.globals.object_faa5fc == &b.second);
        }
    }
    Bench missing;
    missing.services[36].reset();
    CHECK(missing.run() == ModeLifecycleStatus::invalid_registry);
    CHECK(missing.owner.subsystem_count == 49);
    CHECK(missing.trace.back() == "config2");
}

void callee_and_storage_refusals() {
    for (const auto* point :
         {"prepare", "drain", "config0", "config1", "config2"}) {
        Bench b;
        b.calls.refused = point;
        CHECK(b.run() == ModeLifecycleStatus::modern_refusal);
        CHECK(b.trace.back() == point);
        CHECK(b.owner.subsystem_count ==
              (std::string(point).starts_with("config") && point[6] != '0'
                   ? 49U
                   : 13U));
        CHECK(b.globals.object_faa5fc == &b.second);
    }
    for (int index : {0, 1}) {
        for (int failure : {0, 1, 2}) {
            Bench b;
            b.globals.byte_11405e4 = 1;
            if (failure == 0)
                b.calls.null_allocation = index;
            if (failure == 1)
                b.calls.short_allocation = index;
            if (failure == 2)
                b.calls.null_constructor = index;
            CHECK(b.run() == ModeLifecycleStatus::modern_refusal);
            CHECK(b.calls.allocations == index + 1);
            CHECK(b.calls.loops == index && b.calls.destroys == index);
            CHECK(b.owner.subsystem_count == 13);
            CHECK(b.globals.dword_fa9cd4 == 2);
            CHECK(b.globals.mode_f995a4 == (index == 0 ? 18U : 24U));
            CHECK(b.owner.mode.current == 18);
        }
    }
    for (const auto* point : {"copy", "format", "loop", "destroy"}) {
        Bench b;
        b.globals.byte_11405e4 = 1;
        b.calls.refused = point;
        CHECK(b.run() == ModeLifecycleStatus::modern_refusal);
        CHECK(b.trace.back() == point && b.owner.subsystem_count == 13);
    }
    Bench no_copy;
    no_copy.globals.byte_11405e4 = 1;
    no_copy.calls.copy_slot = {};
    CHECK(no_copy.run() == ModeLifecycleStatus::modern_refusal);
    CHECK(no_copy.calls.allocations == 0 && no_copy.globals.byte_f995a8 == 0);
    CHECK(std::all_of(no_copy.globals.path_f994a0.begin(),
                      no_copy.globals.path_f994a0.end(),
                      [](char c) { return !c; }));
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    identities(argv[1]);
    successful_order();
    display_and_blocked_modes();
    movie_paths_and_live_state();
    partial_service_outcomes();
    callee_and_storage_refusals();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

#include "tdur/game/startup_completion.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace {
using namespace tdur::game;
using namespace tdur::game::startup;
namespace cfg = tdur::game::config;
namespace reg = tdur::game::config_registry;
int checks = 0, failures = 0;
void check(bool value, const char* label, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %d: %s\n", line, label);
    }
}
#define CHECK(value) check((value), #value, __LINE__)

// Only the unknown service/callee implementations are witnesses. The
// controller, both allocation/fill helpers and configuration driver are real.
struct Service final : StartupServiceBase {
    int id;
    std::vector<std::string>& trace;
    std::function<void()> after_init;
    ModeInitOutcome result = ModeInitOutcome::initialized;
    int initialized = 0;
    Service(int value, std::vector<std::string>& log) : id(value), trace(log) {}
    ModeInitOutcome initialize(GameModeRuntime& owner) override {
        ++initialized;
        trace.push_back("init" + std::to_string(id));
        CHECK(startup_status == -1);
        if (result == ModeInitOutcome::modern_refusal)
            return result;
        CHECK(initialize_9b3f80(*this, owner) == InitOutcome::completed);
        if (after_init)
            after_init();
        return result;
    }
    void on_forward_pass() override {
        trace.push_back("forward" + std::to_string(id));
    }
    void on_reverse_pass() override {
        trace.push_back("reverse" + std::to_string(id));
    }
    void on_shutdown() override {
        trace.push_back("shutdown" + std::to_string(id));
    }
};

struct Host final : LifecycleHostPorts {
    std::vector<std::string>& trace;
    std::uint32_t gate = 0x100;
    ModeLifecycleStatus completion = ModeLifecycleStatus::success;
    explicit Host(std::vector<std::string>& log) : trace(log) {}
    std::uint32_t startup_gate() override {
        trace.push_back("gate");
        return gate;
    }
    ModeLifecycleStatus complete_startup(GameModeRuntime&,
                                         ModeLifecyclePorts&) override {
        trace.push_back("complete");
        return completion;
    }
};

struct Global final : HelperContextSlot {
    HelperContext context;
    HelperContext* current = &context;
    int loads = 0;
    HelperContext* load() override {
        ++loads;
        return current;
    }
};

struct Allocator final : AllocatorPort {
    std::deque<std::vector<std::byte>> storage;
    int calls = 0;
    int bad_call = -1;
    std::function<void()> after_allocate;
    Allocation allocate_slot20(std::uint32_t size, std::uint32_t a2,
                               std::uint32_t a3, std::uint32_t a4) override {
        CHECK(a2 == 0 && a3 == 0 && a4 == 0);
        const auto capacity = calls++ == bad_call ? 1U : size;
        auto& block = storage.emplace_back(capacity, std::byte{0x77});
        if (after_allocate)
            after_allocate();
        return {block.data(), block.size()};
    }
};

struct ConfigurationPorts final : reg::Constructor9896c0Ports,
                                  cfg::ConfigLoaderPorts,
                                  cfg::SectionParserPorts,
                                  cfg::DriverPorts {
    std::vector<std::string>& trace;
    StartupServiceBase* fifth = nullptr;
    GameModeRuntime* owner = nullptr;
    bool unresolved_name = false, refused_load = false;
    int registrations = 0, external_loads = 0;
    explicit ConfigurationPorts(std::vector<std::string>& log) : trace(log) {}
    void call_4052b0_subobject_30(reg::ParserObject9896c0&) override {
        trace.push_back("configuration");
        CHECK(fifth->owner_10 == owner && fifth->flag_18 == 0);
        CHECK(owner->subsystem_count == 12);
    }
    void call_4377b0(reg::ParserObject9896c0&) override {}
    std::optional<std::string_view>
    read_indirect_name_slot(reg::ParserObject9896c0&, NativeIdentity,
                            std::size_t) override {
        return unresolved_name ? std::nullopt
                               : std::optional<std::string_view>{"fixture"};
    }
    void call_4378b0(reg::ParserObject9896c0&, const reg::RegistrationRecord&,
                     const reg::ResolvedName&) override {
        ++registrations;
    }
    Allocation allocate_1699104_slot20(std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t) override {
        return {};
    }
    reg::OpaquePair404dc0 call_404dc0(reg::ParserObject9896c0&,
                                      std::size_t) override {
        CHECK(false); // The witness leaves the native table count at zero.
        return {};
    }
    void select_block_6b18d0(std::span<const char>, cfg::LoadSlots&) override {}
    cfg::ExternalCallOutcome
    load_external_9a5d20(const cfg::ExternalLoadRequest&,
                         cfg::LoadSlots&) override {
        ++external_loads;
        return refused_load ? cfg::ExternalCallOutcome::modern_refusal
                            : cfg::ExternalCallOutcome::native_returned;
    }
    cfg::ParseCursor prefix_4381b0(const cfg::SectionParseCall&) override {
        CHECK(false); // No configuration block is supplied by this witness.
        return {};
    }
    cfg::HelperCarry commands_4383b0(const cfg::SectionParseCall&,
                                     cfg::HelperCarry value,
                                     cfg::ParserObject&) override {
        CHECK(false);
        return value;
    }
    cfg::HelperCarry section_438250(const cfg::SectionParseCall&,
                                    cfg::HelperCarry value, std::uint8_t&,
                                    std::uint8_t&) override {
        CHECK(false);
        return value;
    }
    void copy_9ec36c(std::span<char> dest, cfg::NativeIdentity,
                     std::optional<std::string_view> value,
                     std::uint32_t) override {
        // Explicit source-owned input for unresolved copy/INI functions.
        const auto text = value.value_or("fixture");
        std::copy_n(text.begin(), std::min(text.size(), dest.size()),
                    dest.begin());
        if (text.size() < dest.size())
            dest[text.size()] = '\0';
    }
    cfg::HelperCallOutcome override_986b60(cfg::NativeIdentity,
                                           std::string_view, std::span<char>,
                                           std::uint32_t,
                                           cfg::Inputs986b60) override {
        return cfg::HelperCallOutcome::native_returned;
    }
    cfg::HelperCallOutcome call_98dd20(StartupServiceBase&) override {
        return cfg::HelperCallOutcome::native_returned;
    }
    cfg::HelperCallOutcome call_98de80(StartupServiceBase&,
                                       std::uint32_t) override {
        return cfg::HelperCallOutcome::native_returned;
    }
    void call_9a4750(cfg::NativeIdentity) override { CHECK(false); }
    void call_9a6040(cfg::NativeIdentity, std::uint32_t) override {
        CHECK(false);
    }
    void release_6b19c0(cfg::BlockHandle, std::uint32_t,
                        std::uint8_t&) override {
        CHECK(false);
    }
    void free_674e0c(cfg::BlockHandle) override { CHECK(false); }
    void call_87b430(cfg::NativeIdentity, cfg::Block11e2f3a&,
                     std::uint32_t) override {}
    cfg::HelperCallOutcome destroy_988fc0(cfg::ParserObject&) override {
        trace.push_back("destroy-configuration");
        return cfg::HelperCallOutcome::native_returned;
    }
};

struct Bench {
    GameModeRuntime owner;
    std::vector<std::string> trace;
    Host host{trace};
    Global global;
    Allocator allocator;
    BootServices services;
    cfg::DriverContext driver;
    cfg::BlockStore store;
    cfg::SectionParserState parser;
    ConfigurationPorts ports{trace};
    StartupLifecycleBinding binding{
        services,
        global,
        allocator,
        {driver, store, parser, {ports, ports, ports, ports}},
        host};
    Bench() {
        for (std::size_t i = 0; i < services.size(); ++i)
            services[i] = std::make_shared<Service>(static_cast<int>(i), trace);
        ports.fifth = services[4].get();
        ports.owner = &owner;
        service(11).after_init = [&] {
            services[4]->flag_18 = 7;
            services[4]->owner_10 = nullptr;
        };
    }
    Service& service(std::size_t i) {
        return static_cast<Service&>(*services.at(i));
    }
    ModeLifecycleStatus run() {
        return initialize_mode_subsystems(owner, binding);
    }
};

// These unresolved callees are witnesses. The complete 13 + 38 startup
// sequence below uses the actual controller, both helpers and both drivers.
struct CompletionCalls final : StartupCompletionPorts {
    std::vector<std::string>& trace;
    explicit CompletionCalls(std::vector<std::string>& log) : trace(log) {}
    StartupPathCopy load_copy_9ec36c() override {
        CHECK(false);
        return {};
    }
    ModeCallOutcome call_71e8c0(NativeIdentity, NativeIdentity, NativeIdentity,
                                std::uint32_t) override {
        CHECK(false);
        return ModeCallOutcome::modern_refusal;
    }
    Allocation allocate_1699104_slot20(std::uint32_t, std::uint32_t,
                                       std::uint32_t, std::uint32_t) override {
        CHECK(false);
        return {};
    }
    StartupLoadingScenePtr construct_71eb80(Allocation) override {
        CHECK(false);
        return {};
    }
    ModeCallOutcome call_97f260() override {
        CHECK(false);
        return ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome scene_slot0(StartupLoadingScene&, std::uint32_t) override {
        CHECK(false);
        return ModeCallOutcome::modern_refusal;
    }
    std::optional<std::uint32_t> call_5066e0(NativeIdentity,
                                             std::uint32_t) override {
        trace.push_back("display");
        return 0;
    }
    ModeCallOutcome call_9b0190(NativeIdentity) override {
        trace.push_back("prepare");
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome call_9a4930(NativeIdentity) override {
        trace.push_back("drain");
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome call_98e090(NativeIdentity) override {
        trace.push_back("game-config0");
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome call_98dbb0(NativeIdentity) override {
        trace.push_back("game-config1");
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome call_98da20(NativeIdentity) override {
        trace.push_back("game-config2");
        return ModeCallOutcome::native_returned;
    }
};

void whole_startup_composition() {
    Bench b;
    DerivedStartupServices services;
    for (std::size_t i = 0; i < services.size(); ++i)
        services[i] =
            std::make_shared<Service>(static_cast<int>(13 + i), b.trace);
    StartupCompletionState state{24};
    StartupCompletionGlobals globals;
    StartupGlobals shared;
    Object143F520 object;
    shared.global_143f520 = &object;
    CompletionCalls calls{b.trace};
    // Explicit gate witness. The product host stays abstract; the unresolved
    // nonordinary gate is not replaced by this test value in product code.
    struct Host97c8f0 final : DerivedStartupCompletion {
        using DerivedStartupCompletion::DerivedStartupCompletion;
        std::uint32_t startup_gate() override { return 0x100; }
    } host{{state, globals, shared, services, calls}};
    StartupLifecycleBinding binding{
        b.services,
        b.global,
        b.allocator,
        {b.driver, b.store, b.parser, {b.ports, b.ports, b.ports, b.ports}},
        host};
    CHECK(initialize_mode_subsystems(b.owner, binding) ==
          ModeLifecycleStatus::success);
    CHECK(b.owner.subsystem_count == 51 && b.global.loads == 102);
    CHECK(b.allocator.calls == 0 && b.ports.registrations == 375);
    CHECK(globals.object_faa5fc == &object && b.owner.mode.current == 24);
    for (std::size_t i = 0; i < services.size(); ++i) {
        CHECK(b.owner.subsystem_slots[13 + i] == services[i]);
        CHECK(services[i]->owner_10 == &b.owner);
    }
    const auto config =
        std::find(b.trace.begin(), b.trace.end(), "game-config1");
    CHECK(config != b.trace.end() && *(config - 1) == "init48" &&
          *(config + 1) == "game-config2" && *(config + 2) == "init49");
    CHECK(std::count_if(b.trace.begin(), b.trace.end(), [](const auto& event) {
              return event.starts_with("forward");
          }) == 51);
    CHECK(shutdown_mode_subsystems(b.owner) == ModeLifecycleStatus::success);
    CHECK(b.trace.back() == "shutdown0");
}

void full_sequence() {
    Bench b;
    CHECK(!b.binding.last_begin() && !b.binding.last_end() &&
          !b.binding.last_configuration());
    CHECK(b.run() == ModeLifecycleStatus::success);
    std::vector<std::string> expected{"gate"};
    for (int i = 0; i != 12; ++i)
        expected.push_back("init" + std::to_string(i));
    expected.insert(expected.end(), {"configuration", "destroy-configuration",
                                     "init12", "complete"});
    for (int i = 0; i != 13; ++i)
        expected.push_back("forward" + std::to_string(i));
    CHECK(b.trace == expected);
    CHECK(b.owner.subsystem_count == 13);
    // GamePC, Physics and Replay; no adddevice override was supplied.
    CHECK(b.ports.registrations == 375 && b.ports.external_loads == 3);
    CHECK(b.binding.last_configuration()->outcome ==
          cfg::DriverOutcome::completed);
    CHECK(b.global.loads == 26 && b.allocator.calls == 0);
    for (std::size_t i = 0; i != b.services.size(); ++i)
        CHECK(b.owner.subsystem_slots[i] == b.services[i]);
    CHECK(shutdown_mode_subsystems(b.owner) == ModeLifecycleStatus::success);
    CHECK(b.owner.subsystem_count == closed_subsystem_count);
    CHECK(b.trace.back() == "shutdown0");
}

void helper_refusals() {
    for (int bad : {0, 1}) {
        Bench b;
        b.global.context = {1, 4};
        b.allocator.bad_call = bad;
        CHECK(b.run() == ModeLifecycleStatus::modern_refusal);
        CHECK(b.owner.subsystem_count == 0);
        CHECK(b.owner.subsystem_slots[0] == b.services[0]);
        CHECK(b.service(0).initialized == bad);
        CHECK(!b.binding.last_configuration());
        CHECK(b.allocator.storage.back() ==
              std::vector<std::byte>(1, std::byte{0x77}));
        if (bad == 0) {
            CHECK(b.binding.last_begin() ==
                  HelperOutcome::refused_fill_exceeds_storage);
            CHECK(!b.binding.last_end());
        } else {
            CHECK(b.binding.last_end() ==
                  HelperOutcome::refused_fill_exceeds_storage);
            CHECK(b.allocator.storage.front() ==
                  std::vector<std::byte>(4, std::byte{0xBD}));
        }
    }
    Bench missing;
    missing.global.current = nullptr;
    CHECK(missing.run() == ModeLifecycleStatus::modern_refusal);
    CHECK(missing.binding.last_begin() ==
          HelperOutcome::refused_missing_context);
    CHECK(missing.trace == std::vector<std::string>{"gate"});
    Bench live;
    live.global.context = {1, 4};
    HelperContext changed{0, 2};
    live.allocator.after_allocate = [&] { live.global.current = &changed; };
    CHECK(register_mode_subsystem(live.owner, live.services[0], live.binding) ==
          ModeLifecycleStatus::success);
    CHECK(live.allocator.storage.front() ==
          (std::vector<std::byte>{std::byte{0xBD}, std::byte{0xBD},
                                  std::byte{0x77}, std::byte{0x77}}));
    CHECK(live.binding.last_end() == HelperOutcome::inactive);
}

void initialization_result_distinction() {
    for (const auto outcome :
         {ModeInitOutcome::native_rejected, ModeInitOutcome::modern_refusal}) {
        Bench b;
        b.service(0).result = outcome;
        CHECK(b.run() == (outcome == ModeInitOutcome::native_rejected
                              ? ModeLifecycleStatus::rejected
                              : ModeLifecycleStatus::modern_refusal));
        CHECK(b.owner.subsystem_count ==
              (outcome == ModeInitOutcome::native_rejected ? 1U : 0U));
        CHECK(b.binding.last_end().has_value() ==
              (outcome == ModeInitOutcome::native_rejected));
        CHECK(b.service(0).initialized == 1);
        CHECK(b.service(1).initialized == 0);
    }
}

void configuration_refusal() {
    for (bool at_load : {false, true}) {
        Bench b;
        b.ports.unresolved_name = !at_load;
        b.ports.refused_load = at_load;
        CHECK(b.run() == ModeLifecycleStatus::modern_refusal);
        CHECK(b.owner.subsystem_count == 12);
        CHECK(b.service(12).initialized == 0);
        CHECK(b.trace.back() == "configuration");
        CHECK(b.binding.last_configuration()->outcome ==
              (at_load ? cfg::DriverOutcome::refused_loader
                       : cfg::DriverOutcome::refused_parser_construction));
        CHECK(b.ports.external_loads == (at_load ? 1 : 0));
        CHECK(b.services[4]->owner_10 == &b.owner &&
              b.services[4]->flag_18 == 0);
    }
}

void gate_and_derived_completion() {
    Bench gate;
    gate.host.gate = 0;
    CHECK(gate.run() == ModeLifecycleStatus::rejected);
    CHECK(gate.trace == std::vector<std::string>{"gate"});
    CHECK(gate.global.loads == 0 && !gate.binding.last_configuration());
    Bench derived;
    derived.host.completion = ModeLifecycleStatus::rejected;
    CHECK(derived.run() == ModeLifecycleStatus::rejected);
    CHECK(derived.owner.subsystem_count == 13);
    CHECK(derived.trace.back() == "complete");
    for (auto result : {ModeLifecycleStatus::modern_refusal,
                        ModeLifecycleStatus::invalid_registry}) {
        Bench refusal;
        refusal.host.completion = result;
        CHECK(refusal.run() == result);
        CHECK(refusal.owner.subsystem_count == 13);
        CHECK(refusal.trace.back() == "complete");
    }
}
} // namespace

int main() {
    full_sequence();
    helper_refusals();
    initialization_result_distinction();
    configuration_refusal();
    gate_and_derived_completion();
    whole_startup_composition();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

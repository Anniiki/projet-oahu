#pragma once

#include "tdur/game/config_aux_986b60.hpp"
#include "tdur/game/config_aux_988fc0.hpp"
#include "tdur/game/startup_completion.hpp"

namespace tdur::game::config {

template <std::size_t Capacity> struct StartupConfigFrame {
    std::array<char, Capacity> path{};
    // Caller stack outputs are not cleared before loading a subsequent file.
    LoadSlots slots;
};
using EffectsConfigFrame = StartupConfigFrame<0x100>;
using RadioConfigFrame = StartupConfigFrame<0x100>;
using LeaderboardsConfigFrame = StartupConfigFrame<0x200>;

struct StartupConfigGlobals {
    std::uint8_t integrated_first_f68bf0 = 0;
    std::uint8_t service_29b = 0; // Field of the same 1140360 service.
};

struct StartupPathFormat {
    NativeIdentity format;
    std::string_view verified_format;
    NativeIdentity prefix; // 1140444 or 11404C4, selected at this call.
    std::optional<NativeIdentity> second_argument;
    std::optional<std::string_view> verified_second_argument;
};

struct IntegratedConfigReturn {
    HelperCallOutcome control = HelperCallOutcome::modern_refusal;
    std::optional<BlockHandle> block; // Native EAX, null is not a refusal.
};

struct SynchronousConfigCall {
    NativeIdentity service{0x10E80C0};
    std::optional<BlockHandle>& block; // ECX at the call.
    std::span<const char> path;
    std::uint32_t& size; // Third stack argument, passed by reference.
    std::array<std::uint32_t, 3> trailing_arguments{};
    std::uint32_t eax_at_call = 0;
};

// Only unresolved callees are ports. Neither 6B1A00 nor 9A5D20 is called
// by these three drivers, so those loader implementations are not substituted.
class StartupConfigPorts {
  public:
    virtual ~StartupConfigPorts() = default;
    virtual std::optional<std::uint8_t> read_selector_1140370_e0() = 0;
    virtual HelperCallOutcome format_9ec378(std::span<char> destination,
                                            const StartupPathFormat& call) = 0;
    virtual IntegratedConfigReturn lookup_9aff20(NativeIdentity receiver,
                                                 std::span<const char> path,
                                                 std::uint32_t& size) = 0;
    // Empty optional: modern refusal. Otherwise raw AL; every nonzero byte
    // selects the external-success path, with the live output slot.
    virtual std::optional<std::uint8_t>
    load_9a5a70(const SynchronousConfigCall& call) = 0;
    virtual HelperCallOutcome free_674e0c(BlockHandle block) = 0;
};

struct StartupConfigEnvironment {
    DriverContext& args_globals;
    StartupConfigGlobals& globals;
    BlockStore& store;
    SectionParserState& section_state;
    EffectsConfigFrame& effects;
    RadioConfigFrame& radio;
    LeaderboardsConfigFrame& leaderboards;
    config_registry::Constructor9896c0Ports& constructor;
    SectionParserPorts& sections;
    ArgsTablePorts& args;
    ParserTeardownPorts& teardown;
    StartupConfigPorts& calls;
};

enum class StartupConfigOutcome {
    native_return,
    refused_constructor,
    refused_selector,
    refused_format,
    refused_args,
    refused_integrated_lookup,
    refused_external_load,
    refused_null_parse_block,
    refused_section,
    refused_free,
    refused_teardown
};

struct StartupConfigFileTrace {
    bool integrated_called = false;
    bool external_called = false;
    bool external_al_nonzero = false;
    bool free_called = false;
    std::optional<BlockHandle> parsed_block;
};

struct StartupConfigResult {
    StartupConfigOutcome outcome = StartupConfigOutcome::native_return;
    std::size_t file_index = 0;
    std::string_view refused_section;
    std::uint32_t section_calls = 0;
    std::array<StartupConfigFileTrace, 5> files;
    config_registry::ConstructResult construction;
    std::optional<Result986b60> lookup;
    std::optional<Result988fc0> teardown;
    std::optional<SectionParseResult> section;
};

// Complete primary callers. Each owns one parser throughout its loads and
// reaches 988FC0 only on a native-return path. These functions have RET 4,
// not a tested native AL success value.
StartupConfigResult
run_effects_config_98e090(startup::StartupServiceBase& service,
                          StartupConfigEnvironment env);
StartupConfigResult
run_radio_config_98dbb0(startup::StartupServiceBase& service,
                        StartupConfigEnvironment env);
StartupConfigResult
run_leaderboards_config_98da20(startup::StartupServiceBase& service,
                               StartupConfigEnvironment env);

// Supply the three real caller bodies to the derived completion driver.
// Other unresolved loading/scene/preparation calls remain with inner.
class StartupConfigurationBinding final
    : public startup::StartupCompletionPorts {
  public:
    StartupConfigurationBinding(startup::StartupCompletionPorts& inner,
                                startup::StartupServiceBase& service,
                                StartupConfigEnvironment env)
        : inner_(inner), service_(service), env_(env) {}
    startup::StartupPathCopy load_copy_9ec36c() override {
        return inner_.load_copy_9ec36c();
    }
    ModeCallOutcome call_71e8c0(startup::NativeIdentity r,
                                startup::NativeIdentity a,
                                startup::NativeIdentity b,
                                std::uint32_t c) override {
        return inner_.call_71e8c0(r, a, b, c);
    }
    startup::Allocation allocate_1699104_slot20(std::uint32_t a,
                                                std::uint32_t b,
                                                std::uint32_t c,
                                                std::uint32_t d) override {
        return inner_.allocate_1699104_slot20(a, b, c, d);
    }
    startup::StartupLoadingScenePtr
    construct_71eb80(startup::Allocation storage) override {
        return inner_.construct_71eb80(storage);
    }
    ModeCallOutcome call_97f260() override { return inner_.call_97f260(); }
    ModeCallOutcome scene_slot0(startup::StartupLoadingScene& scene,
                                std::uint32_t arg) override {
        return inner_.scene_slot0(scene, arg);
    }
    std::optional<std::uint32_t> call_5066e0(startup::NativeIdentity receiver,
                                             std::uint32_t arg) override {
        return inner_.call_5066e0(receiver, arg);
    }
    ModeCallOutcome call_9b0190(startup::NativeIdentity r) override {
        return inner_.call_9b0190(r);
    }
    ModeCallOutcome call_9a4930(startup::NativeIdentity r) override {
        return inner_.call_9a4930(r);
    }
    ModeCallOutcome call_98e090(startup::NativeIdentity arg) override;
    ModeCallOutcome call_98dbb0(startup::NativeIdentity arg) override;
    ModeCallOutcome call_98da20(startup::NativeIdentity arg) override;

    std::optional<StartupConfigResult> last_effects, last_radio,
        last_leaderboards;

  private:
    startup::StartupCompletionPorts& inner_;
    startup::StartupServiceBase& service_;
    StartupConfigEnvironment env_;
};

} // namespace tdur::game::config

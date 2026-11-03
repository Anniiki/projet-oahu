#include "tdur/game/startup_config_drivers.hpp"

#include "tdur/game/config_registry_9896c0_table.hpp"

#include <bit>

namespace tdur::game::config {
namespace {
struct FileSpec {
    NativeIdentity format;
    std::string_view text;
    std::optional<NativeIdentity> second;
    std::optional<std::string_view> second_text;
};
constexpr FileSpec effects_files[]{
    {{0xF0A490}, "%sFX/HDRIConf.ini", {}, {}},
    {{0xF0A470}, "%sFX/Weather.ini", {}, {}},
    {{0xF0A448}, "%sFX/Weather_HDRI.ini", {}, {}},
    {{0xF0A430}, "%sFX/%s", NativeIdentity{0xF0A438}, "WeatherDesc.ini"},
    {{0xF0A418}, "%sFX/FX.ini", {}, {}},
};
constexpr std::string_view effects_single_sections[]{
    "HDRICONF", "WEATHERCONFIG", "WEATHERCONFIG", "WEATHERDESC"};
constexpr std::string_view effects_final_sections[]{
    "CAR",  "OCEAN",    "GLOW-INGAME",       "PAUSE",     "GRASS",
    "HDRI", "HDRICONF", "AVATARLIGHTFACTOR", "REFLECTION"};
constexpr FileSpec radio_file{
    {0xF0A2D0}, "%sSound/Radio/%s", NativeIdentity{0xF76574}, "Radio.ini"};
constexpr FileSpec leaderboards_file{
    {0xF0A2A4}, "%sDatabase/Leaderboards.ini", {}, {}};

bool returned(HelperCallOutcome outcome) {
    return outcome == HelperCallOutcome::native_returned;
}

struct LoadedFile {
    std::optional<BlockHandle> block;
    bool external = false;
    bool skip = false;
};

class Run {
  public:
    Run(StartupConfigEnvironment env, StartupConfigResult& result)
        : env_(env), result_(result) {}
    bool fail(StartupConfigOutcome outcome) {
        result_.outcome = outcome;
        return false;
    }
    bool construct(startup::StartupServiceBase& service) {
        result_.construction = config_registry::construct_9896c0(
            parser_.registry, std::bit_cast<startup::OpaqueWord>(&service),
            config_registry::kRegistrations9896c0, env_.constructor);
        return result_.construction.outcome ==
                   config_registry::ConstructOutcome::completed ||
               fail(StartupConfigOutcome::refused_constructor);
    }
    bool format(std::span<char> path, const FileSpec& file) {
        const auto selector = env_.calls.read_selector_1140370_e0();
        if (!selector)
            return fail(StartupConfigOutcome::refused_selector);
        const StartupPathFormat call{file.format,
                                     file.text,
                                     {*selector ? 0x11404C4U : 0x1140444U},
                                     file.second,
                                     file.second_text};
        return returned(env_.calls.format_9ec378(path, call)) ||
               fail(StartupConfigOutcome::refused_format);
    }
    bool args(std::span<char> path, std::uint32_t bound, NativeIdentity key,
              std::string_view text) {
        const Inputs986b60 inputs{env_.args_globals.global_10e7350,
                                  env_.args_globals.global_10e734c};
        result_.lookup = run_args_lookup_986b60(key, text, path.first(bound),
                                                bound, inputs, env_.args);
        return result_.lookup->outcome == Outcome986b60::native_return ||
               fail(StartupConfigOutcome::refused_args);
    }
    bool load(std::span<const char> path, LoadSlots& slots,
              bool store_fallback_block, LoadedFile& loaded) {
        auto& trace = result_.files[result_.file_index];
        const bool integrated_first = env_.globals.integrated_first_f68bf0 != 0;
        auto integrated = [&](bool store_block) {
            trace.integrated_called = true;
            const auto value =
                env_.calls.lookup_9aff20({0xFA4068}, path, slots.size);
            if (!returned(value.control))
                return fail(StartupConfigOutcome::refused_integrated_lookup);
            loaded.block = value.block;
            if (store_block)
                slots.block = value.block;
            return true;
        };
        auto external = [&] {
            trace.external_called = true;
            const auto al = env_.calls.load_9a5a70(
                {{0x10E80C0}, slots.block, path, slots.size, {}, 0});
            if (!al)
                return fail(StartupConfigOutcome::refused_external_load);
            trace.external_al_nonzero = *al != 0;
            if (*al != 0) {
                loaded.block = slots.block;
                loaded.external = true;
            }
            return true;
        };
        if (integrated_first) {
            if (!integrated(true))
                return false;
            if (!loaded.block && !external())
                return false;
        } else {
            if (!external())
                return false;
            if (!loaded.external && !integrated(store_fallback_block))
                return false;
        }
        // External AL=true reaches 438450 even with a null output. It is
        // not the native "absent integrated file" path. Refuse that missing
        // owned object rather than fabricating a successful parser call.
        if (loaded.external && !loaded.block)
            return fail(StartupConfigOutcome::refused_null_parse_block);
        loaded.skip = !loaded.block;
        trace.parsed_block = loaded.block;
        return true;
    }
    bool section(const LoadedFile& loaded, std::uint32_t size,
                 std::string_view name) {
        ++result_.section_calls;
        result_.section = run_config_section_parser_438450(
            env_.store, *loaded.block, size, name, parser_, env_.section_state,
            env_.sections);
        if (result_.section->outcome != SectionParseOutcome::native_returned) {
            result_.refused_section = name;
            return fail(StartupConfigOutcome::refused_section);
        }
        return true;
    }
    bool release(const LoadedFile& loaded) {
        if (!loaded.external || !loaded.block)
            return true;
        result_.files[result_.file_index].free_called = true;
        return returned(env_.calls.free_674e0c(*loaded.block)) ||
               fail(StartupConfigOutcome::refused_free);
    }
    void teardown() {
        result_.teardown = run_parser_teardown_988fc0(parser_, env_.teardown);
        if (result_.teardown->outcome != Outcome988fc0::native_return)
            fail(StartupConfigOutcome::refused_teardown);
    }

  private:
    StartupConfigEnvironment env_;
    StartupConfigResult& result_;
    ParserObject parser_;
};

ModeCallOutcome completion_outcome(const StartupConfigResult& result) {
    return result.outcome == StartupConfigOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace

StartupConfigResult
run_effects_config_98e090(startup::StartupServiceBase& service,
                          StartupConfigEnvironment env) {
    StartupConfigResult result;
    Run run{env, result};
    if (!run.construct(service))
        return result;
    // Unlike the other callers, the native does not clear this path buffer.
    auto& frame = env.effects;
    for (std::size_t file = 0; file < std::size(effects_files); ++file) {
        result.file_index = file;
        LoadedFile loaded;
        if (!run.format(frame.path, effects_files[file]) ||
            !run.load(frame.path, frame.slots, file != 4, loaded))
            return result;
        if (!loaded.skip) {
            const auto size = frame.slots.size;
            if (file != 4) {
                if (!run.section(loaded, size, effects_single_sections[file]))
                    return result;
            } else {
                // EBX captures size once for all nine final sections.
                for (auto section : effects_final_sections)
                    if (!run.section(loaded, size, section))
                        return result;
            }
            if (!run.release(loaded))
                return result;
        }
        if (file == 0)
            env.globals.service_29b = 0;
        else if (file == 1)
            env.globals.service_29b = 1;
    }
    run.teardown();
    return result;
}

StartupConfigResult
run_radio_config_98dbb0(startup::StartupServiceBase& service,
                        StartupConfigEnvironment env) {
    StartupConfigResult result;
    auto& frame = env.radio;
    frame.path.fill('\0'); // Before 9896C0.
    Run run{env, result};
    if (!run.construct(service) || !run.format(frame.path, radio_file) ||
        !run.args(frame.path, 0xFF, {0xF0A2C8}, "radio"))
        return result;
    LoadedFile loaded;
    if (!run.load(frame.path, frame.slots, false, loaded))
        return result;
    if (!loaded.skip && (!run.section(loaded, frame.slots.size, "Radio") ||
                         !run.release(loaded)))
        return result;
    run.teardown();
    return result;
}

StartupConfigResult
run_leaderboards_config_98da20(startup::StartupServiceBase& service,
                               StartupConfigEnvironment env) {
    StartupConfigResult result;
    auto& frame = env.leaderboards;
    frame.path.fill('\0'); // Before 9896C0, all 512 bytes.
    Run run{env, result};
    if (!run.construct(service) || !run.format(frame.path, leaderboards_file) ||
        !run.args(frame.path, 0x1FF, {0xF0A294}, "leaderboards"))
        return result;
    LoadedFile loaded;
    if (!run.load(frame.path, frame.slots, false, loaded))
        return result;
    if (!loaded.skip) {
        if (!run.section(loaded, frame.slots.size, "Leaderboards") ||
            !run.section(loaded, frame.slots.size, "Nb_Challenges") ||
            !run.release(loaded))
            return result;
    }
    run.teardown();
    return result;
}

ModeCallOutcome
StartupConfigurationBinding::call_98e090(startup::NativeIdentity arg) {
    if (arg.va != 0x1140360)
        return ModeCallOutcome::modern_refusal;
    last_effects = run_effects_config_98e090(service_, env_);
    return completion_outcome(*last_effects);
}
ModeCallOutcome
StartupConfigurationBinding::call_98dbb0(startup::NativeIdentity arg) {
    if (arg.va != 0x1140360)
        return ModeCallOutcome::modern_refusal;
    last_radio = run_radio_config_98dbb0(service_, env_);
    return completion_outcome(*last_radio);
}
ModeCallOutcome
StartupConfigurationBinding::call_98da20(startup::NativeIdentity arg) {
    if (arg.va != 0x1140360)
        return ModeCallOutcome::modern_refusal;
    last_leaderboards = run_leaderboards_config_98da20(service_, env_);
    return completion_outcome(*last_leaderboards);
}

} // namespace tdur::game::config

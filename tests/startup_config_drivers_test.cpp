#include "startup_file_sources_witness.hpp"
#include "tdur/game/startup_config_drivers.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace tdur::game;
using namespace tdur::game::config;
namespace st = tdur::game::startup;
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

struct Service final : st::StartupServiceBase {
    ModeInitOutcome initialize(GameModeRuntime& owner) override {
        CHECK(st::initialize_9b3f80(*this, owner) ==
              st::InitOutcome::completed);
        return ModeInitOutcome::initialized;
    }
    void on_forward_pass() override {}
    void on_reverse_pass() override {}
    void on_shutdown() override {}
};

// All unspecified callees below are source-owned witnesses. The registry,
// section driver, argument lookup, teardown and three caller bodies are real.
struct Bench final : reg::Constructor9896c0Ports,
                     SectionParserPorts,
                     ArgsTablePorts,
                     ParserTeardownPorts,
                     StartupConfigPorts {
    Service service;
    DriverContext args_globals;
    StartupConfigGlobals globals;
    BlockStore store;
    SectionParserState state;
    EffectsConfigFrame effects;
    RadioConfigFrame radio;
    LeaderboardsConfigFrame leaderboards;
    BlockHandle integrated, external;
    std::vector<std::string> events, sections_seen, formatted, loaded_paths;
    std::vector<std::size_t> parsed_sizes;
    std::vector<std::uint8_t> seen_flags;
    std::vector<BlockHandle> freed;
    std::vector<std::string> argv;
    std::optional<BlockHandle> integrated_value, external_value;
    std::optional<std::uint8_t> selector{std::uint8_t{0}};
    std::optional<std::uint8_t> external_al{std::uint8_t{1}};
    bool integrated_refuses = false, format_refuses = false,
         free_refuses = false;
    bool write_external_block = true, write_sizes = true,
         establish_link80 = true;
    bool unresolved_name = false, args_unreadable = false;
    bool bad_cursor = false;
    std::uint32_t load_size = 1;
    int constructors = 0, registrations = 0, teardowns = 0, copies = 0;
    st::StartupServiceBase* expected_service = &service;
    StartupConfigPorts* file_calls = this;
    std::function<void(std::string_view)> callback;
    std::function<void()> at_construct;
    Bench() {
        integrated = store.add(std::vector<std::byte>(16, std::byte{'['}));
        external = store.add(std::vector<std::byte>(16, std::byte{'['}));
        integrated_value = integrated;
        external_value = external;
        globals.integrated_first_f68bf0 = 1;
        globals.service_29b = 9;
        effects.path.fill('e');
        radio.path.fill('r');
        leaderboards.path.fill('l');
        args_globals.global_10e7350 = 0x123400;
    }
    StartupConfigEnvironment environment() {
        return {args_globals, globals, store, state, effects, radio,
                leaderboards, *this,   *this, *this, *this,   *file_calls};
    }
    void event(const std::string& value) {
        events.push_back(value);
        if (callback)
            callback(value);
    }
    void call_4052b0_subobject_30(reg::ParserObject9896c0&) override {
        ++constructors;
        event("construct");
        if (at_construct)
            at_construct();
    }
    void call_4377b0(reg::ParserObject9896c0& parser) override {
        if (establish_link80)
            parser.link_80 = 0; // Explicit witness for the unknown helper.
    }
    std::optional<std::string_view>
    read_indirect_name_slot(reg::ParserObject9896c0&, st::NativeIdentity,
                            std::size_t) override {
        return unresolved_name ? std::nullopt
                               : std::optional<std::string_view>{"fixture"};
    }
    void call_4378b0(reg::ParserObject9896c0&, const reg::RegistrationRecord&,
                     const reg::ResolvedName&) override {
        ++registrations;
    }
    st::Allocation allocate_1699104_slot20(std::uint32_t size, std::uint32_t a,
                                           std::uint32_t b,
                                           std::uint32_t c) override {
        CHECK(size == 4 && a == 0 && b == 0 && c == 0);
        return {}; // Native null allocation is allowed by this constructor.
    }
    reg::OpaquePair404dc0 call_404dc0(reg::ParserObject9896c0&,
                                      std::size_t) override {
        CHECK(false);
        return {};
    }
    ParseCursor prefix_4381b0(const SectionParseCall& call) override {
        CHECK(call.section.has_value());
        sections_seen.emplace_back(*call.section);
        parsed_sizes.push_back(call.data.size());
        seen_flags.push_back(globals.service_29b);
        event("section:" + sections_seen.back());
        if (bad_cursor) {
            call.state.stop_flag_10e8069 = 7;
            return {call.data.size() + 1};
        }
        return {call.data.size()};
    }
    HelperCarry commands_4383b0(const SectionParseCall&, HelperCarry value,
                                ParserObject&) override {
        CHECK(false);
        return value;
    }
    HelperCarry section_438250(const SectionParseCall&, HelperCarry value,
                               std::uint8_t& found, std::uint8_t&) override {
        found = 0;
        return value;
    }
    std::optional<st::OpaqueWord> read_entry(st::OpaqueWord table,
                                             std::int32_t index) override {
        CHECK(table == args_globals.global_10e7350);
        if (args_unreadable || index < 0 || std::size_t(index) >= argv.size())
            return std::nullopt;
        return (static_cast<st::OpaqueWord>(index) + 1) * 0x1000;
    }
    std::optional<std::span<const char>>
    resolve_text(st::OpaqueWord entry) override {
        const auto index = static_cast<std::size_t>(entry / 0x1000 - 1);
        if (index >= argv.size())
            return std::nullopt;
        return std::span(argv[index].c_str(), argv[index].size() + 1);
    }
    std::uint32_t compare_9ec25c(NativeIdentity key,
                                 std::optional<std::string_view> label,
                                 st::OpaqueWord entry_plus_1) override {
        CHECK(key.value == 0xF0A2C8 || key.value == 0xF0A294);
        CHECK(label.has_value());
        const auto index =
            static_cast<std::size_t>((entry_plus_1 - 1) / 0x1000 - 1);
        return argv.at(index).substr(1) == *label ? 0U : 1U;
    }
    void copy_9ec36c(std::span<char> destination, st::OpaqueWord source,
                     std::uint32_t count) override {
        CHECK(destination.size() == count && (count == 255 || count == 511));
        const auto text = *resolve_text(source);
        std::copy_n(text.begin(), std::min(text.size(), destination.size()),
                    destination.begin());
        ++copies;
        event("override");
    }
    std::optional<std::span<const std::byte, 4>>
    resolve_prefix(st::OpaqueWord) override {
        CHECK(false);
        return std::nullopt;
    }
    void call_401180(st::OpaqueWord, std::uint32_t, std::uint32_t,
                     NativeIdentity) override {
        CHECK(false);
    }
    void free_674e0c(st::OpaqueWord) override { CHECK(false); }
    void call_437860(ParserObject& parser, std::uint32_t ebx) override {
        CHECK(ebx == 0);
        CHECK(parser.registry.link_8c ==
              std::bit_cast<st::OpaqueWord>(expected_service));
        ++teardowns;
        event("teardown");
    }
    bool call_indirect_slot0(st::OpaqueWord, std::uint32_t) override {
        CHECK(false);
        return false;
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        event("selector");
        return selector;
    }
    HelperCallOutcome format_9ec378(std::span<char> destination,
                                    const StartupPathFormat& call) override {
        CHECK(call.prefix.value ==
              (selector.value_or(0) ? 0x11404C4U : 0x1140444U));
        CHECK(call.verified_format.starts_with("%s"));
        std::string text =
            "fixture/" + std::string(call.verified_format.substr(2));
        if (call.second_argument) {
            CHECK(call.verified_second_argument.has_value());
            const auto marker = text.find("%s");
            CHECK(marker != std::string::npos);
            text.replace(marker, 2, *call.verified_second_argument);
        }
        CHECK(text.size() < destination.size());
        std::copy(text.begin(), text.end(), destination.begin());
        destination[text.size()] = '\0';
        formatted.push_back(text);
        event("format");
        return format_refuses ? HelperCallOutcome::modern_refusal
                              : HelperCallOutcome::native_returned;
    }
    IntegratedConfigReturn lookup_9aff20(NativeIdentity receiver,
                                         std::span<const char> path,
                                         std::uint32_t& size) override {
        CHECK(receiver.value == 0xFA4068);
        loaded_paths.emplace_back(path.data());
        if (write_sizes)
            size = load_size;
        event("integrated");
        return {integrated_refuses ? HelperCallOutcome::modern_refusal
                                   : HelperCallOutcome::native_returned,
                integrated_value};
    }
    std::optional<std::uint8_t>
    load_9a5a70(const SynchronousConfigCall& call) override {
        CHECK(call.service.value == 0x10E80C0 && call.eax_at_call == 0);
        CHECK(call.trailing_arguments ==
              (std::array<std::uint32_t, 3>{0, 0, 0}));
        loaded_paths.emplace_back(call.path.data());
        if (write_external_block)
            call.block = external_value;
        if (write_sizes)
            call.size = load_size;
        event("external");
        return external_al;
    }
    HelperCallOutcome free_674e0c(BlockHandle block) override {
        freed.push_back(block);
        event("free");
        return free_refuses ? HelperCallOutcome::modern_refusal
                            : HelperCallOutcome::native_returned;
    }
    StartupConfigResult run(int which) {
        if (which == 0)
            return run_effects_config_98e090(service, environment());
        if (which == 1)
            return run_radio_config_98dbb0(service, environment());
        return run_leaderboards_config_98da20(service, environment());
    }
};

std::array<std::vector<std::string>, 3> expected_sections;
void read_primary_fixture(const char* filename) {
    std::ifstream input(filename);
    CHECK(input.good());
    std::string row;
    while (std::getline(input, row)) {
        std::istringstream values(row);
        unsigned int kind, site, identity;
        std::string label;
        CHECK(static_cast<bool>(values >> kind >> std::hex >> site >>
                                identity >> label));
        CHECK(kind < 3 && site > 0x98DA20 && site < 0x98E699 && identity != 0);
        if (kind < 3)
            expected_sections[kind].push_back(label);
    }
    CHECK(expected_sections[0].size() == 13 &&
          expected_sections[1].size() == 1 && expected_sections[2].size() == 2);
}

void primary_order_and_frames() {
    for (int kind = 0; kind < 3; ++kind) {
        Bench b;
        b.at_construct = [&] {
            CHECK(b.effects.path.front() == 'e');
            if (kind == 1)
                CHECK(std::all_of(b.radio.path.begin(), b.radio.path.end(),
                                  [](char c) { return !c; }));
            if (kind == 2)
                CHECK(std::all_of(b.leaderboards.path.begin(),
                                  b.leaderboards.path.end(),
                                  [](char c) { return !c; }));
        };
        const auto result = b.run(kind);
        CHECK(result.outcome == StartupConfigOutcome::native_return);
        CHECK(b.sections_seen == expected_sections[kind]);
        CHECK(result.section_calls == expected_sections[kind].size());
        CHECK(b.registrations == 375 && b.constructors == 1 &&
              b.teardowns == 1);
        CHECK(b.freed.empty());
        if (kind == 0) {
            CHECK(b.formatted ==
                  (std::vector<std::string>{
                      "fixture/FX/HDRIConf.ini", "fixture/FX/Weather.ini",
                      "fixture/FX/Weather_HDRI.ini",
                      "fixture/FX/WeatherDesc.ini", "fixture/FX/FX.ini"}));
            CHECK(b.seen_flags[0] == 9 && b.seen_flags[1] == 0);
            CHECK(std::all_of(b.seen_flags.begin() + 2, b.seen_flags.end(),
                              [](auto x) { return x == 1; }));
            CHECK(b.globals.service_29b == 1);
        }
    }
}

void priority_and_fallbacks() {
    for (int kind = 0; kind < 3; ++kind) {
        for (bool first : {false, true}) {
            for (bool primary_present : {false, true}) {
                Bench b;
                b.globals.integrated_first_f68bf0 = first ? 255 : 0;
                if (first && !primary_present)
                    b.integrated_value.reset();
                if (!first && !primary_present)
                    b.external_al = std::uint8_t{0};
                else
                    b.external_al = std::uint8_t{128};
                b.callback = [&](std::string_view event) {
                    if (!first && !primary_present &&
                        event.starts_with("section:")) {
                        const auto& slots = kind == 0   ? b.effects.slots
                                            : kind == 1 ? b.radio.slots
                                                        : b.leaderboards.slots;
                        const bool writes_slot =
                            kind == 0 && b.formatted.size() < 5;
                        CHECK(slots.block ==
                              (writes_slot ? b.integrated : b.external));
                    }
                };
                const auto result = b.run(kind);
                CHECK(result.outcome == StartupConfigOutcome::native_return);
                std::vector<std::string> source_order;
                for (const auto& event : b.events)
                    if (event == "integrated" || event == "external")
                        source_order.push_back(event);
                const std::size_t file_count = kind == 0 ? 5U : 1U;
                std::vector<std::string> expected;
                for (std::size_t i = 0; i < file_count; ++i) {
                    expected.push_back(first ? "integrated" : "external");
                    if (!primary_present)
                        expected.push_back(first ? "external" : "integrated");
                }
                CHECK(source_order == expected);
                CHECK(b.freed.size() == ((!first && primary_present) ||
                                                 (first && !primary_present)
                                             ? file_count
                                             : 0));
            }
        }
        Bench absent;
        absent.integrated_value.reset();
        absent.external_al = std::uint8_t{0};
        CHECK(absent.run(kind).outcome == StartupConfigOutcome::native_return);
        CHECK(absent.sections_seen.empty() && absent.teardowns == 1);
        if (kind == 0)
            CHECK(absent.globals.service_29b == 1);
    }
}

void argument_lookup_and_live_flags() {
    for (int kind : {1, 2}) {
        Bench b;
        b.argv = {kind == 1 ? "-radio" : "-leaderboards", "override.ini"};
        b.callback = [&](std::string_view event) {
            if (event == "format") {
                b.args_globals.global_10e734c = 2;
                b.globals.integrated_first_f68bf0 = 0;
            }
        };
        const auto result = b.run(kind);
        CHECK(result.outcome == StartupConfigOutcome::native_return);
        CHECK(result.lookup && result.lookup->copied && result.lookup->al == 1);
        CHECK(b.loaded_paths == std::vector<std::string>{"override.ini"});
        CHECK(b.copies == 1 && b.freed.size() == 1);
    }
    Bench stale;
    stale.globals.integrated_first_f68bf0 = 0;
    stale.write_external_block = false;
    stale.radio.slots.block = stale.integrated;
    CHECK(stale.run(1).outcome == StartupConfigOutcome::native_return);
    CHECK(stale.freed == std::vector<BlockHandle>{stale.integrated});
    for (int kind : {1, 2}) {
        Bench failed_external;
        failed_external.globals.integrated_first_f68bf0 = 0;
        failed_external.external_al = std::uint8_t{0};
        failed_external.integrated_value.reset();
        CHECK(failed_external.run(kind).outcome ==
              StartupConfigOutcome::native_return);
        const auto& slots = kind == 1 ? failed_external.radio.slots
                                      : failed_external.leaderboards.slots;
        // Neither caller stores fallback EAX into the output slot.
        CHECK(slots.block == failed_external.external);
    }
}

void sizes_and_final_fallback_slot() {
    Bench effects;
    effects.callback = [&](std::string_view event) {
        if (event == "section:CAR")
            effects.effects.slots.size = 10;
    };
    CHECK(effects.run(0).outcome == StartupConfigOutcome::native_return);
    CHECK(std::all_of(effects.parsed_sizes.begin() + 4,
                      effects.parsed_sizes.end(),
                      [](auto n) { return n == 1; }));
    Bench leaderboards;
    leaderboards.callback = [&](std::string_view event) {
        if (event == "section:Leaderboards")
            leaderboards.leaderboards.slots.size = 10;
    };
    CHECK(leaderboards.run(2).outcome == StartupConfigOutcome::native_return);
    CHECK(leaderboards.parsed_sizes == (std::vector<std::size_t>{1, 10}));
    Bench fallback;
    fallback.globals.integrated_first_f68bf0 = 0;
    fallback.external_al = std::uint8_t{0};
    CHECK(fallback.run(0).outcome == StartupConfigOutcome::native_return);
    CHECK(fallback.effects.slots.block == fallback.external);
    CHECK(
        fallback.freed
            .empty()); // Last fallback's local EDI is not written to the slot.
}

void refusals_and_empty_blocks() {
    for (int kind = 0; kind < 3; ++kind) {
        Bench null;
        null.globals.integrated_first_f68bf0 = 0;
        null.external_value.reset();
        CHECK(null.run(kind).outcome ==
              StartupConfigOutcome::refused_null_parse_block);
        CHECK(null.sections_seen.empty() && null.teardowns == 0);
        CHECK(null.globals.service_29b == 9);
        Bench empty;
        empty.integrated_value = empty.store.add({});
        empty.load_size = 0;
        CHECK(empty.run(kind).outcome == StartupConfigOutcome::native_return);
        CHECK(empty.sections_seen == expected_sections[kind]);
        Bench bad_size;
        bad_size.load_size = 100;
        CHECK(bad_size.run(kind).outcome ==
              StartupConfigOutcome::refused_section);
        CHECK(bad_size.sections_seen.empty() && bad_size.teardowns == 0);
        Bench cursor;
        cursor.bad_cursor = true;
        CHECK(cursor.run(kind).outcome ==
              StartupConfigOutcome::refused_section);
        CHECK(cursor.state.stop_flag_10e8069 == 7 && cursor.teardowns == 0);
        Bench selector;
        selector.selector.reset();
        CHECK(selector.run(kind).outcome ==
              StartupConfigOutcome::refused_selector);
        CHECK(selector.events.back() == "selector" && selector.teardowns == 0);
        Bench format;
        format.format_refuses = true;
        CHECK(format.run(kind).outcome == StartupConfigOutcome::refused_format);
        CHECK(format.events.back() == "format" && format.teardowns == 0);
        Bench integrated;
        integrated.integrated_refuses = true;
        CHECK(integrated.run(kind).outcome ==
              StartupConfigOutcome::refused_integrated_lookup);
        CHECK(integrated.events.back() == "integrated" &&
              integrated.teardowns == 0);
        Bench external;
        external.integrated_value.reset();
        external.external_al.reset();
        CHECK(external.run(kind).outcome ==
              StartupConfigOutcome::refused_external_load);
        CHECK(external.events.back() == "external" && external.teardowns == 0);
        Bench free;
        free.globals.integrated_first_f68bf0 = 0;
        free.free_refuses = true;
        CHECK(free.run(kind).outcome == StartupConfigOutcome::refused_free);
        CHECK(free.events.back() == "free" && free.teardowns == 0);
        Bench constructor;
        constructor.unresolved_name = true;
        CHECK(constructor.run(kind).outcome ==
              StartupConfigOutcome::refused_constructor);
        CHECK(constructor.teardowns == 0 && constructor.formatted.empty());
        Bench teardown;
        teardown.establish_link80 = false;
        CHECK(teardown.run(kind).outcome ==
              StartupConfigOutcome::refused_teardown);
        CHECK(teardown.teardowns == 1);
    }
    for (int kind : {1, 2}) {
        Bench args;
        args.args_globals.global_10e734c = 1;
        args.args_unreadable = true;
        CHECK(args.run(kind).outcome == StartupConfigOutcome::refused_args);
        CHECK(args.loaded_paths.empty() && args.teardowns == 0);
    }
}

struct Inner final : st::StartupCompletionPorts {
    int delegated = 0, configurations = 0;
    st::StartupPathCopy load_copy_9ec36c() override {
        ++delegated;
        return {};
    }
    ModeCallOutcome call_71e8c0(st::NativeIdentity, st::NativeIdentity,
                                st::NativeIdentity, std::uint32_t) override {
        ++delegated;
        return ModeCallOutcome::modern_refusal;
    }
    st::Allocation allocate_1699104_slot20(std::uint32_t, std::uint32_t,
                                           std::uint32_t,
                                           std::uint32_t) override {
        ++delegated;
        return {};
    }
    st::StartupLoadingScenePtr construct_71eb80(st::Allocation) override {
        ++delegated;
        return {};
    }
    ModeCallOutcome call_97f260() override {
        ++delegated;
        return ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome scene_slot0(st::StartupLoadingScene&,
                                std::uint32_t) override {
        ++delegated;
        return ModeCallOutcome::modern_refusal;
    }
    std::optional<std::uint32_t> call_5066e0(st::NativeIdentity,
                                             std::uint32_t) override {
        ++delegated;
        return 0;
    }
    ModeCallOutcome call_9b0190(st::NativeIdentity) override {
        ++delegated;
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome call_9a4930(st::NativeIdentity) override {
        ++delegated;
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome call_98e090(st::NativeIdentity) override {
        ++configurations;
        return ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome call_98dbb0(st::NativeIdentity) override {
        ++configurations;
        return ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome call_98da20(st::NativeIdentity) override {
        ++configurations;
        return ModeCallOutcome::modern_refusal;
    }
};

struct Lifecycle final : ModeLifecyclePorts,
                         st::HelperContextSlot,
                         st::AllocatorPort {
    st::HelperContext context;
    st::HelperContext* load() override { return &context; }
    st::Allocation allocate_slot20(std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::uint32_t) override {
        CHECK(false);
        return {};
    }
    std::uint32_t startup_gate() override {
        CHECK(false);
        return 0;
    }
    ModeSubsystemPtr boot_subsystem(std::size_t) override {
        CHECK(false);
        return {};
    }
    ModeCallOutcome begin_initialization(ModeSubsystem& member) override {
        CHECK(st::initialization_helper_9b40c0(
                  dynamic_cast<st::StartupServiceBase&>(member), *this,
                  *this) == st::HelperOutcome::inactive);
        return ModeCallOutcome::native_returned;
    }
    ModeCallOutcome end_initialization(ModeSubsystem& member) override {
        CHECK(st::initialization_helper_9b4060(
                  dynamic_cast<st::StartupServiceBase&>(member), *this,
                  *this) == st::HelperOutcome::inactive);
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

void composed_file_sources() {
    for (int priority = 0; priority < 2; ++priority) {
        for (int available = 0; available < 2; ++available) {
            for (int external_al = 0; external_al < 2; ++external_al) {
                for (int kind = 0; kind < 3; ++kind) {
                    Bench b;
                    b.globals.integrated_first_f68bf0 =
                        static_cast<std::uint8_t>(priority);
                    IntegratedSourceState source;
                    source.available_2ac = static_cast<std::uint8_t>(available);
                    IntegratedSourceFrame frame{};
                    source_test::IntegratedPorts integrated;
                    integrated.size = 1;
                    integrated.data.block = b.integrated;
                    source_test::ExternalPorts external;
                    external.local_size = 1;
                    external.allocation = b.external;
                    external.callback_result.al =
                        static_cast<std::uint8_t>(external_al);
                    StartupFileSourcesBinding calls{b, source, frame,
                                                    integrated, external};
                    b.file_calls = &calls;
                    const auto result = b.run(kind);
                    CHECK(result.outcome ==
                          StartupConfigOutcome::native_return);
                    const bool external_selected =
                        external_al && (!priority || !available);
                    const bool any = available || external_al;
                    CHECK(b.sections_seen ==
                          (any ? expected_sections[kind]
                               : std::vector<std::string>{}));
                    CHECK(b.freed.size() ==
                          (external_selected ? (kind == 0 ? 5U : 1U) : 0U));
                    CHECK(b.registrations == 375 && b.teardowns == 1);
                    CHECK(
                        b.loaded_paths
                            .empty()); // Neither old high-level port was used.
                    CHECK(integrated.same_local && external.same_local &&
                          external.releases == 0);
                    CHECK(external.events.empty() ==
                          bool(priority && available));
                    CHECK(integrated.events.empty() ==
                          bool(!available || (!priority && external_al)));
                }
            }
        }
    }
    Bench b;
    IntegratedSourceState source;
    source.available_2ac = 1;
    IntegratedSourceFrame frame{};
    source_test::IntegratedPorts integrated;
    integrated.found.reset();
    source_test::ExternalPorts external;
    StartupFileSourcesBinding calls{b, source, frame, integrated, external};
    b.file_calls = &calls;
    CHECK(b.run(1).outcome == StartupConfigOutcome::refused_integrated_lookup);
    CHECK(external.events.empty() && b.teardowns == 0 &&
          b.sections_seen.empty());
    std::uint32_t size = 77;
    CHECK(calls.lookup_9aff20({0}, std::span("a"), size).control ==
          HelperCallOutcome::modern_refusal);
    CHECK(size == 77 && calls.last_integrated->outcome ==
                            IntegratedSourceOutcome::refused_receiver);
}

void composed_completion() {
    Bench b;
    Inner inner;
    GameModeRuntime owner;
    for (int i = 0; i < 13; ++i)
        owner.subsystem_slots.push_back(std::make_shared<Service>());
    owner.subsystem_count = 13;
    b.expected_service =
        dynamic_cast<st::StartupServiceBase*>(owner.subsystem_slots[4].get());
    IntegratedSourceState source;
    source.available_2ac = 1;
    IntegratedSourceFrame frame{};
    source_test::IntegratedPorts integrated;
    integrated.size = 1;
    integrated.data.block = b.integrated;
    source_test::ExternalPorts external;
    StartupFileSourcesBinding file_sources{b, source, frame, integrated,
                                           external};
    b.file_calls = &file_sources;
    StartupConfigurationBinding calls{inner, *b.expected_service,
                                      b.environment()};
    st::DerivedStartupServices services;
    for (auto& service : services)
        service = std::make_shared<Service>();
    st::StartupCompletionState completion_state{24};
    st::StartupCompletionGlobals completion_globals;
    st::StartupGlobals shared;
    Lifecycle lifecycle;
    CHECK(st::complete_startup_97c8f0(owner, lifecycle,
                                      {completion_state, completion_globals,
                                       shared, services, calls}) ==
          ModeLifecycleStatus::success);
    CHECK(owner.subsystem_count == 51 && b.registrations == 3 * 375);
    CHECK(b.sections_seen.size() == 16 && b.teardowns == 3);
    CHECK(calls.last_effects && calls.last_radio && calls.last_leaderboards);
    CHECK(inner.configurations == 0 && inner.delegated == 3);
    CHECK(b.loaded_paths.empty() && external.events.empty());
    CHECK(std::count(integrated.events.begin(), integrated.events.end(),
                     "destroy") == 7);
    CHECK(integrated.leaf_seen == "Leaderboards.ini");
    CHECK(calls.call_98e090({0}) == ModeCallOutcome::modern_refusal);
    Bench refusal;
    refusal.format_refuses = true;
    StartupConfigurationBinding rejected{inner, refusal.service,
                                         refusal.environment()};
    CHECK(rejected.call_98dbb0({0x1140360}) == ModeCallOutcome::modern_refusal);
    CHECK(rejected.last_radio->outcome == StartupConfigOutcome::refused_format);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    read_primary_fixture(argv[1]);
    primary_order_and_frames();
    priority_and_fallbacks();
    argument_lookup_and_live_flags();
    sizes_and_final_fallback_slot();
    refusals_and_empty_blocks();
    composed_file_sources();
    composed_completion();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

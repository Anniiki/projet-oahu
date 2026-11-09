#include "tdur/game/startup_world_loaders.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <limits>
#include <string>
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

struct Ports : WorldLoaderPorts {
    std::vector<std::string> events;
    std::function<void(std::string_view)> hook;
    std::size_t fail_at = 0, step_count = 0, formatter_reads = 0;
    int formatter_generation = 1;
    bool missing_formatter = false, unterminated = false;
    std::size_t pending_count = 0;
    AreaLoadCall area_call{};
    SectorLoadCall sector_call{};
    std::string area_path, sector_path, sector_name;
    AreaLoadPayload* area_payload = nullptr;
    SectorLoadPayload* sector_payload = nullptr;
    GameModeRuntime* finished_owner = nullptr;
    bool step(const std::string& event) {
        events.push_back(event);
        ++step_count;
        if (hook)
            hook(event);
        return step_count != fail_at;
    }
    WorldPathFormatter load_formatter_9ec378() override {
        ++formatter_reads;
        if (missing_formatter)
            return {};
        return [this, generation = formatter_generation](
                   std::span<char> output, WorldPathKind kind,
                   std::span<const std::int32_t> args) {
            const auto result = format_world_path(output, kind, args);
            if (unterminated)
                std::fill(output.begin(), output.end(), 'x');
            return step("format" + std::to_string(generation)) ? result : no;
        };
    }
    ModeCallOutcome reset_area_709200(AreaLoadPayload& payload) override {
        area_payload = &payload;
        payload.port_state = 21;
        return step("reset_area") ? ok : no;
    }
    ModeCallOutcome load_area_709590(AreaLoadPayload& payload,
                                     const AreaLoadCall& call) override {
        CHECK(&payload == area_payload);
        area_call = call;
        area_path = call.path;
        area_call.path = area_path;
        payload.port_state = 22;
        return step("load_area") ? ok : no;
    }
    ModeCallOutcome reset_sector_705cb0(SectorLoadPayload& payload) override {
        sector_payload = &payload;
        payload.port_state = 31;
        return step("reset_sector") ? ok : no;
    }
    ModeCallOutcome load_sector_706030(SectorLoadPayload& payload,
                                       const SectorLoadCall& call) override {
        CHECK(&payload == sector_payload);
        sector_call = call;
        sector_path = call.path;
        sector_name = call.name;
        sector_call.path = sector_path;
        sector_call.name = sector_name;
        payload.port_state = 32;
        return step("load_sector") ? ok : no;
    }
    std::optional<std::uint8_t>
    pending_9a2a20(NativeIdentity receiver) override {
        CHECK(receiver.va == 0x10E80C0);
        if (!step("query"))
            return std::nullopt;
        return static_cast<std::uint8_t>(pending_count != 0 ? 0x80 : 0);
    }
    ModeCallOutcome pump_9a2bd0(NativeIdentity receiver) override {
        CHECK(receiver.va == 0x10E80C0);
        if (pending_count != 0)
            --pending_count;
        return step("pump") ? ok : no;
    }
    ModeCallOutcome finish_609a20(NativeIdentity receiver,
                                  std::uint32_t argument) override {
        CHECK(receiver.va == 0x100AF5C && argument == 0);
        return step("finish_a") ? ok : no;
    }
    ModeCallOutcome finish_9b4ba0(GameModeRuntime& receiver,
                                  std::uint32_t argument) override {
        CHECK(argument == 0);
        finished_owner = &receiver;
        return step("finish_b") ? ok : no;
    }
    ModeCallOutcome free_674e0c(Allocation) override {
        return step("free") ? ok : no;
    }
};

struct Fixture {
    ForwardCycleObject object, replacement;
    ForwardCycleGlobals globals{&object};
    GameModeRuntime owner, second_owner;
    GameModeRuntime* live_owner = &owner;
    GridPair scale{8, 8};
    ServicePreparations state;
    Ports ports;
    Fixture() {
        state.area_position_00 = {1, 1};
        state.sector_position_08 = {3, 3};
        state.areas_76fd0.dimensions = {3, 4};
        state.areas_76fd0.scale_376c = &scale;
        state.areas_76fd0.data_3770 = {&object, 0x7A750};
        state.sectors_9c0.dimensions = {8, 8};
        state.sectors_9c0.areas_76608 = &state.areas_76fd0;
        state.sectors_9c0.data_76604 = {&object, 0x7A750};
        object.sector_properties_84090.resize(11 * 3 * 64);
        for (std::size_t i = 0; i < object.sector_properties_84090.size(); ++i)
            object.sector_properties_84090[i] =
                0x10000 + static_cast<std::uint32_t>(i);
        replacement.sector_properties_84090 = object.sector_properties_84090;
        for (auto& value : replacement.sector_properties_84090)
            value += 0x20000;
    }
    WorldLoadOutcome area() {
        return load_area_708c80(state.areas_76fd0,
                                state.areas_76fd0.center_2690,
                                state.area_position_00, 17, ports);
    }
    WorldLoadOutcome sector() {
        return load_sector_705190(
            state.sectors_9c0, state.sectors_9c0.center_65770,
            state.area_position_00, state.sector_position_08, globals, ports);
    }
};

void formatting_and_lookup() {
    std::array<char, 128> buffer;
    buffer.fill('z');
    const std::array<std::int32_t, 5> area{2, 2, 3, 2, 3};
    CHECK(format_world_path(buffer, WorldPathKind::area, area) == ok);
    CHECK(std::string_view(buffer.data()) == "Area-2\\Area-2-3\\Area-2-3");
    CHECK(buffer[std::string_view(buffer.data()).size() + 1] == 'z');
    CHECK(format_world_path(std::span(buffer).first(4), WorldPathKind::area,
                            area) == no);
    CHECK(format_world_path(buffer, WorldPathKind::sector_name, area) == no);
    const std::array extremes{std::numeric_limits<std::int32_t>::min(),
                              std::numeric_limits<std::int32_t>::max(), -1, 0};
    CHECK(format_world_path(buffer, WorldPathKind::sector_name, extremes) ==
          ok);
    CHECK(std::string_view(buffer.data()) ==
          "Sector--2147483648-2147483647--1-0");
    AreaPreparation parent;
    for (std::size_t i = 0; i < 3; ++i)
        parent.neighbors_20[i].position_cc0 = {static_cast<std::int32_t>(i), 9};
    parent.center_2690.position_cc0 = {3, 9};
    for (std::size_t i = 0; i < 3; ++i)
        CHECK(find_area_706e30(parent, {static_cast<std::int32_t>(i), 9}) ==
              &parent.neighbors_20[i]);
    CHECK(find_area_706e30(parent, {3, 9}) == &parent.center_2690);
    CHECK(find_area_706e30(parent, {3, 8}) == nullptr);
    parent.neighbors_20[2].position_cc0 = {3, 9};
    CHECK(find_area_706e30(parent, {3, 9}) == &parent.neighbors_20[2]);
}

void ordinary_loads() {
    Fixture f;
    CHECK(f.area() == WorldLoadOutcome::native_return);
    CHECK(f.ports.area_path == "Area-1\\Area-1-1\\Area-1-1");
    CHECK(f.ports.area_call.flags == 17 &&
          f.ports.area_call.scale_bits == 0x41C00000);
    CHECK((f.ports.area_call.data == CycleMemberRef{&f.object, 0x7A750}));
    CHECK((f.ports.events ==
           std::vector<std::string>{"reset_area", "format1", "load_area"}));
    // Independent row-major enumeration. Global rows have eleven areas;
    // each area has eight sector rows of eight entries.
    std::uint32_t property_index = 0;
    for (int ay = 0; ay < 3; ++ay) {
        for (int ax = 0; ax < 11; ++ax) {
            for (int sy = 0; sy < 8; ++sy) {
                for (int sx = 0; sx < 8; ++sx, ++property_index) {
                    if (ax >= 4)
                        continue;
                    f.state.area_position_00 = {ax, ay};
                    f.state.sector_position_08 = {sx, sy};
                    CHECK(f.sector() == WorldLoadOutcome::native_return);
                    const auto& call = f.ports.sector_call;
                    CHECK(call.area_x_bits ==
                          static_cast<std::uint32_t>(ax * 6144));
                    CHECK(call.area_y_bits ==
                          static_cast<std::uint32_t>(ay * 6144));
                    CHECK(call.sector_x_bits ==
                          static_cast<std::uint32_t>(ax * 6144 + sx * 768 +
                                                     (sy % 2) * 384));
                    CHECK(call.sector_y_bits ==
                          static_cast<std::uint32_t>(ay * 6144 + sy * 768));
                    CHECK(call.scale_bits == 0x40400000);
                    CHECK((call.data == CycleMemberRef{&f.object, 0x7A750}));
                    const auto& slot = f.state.sectors_9c0.center_65770;
                    CHECK(slot.property_10e6c() == 0x10000 + property_index);
                    CHECK((slot.area_10e80 == GridPair{ax, ay}));
                    CHECK((slot.sector_10e88 == GridPair{sx, sy}));
                }
            }
        }
    }
    CHECK(f.ports.sector_path == "Area-3\\Area-3-2\\Sector-3-2-7-7");
    CHECK(f.ports.sector_name == "Sector-3-2-7-7");
}

void callback_visibility() {
    Fixture f;
    GridPair different_scale{3, 5};
    f.ports.hook = [&](std::string_view event) {
        if (event == "reset_area")
            f.state.area_position_00 = {2, 1};
        if (event == "format1") {
            f.state.areas_76fd0.scale_376c = &different_scale;
            f.state.area_position_00 = {3, 2};
        }
        if (event == "load_area")
            f.state.area_position_00 = {1, 0};
    };
    CHECK(f.area() == WorldLoadOutcome::native_return);
    CHECK(f.ports.area_path == "Area-2\\Area-2-1\\Area-2-1");
    CHECK((f.ports.area_call.position == GridPair{3, 2}));
    CHECK(f.ports.area_call.x_bits == 11520 &&
          f.ports.area_call.y_bits == 4608);
    CHECK((f.state.areas_76fd0.center_2690.position_cc0 == GridPair{1, 0}));

    Fixture s;
    int formats = 0;
    s.ports.hook = [&](std::string_view event) {
        if (event == "format1") {
            ++formats;
            if (formats == 1) {
                s.ports.formatter_generation = 2;
                s.state.area_position_00 = {2, 1};
            } else {
                s.state.areas_76fd0.scale_376c = &different_scale;
                s.state.sector_position_08 = {4, 2};
            }
        }
        if (event == "load_sector") {
            s.globals.object_fa48b0 = &s.replacement;
            s.state.area_position_00 = {0, 0};
            s.state.sector_position_08 = {1, 2};
        }
    };
    CHECK(s.sector() == WorldLoadOutcome::native_return);
    CHECK(formats == 2 && s.ports.formatter_reads == 1);
    CHECK(s.ports.sector_path == "Area-1\\Area-1-1\\Sector-1-1-3-3");
    CHECK(s.ports.sector_name == "Sector-2-1-3-3");
    CHECK(s.ports.sector_call.sector_x_bits == 10752);
    CHECK(s.ports.sector_call.sector_y_bits == 3840);
    CHECK(s.state.sectors_9c0.center_65770.property_10e6c() == 0x30011);
    CHECK((s.state.sectors_9c0.center_65770.sector_10e88 == GridPair{1, 2}));

    Fixture overflow;
    overflow.scale = {-1, std::numeric_limits<std::int32_t>::max()};
    CHECK(overflow.area() == WorldLoadOutcome::native_return);
    CHECK(overflow.ports.area_call.x_bits == 0xFFFFFD00);
    CHECK(overflow.ports.area_call.y_bits == 0xFFFFFD00);
    // A formatter can change inputs after validation. Native arithmetic
    // wraps and the driver does not insert a second coordinate check.
    overflow.ports.hook = [&](std::string_view event) {
        if (event == "format1")
            overflow.state.sector_position_08.second = -1;
    };
    CHECK(overflow.sector() == WorldLoadOutcome::native_return);
    CHECK(overflow.ports.sector_call.sector_x_bits == 1920);
    CHECK(overflow.ports.sector_call.sector_y_bits == 0xFFFFFA00);
}

void refusals_and_invalids() {
    for (GridPair bad :
         {GridPair{-1, 1}, GridPair{4, 1}, GridPair{1, -1}, GridPair{1, 3}}) {
        Fixture f;
        f.state.area_position_00 = bad;
        f.state.areas_76fd0.scale_376c = nullptr;
        CHECK(f.area() == WorldLoadOutcome::native_return);
        CHECK(f.ports.events == std::vector<std::string>{"reset_area"});
        CHECK(f.state.areas_76fd0.center_2690.payload_10.port_state == 21);
        CHECK(
            (f.state.areas_76fd0.center_2690.position_cc0 == GridPair{-1, -1}));
        CHECK(f.sector() == WorldLoadOutcome::native_return);
        CHECK(f.state.sectors_9c0.center_65770.status_2b4() == 3);
        CHECK(
            (f.state.sectors_9c0.center_65770.area_10e80 == GridPair{-1, -1}));
    }
    for (GridPair bad :
         {GridPair{-1, 0}, GridPair{8, 0}, GridPair{0, -1}, GridPair{0, 8}}) {
        Fixture f;
        f.state.sector_position_08 = bad;
        CHECK(f.sector() == WorldLoadOutcome::native_return);
        CHECK(f.ports.events == std::vector<std::string>{"reset_sector"});
        CHECK((f.state.sectors_9c0.center_65770.sector_10e88 ==
               GridPair{-1, -1}));
        CHECK(f.state.sectors_9c0.center_65770.status_2b4() == 3);
    }
    for (std::size_t i = 1; i <= 3; ++i) {
        Fixture f;
        f.ports.fail_at = i;
        f.state.areas_76fd0.center_2690.position_cc0 = {99, 98};
        CHECK(f.area() == (i == 1   ? WorldLoadOutcome::refused_reset
                           : i == 2 ? WorldLoadOutcome::refused_formatter
                                    : WorldLoadOutcome::refused_load));
        CHECK(f.ports.events.size() == i);
        CHECK(
            (f.state.areas_76fd0.center_2690.position_cc0 == GridPair{99, 98}));
    }
    for (std::size_t i = 1; i <= 4; ++i) {
        Fixture f;
        f.ports.fail_at = i;
        f.state.sectors_9c0.center_65770.property_10e6c() = 99;
        CHECK(f.sector() == (i == 1   ? WorldLoadOutcome::refused_reset
                             : i == 4 ? WorldLoadOutcome::refused_load
                                      : WorldLoadOutcome::refused_formatter));
        CHECK(f.ports.events.size() == i);
        CHECK(f.state.sectors_9c0.center_65770.property_10e6c() == 99);
    }
    Fixture link;
    link.state.sectors_9c0.areas_76608 = nullptr;
    CHECK(link.sector() == WorldLoadOutcome::refused_area_link);
    link.state.area_position_00.first = -1;
    CHECK(link.sector() == WorldLoadOutcome::native_return);
    Fixture scale;
    scale.state.areas_76fd0.scale_376c = nullptr;
    CHECK(scale.area() == WorldLoadOutcome::refused_scale_link);
    CHECK(scale.sector() == WorldLoadOutcome::refused_scale_link);
    Fixture path;
    path.ports.unterminated = true;
    CHECK(path.area() == WorldLoadOutcome::refused_unterminated_path);
    CHECK(path.sector() == WorldLoadOutcome::refused_unterminated_path);
    path.ports.missing_formatter = true;
    CHECK(path.area() == WorldLoadOutcome::refused_formatter);
    CHECK(path.sector() == WorldLoadOutcome::refused_formatter);
    Fixture global;
    global.ports.hook = [&](std::string_view event) {
        if (event == "load_sector")
            global.globals.object_fa48b0 = nullptr;
    };
    CHECK(global.sector() == WorldLoadOutcome::refused_global);
    CHECK(global.state.sectors_9c0.center_65770.payload_10.port_state == 32);
    CHECK((global.state.sectors_9c0.center_65770.area_10e80 == GridPair{0, 0}));
    Fixture index;
    index.ports.hook = [&](std::string_view event) {
        if (event == "load_sector")
            index.state.sector_position_08.first = -1;
    };
    // This remains a valid DWORD index because the remaining terms are
    // positive. Use a completely negative result to exercise the refusal.
    index.state.area_position_00 = {0, 0};
    index.state.sector_position_08 = {0, 0};
    CHECK(index.sector() == WorldLoadOutcome::refused_property_index);
    CHECK(index.state.sectors_9c0.center_65770.property_10e6c() == 0);
}

void draining() {
    Fixture f;
    f.ports.pending_count = 2;
    f.ports.hook = [&](std::string_view event) {
        if (event == "finish_a")
            f.live_owner = &f.second_owner;
    };
    CHECK(drain_world_9a4930({0x10E80C0}, f.live_owner, 2, f.ports) ==
          WorldDrainOutcome::native_return);
    CHECK((f.ports.events == std::vector<std::string>{"query", "pump", "query",
                                                      "pump", "query",
                                                      "finish_a", "finish_b"}));
    CHECK(f.ports.finished_owner == &f.second_owner);
    constexpr WorldDrainOutcome expected[]{
        WorldDrainOutcome::refused_query,
        WorldDrainOutcome::refused_pump,
        WorldDrainOutcome::refused_query,
        WorldDrainOutcome::refused_pump,
        WorldDrainOutcome::refused_query,
        WorldDrainOutcome::refused_finish_609a20,
        WorldDrainOutcome::refused_finish_9b4ba0};
    for (std::size_t i = 1; i <= 7; ++i) {
        Fixture failed;
        failed.ports.pending_count = 2;
        failed.ports.fail_at = i;
        CHECK(drain_world_9a4930({0x10E80C0}, failed.live_owner, 2,
                                 failed.ports) == expected[i - 1]);
        CHECK(failed.ports.events.size() == i);
    }
    Fixture budget;
    budget.ports.pending_count = 2;
    CHECK(drain_world_9a4930({0x10E80C0}, budget.live_owner, 1, budget.ports) ==
          WorldDrainOutcome::refused_budget);
    CHECK((budget.ports.events ==
           std::vector<std::string>{"query", "pump", "query"}));
    Fixture empty;
    CHECK(drain_world_9a4930({0x10E80C0}, empty.live_owner, 0, empty.ports) ==
          WorldDrainOutcome::native_return);
    Fixture absent;
    absent.live_owner = nullptr;
    CHECK(drain_world_9a4930({0x10E80C0}, absent.live_owner, 0, absent.ports) ==
          WorldDrainOutcome::refused_mode_owner);
    CHECK(
        (absent.ports.events == std::vector<std::string>{"query", "finish_a"}));
}

void composition() {
    Fixture f;
    LoadedServicePreparationPorts loaded(f.globals, f.live_owner, 10, f.ports);
    PreparedServiceCyclePorts prepared(f.object, f.state, loaded);
    Service9b3f80 service({f.globals, prepared});
    f.ports.hook = [&](std::string_view event) {
        if (event == "load_area")
            f.globals.object_fa48b0 = &f.replacement;
    };
    CHECK(service.on_forward_pass() == ok);
    CHECK(f.state.areas_76fd0.initialized_13 == 1);
    CHECK(f.state.sectors_9c0.initialized_0b == 1);
    CHECK(f.object.ready_8a3b1 == 1 && f.replacement.ready_8a3b1 == 0);
    CHECK(f.state.sectors_9c0.center_65770.property_10e6c() >= 0x30000);
    CHECK((f.ports.sector_call.data == CycleMemberRef{&f.object, 0x7A750}));
    CHECK(std::count(f.ports.events.begin(), f.ports.events.end(),
                     "load_area") == 4);
    CHECK(std::count(f.ports.events.begin(), f.ports.events.end(),
                     "load_sector") == 7);
    CHECK(loaded.last_drain == WorldDrainOutcome::native_return);
    std::array<std::byte, 1> allocation;
    service.allocation_04 = {allocation.data(), allocation.size()};
    CHECK(service.on_shutdown() == ok);
    CHECK(service.allocation_04.is_null());
    Fixture failed;
    LoadedServicePreparationPorts bad(failed.globals, failed.live_owner, 0,
                                      failed.ports);
    PreparedServiceCyclePorts prep(failed.object, failed.state, bad);
    failed.ports.pending_count = 1;
    CHECK(forward_service_9b4120(failed.globals, prep) ==
          ServiceCycleOutcome::refused_prepare_7053e0);
    CHECK(failed.state.areas_76fd0.initialized_13 == 1);
    CHECK(failed.state.sectors_9c0.initialized_0b == 0);
    CHECK(failed.object.ready_8a3b1 == 0);
    CHECK(bad.last_drain == WorldDrainOutcome::refused_budget);
}
} // namespace

int main() {
    formatting_and_lookup();
    ordinary_loads();
    callback_visibility();
    refusals_and_invalids();
    draining();
    composition();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

#include "tdur/game/startup_service_preparations.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <limits>
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

void area_neighbors() {
    constexpr GridPair expected[3][8]{
        {{2, 1}, {0, 1}, {1, 2}, {1, 0}, {2, 0}, {0, 0}, {2, 2}, {0, 2}},
        {{1, 0},
         {-1, -1},
         {0, 1},
         {-1, -1},
         {-1, -1},
         {-1, -1},
         {1, 1},
         {-1, -1}},
        {{-1, -1},
         {2, 2},
         {-1, -1},
         {3, 1},
         {-1, -1},
         {2, 1},
         {-1, -1},
         {-1, -1}}};
    constexpr GridPair positions[]{{1, 1}, {0, 0}, {3, 2}};
    for (std::size_t p = 0; p < 3; ++p) {
        for (std::uint32_t direction = 0; direction < 8; ++direction) {
            GridPair output{91, 92};
            CHECK(
                area_neighbor_706c00(positions[p], {3, 4}, direction, output));
            CHECK(output == expected[p][direction]);
        }
    }
    for (auto direction : {8U, 255U, 0xFFFFFFFFU}) {
        GridPair output{91, 92};
        CHECK(!area_neighbor_706c00({0, 0}, {3, 4}, direction, output));
        CHECK((output == GridPair{91, 92}));
    }
    GridPair output;
    const auto min = std::numeric_limits<std::int32_t>::min();
    const auto max = std::numeric_limits<std::int32_t>::max();
    CHECK(area_neighbor_706c00({max, 42}, {8, 8}, 0, output));
    CHECK((output == GridPair{min, 42})); // INC, then signed CMP.
    CHECK(area_neighbor_706c00({min, 42}, {8, 8}, 1, output));
    CHECK((output ==
           GridPair{max, 42})); // DEC, then JS; unchanged axis unchecked.
    CHECK(area_neighbor_706c00({-9, 7}, {8, 8}, 3, output));
    CHECK((output == GridPair{-9, 6}));
}

void sector_neighbors() {
    AreaPreparation areas;
    areas.dimensions = {3, 4};
    SectorPreparation sectors;
    sectors.dimensions = {8, 8};
    sectors.areas_76608 = &areas;
    // Independent full-grid oracle: translate a hex-grid neighbor in global
    // coordinates, then split it into area/sector coordinates by division.
    for (int ay = 0; ay < 3; ++ay)
        for (int ax = 0; ax < 4; ++ax)
            for (int sy = 0; sy < 8; ++sy)
                for (int sx = 0; sx < 8; ++sx)
                    for (std::uint32_t direction = 0; direction < 8;
                         ++direction) {
                        GridPair out_a{91, 92}, out_s{93, 94};
                        const auto result =
                            sector_neighbor_704520(sectors, {ax, ay}, {sx, sy},
                                                   direction, out_a, out_s);
                        if (direction == 2 || direction == 3) {
                            CHECK(result == SectorNeighborOutcome::unavailable);
                            CHECK((out_a == GridPair{91, 92}));
                            CHECK((out_s == GridPair{93, 94}));
                            continue;
                        }
                        const int dx[] = {1,      -1,        0,
                                          0,      sy % 2,    sy % 2 - 1,
                                          sy % 2, sy % 2 - 1};
                        const int dy[] = {0, 0, 0, 0, -1, -1, 1, 1};
                        const int gx = ax * 8 + sx + dx[direction];
                        const int gy = ay * 8 + sy + dy[direction];
                        const bool exists =
                            gx >= 0 && gx < 32 && gy >= 0 && gy < 24;
                        CHECK(result ==
                              (exists ? SectorNeighborOutcome::selected
                                      : SectorNeighborOutcome::unavailable));
                        CHECK((out_a == (exists ? GridPair{gx / 8, gy / 8}
                                                : GridPair{-1, -1})));
                        CHECK((out_s == (exists ? GridPair{gx % 8, gy % 8}
                                                : GridPair{-1, -1})));
                    }
    GridPair out_a{91, 92}, out_s{93, 94};
    sectors.areas_76608 = nullptr;
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {3, 3}, 0, out_a, out_s) ==
          SectorNeighborOutcome::selected); // No boundary, no link read.
    CHECK((out_a == GridPair{1, 1} && out_s == GridPair{4, 3}));
    out_a = {91, 92};
    out_s = {93, 94};
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {7, 3}, 0, out_a, out_s) ==
          SectorNeighborOutcome::missing_area_link);
    CHECK((out_a == GridPair{91, 92} && out_s == GridPair{93, 94}));
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {3, 7}, 6, out_a, out_s) ==
          SectorNeighborOutcome::missing_area_link);
    CHECK((out_a == GridPair{1, 92} && out_s == GridPair{4, 94}));
    sectors.areas_76608 = &areas;
    // Native diagonal uses equality with width, not a general bounds check.
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {8, 3}, 4, out_a, out_s) ==
          SectorNeighborOutcome::selected);
    CHECK((out_a == GridPair{1, 1} && out_s == GridPair{9, 2}));
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {8, 3}, 0, out_a, out_s) ==
          SectorNeighborOutcome::selected);
    CHECK((out_a == GridPair{2, 1} && out_s == GridPair{0, 3}));
    const auto min = std::numeric_limits<std::int32_t>::min();
    const auto max = std::numeric_limits<std::int32_t>::max();
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {max, 3}, 0, out_a, out_s) ==
          SectorNeighborOutcome::selected);
    CHECK((out_s == GridPair{min, 3}));
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {min, 3}, 1, out_a, out_s) ==
          SectorNeighborOutcome::selected);
    CHECK((out_s == GridPair{max, 3}));
    out_a = {91, 92};
    out_s = {93, 94};
    CHECK(sector_neighbor_704520(sectors, {1, 1}, {0, 0}, 0xFFFFFFFFU, out_a,
                                 out_s) == SectorNeighborOutcome::unavailable);
    CHECK((out_a == GridPair{91, 92} && out_s == GridPair{93, 94}));
}

struct Event {
    char kind;
    int slot; // -1 = center, 0.. = bounded neighbor slot.
    GridPair area, sector;
};
struct Ports final : ServicePreparationPorts {
    std::vector<Event> events;
    std::vector<Allocation> freed;
    std::function<void(Event)> effect;
    std::size_t refuse_at = 0;
    ModeCallOutcome record(Event event) {
        events.push_back(event);
        if (effect)
            effect(event);
        return refuse_at == events.size() ? ModeCallOutcome::modern_refusal
                                          : ModeCallOutcome::native_returned;
    }
    ModeCallOutcome load_area_708c80(AreaPreparation& areas,
                                     AreaPreparationSlot& slot,
                                     GridPair& position,
                                     std::uint32_t flags) override {
        CHECK(flags == 0);
        int index = -1;
        if (&slot != &areas.center_2690) {
            for (int i = 0; i < 3; ++i)
                if (&slot == &areas.neighbors_20[static_cast<std::size_t>(i)])
                    index = i;
            CHECK(index >= 0);
        }
        slot.port_state += 10;
        return record({'a', index, position, {}});
    }
    ModeCallOutcome load_sector_705190(SectorPreparation& sectors,
                                       SectorPreparationSlot& slot,
                                       GridPair& area,
                                       GridPair& sector) override {
        int index = -1;
        if (&slot != &sectors.center_65770) {
            for (int i = 0; i < 6; ++i)
                if (&slot == &sectors.neighbors_10[static_cast<std::size_t>(i)])
                    index = i;
            CHECK(index >= 0);
        }
        slot.port_state += 10;
        slot.status_2b4() = 99;
        return record({'s', index, area, sector});
    }
    ModeCallOutcome drain_9a4930(NativeIdentity esi) override {
        CHECK(esi.va == 0x10E80C0);
        return record({'d', -1, {}, {}});
    }
    ModeCallOutcome free_674e0c(Allocation allocation) override {
        freed.push_back(allocation);
        return ModeCallOutcome::native_returned;
    }
};
void initialize(ServicePreparations& state) {
    state.area_position_00 = {1, 1};
    state.sector_position_08 = {3, 3};
    state.areas_76fd0.dimensions = {3, 4};
    state.sectors_9c0.dimensions = {8, 8};
    state.sectors_9c0.areas_76608 = &state.areas_76fd0;
    state.sectors_9c0.byte_09 = 77;
}

void area_driver() {
    constexpr GridPair inputs[]{{3, 3}, {4, 3}, {3, 4}, {4, 4}, {-1, -1}};
    constexpr std::uint32_t rows[]{5, 4, 7, 6, 5};
    constexpr GridPair expected[][3]{{{1, 0}, {0, 1}, {0, 0}},
                                     {{1, 0}, {2, 1}, {2, 0}},
                                     {{1, 2}, {0, 1}, {0, 2}},
                                     {{1, 2}, {2, 1}, {2, 2}},
                                     {{1, 0}, {0, 1}, {0, 0}}};
    ForwardCycleObject object;
    const CycleMemberRef data{&object, 0x7A750};
    for (std::size_t i = 0; i < std::size(inputs); ++i) {
        ServicePreparations state;
        initialize(state);
        state.sector_position_08 = inputs[i];
        Ports ports;
        CHECK(prepare_areas_708d50(state, data, primary_area_directions(),
                                   ports) == PreparationOutcome::native_return);
        CHECK(state.areas_76fd0.direction_row_3768 == rows[i]);
        CHECK(state.areas_76fd0.current_3764 == &state.areas_76fd0.center_2690);
        CHECK(state.areas_76fd0.data_3770 == data);
        CHECK(state.areas_76fd0.initialized_13 == 1);
        CHECK(ports.events.size() == 4);
        for (std::size_t n = 0; n < 3; ++n) {
            CHECK(ports.events[n + 1].slot == static_cast<int>(n));
            CHECK(ports.events[n + 1].area == expected[i][n]);
        }
    }
    ServicePreparations state;
    initialize(state);
    Ports ports;
    std::vector<std::uint32_t> table(primary_area_directions().begin(),
                                     primary_area_directions().end());
    ports.effect = [&](Event e) {
        if (e.slot == -1) {
            state.areas_76fd0.direction_row_3768 = 4;
            state.area_position_00 = {2, 1};
        } else if (e.slot == 0) {
            state.areas_76fd0.direction_row_3768 = 7;
            table[71] = 0; // Live row and table, next coordinate = (3,1).
        }
    };
    CHECK(prepare_areas_708d50(state, data, table, ports) ==
          PreparationOutcome::native_return);
    CHECK((ports.events[1].area == GridPair{2, 0}));
    CHECK((ports.events[2].area == GridPair{3, 1}));
    CHECK((ports.events[3].area == GridPair{1, 2}));
    // The native caller ignores AL=0. A previous stack value survives;
    // the first unknown direction has no initialized value to consume.
    for (int unknown_at : {0, 1}) {
        ServicePreparations local;
        initialize(local);
        Ports seen;
        table.assign(primary_area_directions().begin(),
                     primary_area_directions().end());
        table[static_cast<std::size_t>(50 + unknown_at)] = 8;
        const auto result = prepare_areas_708d50(local, data, table, seen);
        CHECK(result ==
              (unknown_at == 0
                   ? PreparationOutcome::refused_uninitialized_neighbor
                   : PreparationOutcome::native_return));
        if (unknown_at == 0) {
            CHECK(seen.events.size() == 1);
            CHECK(local.areas_76fd0.initialized_13 == 0);
        } else {
            CHECK(seen.events[1].area == seen.events[2].area);
        }
    }
    ServicePreparations short_table;
    initialize(short_table);
    Ports short_seen;
    CHECK(prepare_areas_708d50(short_table, data, {}, short_seen) ==
          PreparationOutcome::refused_direction_table);
    CHECK(short_seen.events.size() == 1); // Prior native writes/call retained.
    CHECK(short_table.areas_76fd0.current_3764 ==
          &short_table.areas_76fd0.center_2690);
}

void sector_driver() {
    ServicePreparations state;
    initialize(state);
    state.area_position_00 = {0, 0};
    state.sector_position_08 = {0, 0};
    for (auto& slot : state.sectors_9c0.neighbors_10) {
        slot.port_state = 42;
        slot.status_2b4() = 17;
    }
    Ports ports;
    CHECK(prepare_sectors_7053e0(state, {}, ports) ==
          PreparationOutcome::native_return);
    CHECK(ports.events.size() == 4); // Center, drain, east, south-east.
    CHECK(ports.events[0].kind == 's' && ports.events[0].slot == -1);
    CHECK(ports.events[1].kind == 'd');
    CHECK(ports.events[2].slot == 0 && ports.events[3].slot == 4);
    const std::uint32_t statuses[]{99, 3, 3, 3, 99, 3};
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(state.sectors_9c0.neighbors_10[i].status_2b4() == statuses[i]);
        CHECK(state.sectors_9c0.neighbors_10[i].port_state ==
              (statuses[i] == 3 ? 42U : 52U));
    }
    CHECK(state.sectors_9c0.current_76600 == &state.sectors_9c0.center_65770);
    CHECK(state.sectors_9c0.initialized_0b == 1);
    CHECK(state.sectors_9c0.active_08 == 1 && state.sectors_9c0.byte_09 == 0);
    ServicePreparations live;
    initialize(live);
    Ports mutation;
    mutation.effect = [&](Event event) {
        if (event.kind == 'd')
            live.sector_position_08 = {0, 0};
        if (event.kind == 's' && event.slot == 0)
            live.area_position_00 = {0, 0};
    };
    CHECK(prepare_sectors_7053e0(live, {}, mutation) ==
          PreparationOutcome::native_return);
    CHECK((mutation.events[2].area == GridPair{1, 1}));
    CHECK((mutation.events[2].sector == GridPair{1, 0}));
    CHECK(live.sectors_9c0.neighbors_10[1].status_2b4() ==
          3); // Live source changed.
}

void binding_and_refusals() {
    for (std::size_t fail_at = 0; fail_at <= 12; ++fail_at) {
        ServicePreparations state;
        initialize(state);
        ForwardCycleObject object, replacement;
        object.member_20.bytes.fill(std::byte{0x2A});
        object.member_20.bytes[2] = std::byte{0};
        ForwardCycleGlobals globals{&object};
        Ports ports;
        ports.refuse_at = fail_at;
        ports.effect = [&](Event) { globals.object_fa48b0 = &replacement; };
        PreparedServiceCyclePorts binding(object, state, ports);
        auto service = std::make_shared<Service9b3f80>(
            ServiceCycleContext{globals, binding});
        GameModeRuntime runtime;
        runtime.subsystem_slots = {service};
        runtime.subsystem_count = 1;
        CHECK(forward_mode_subsystems(runtime) ==
              (fail_at == 0 ? ModeLifecycleStatus::success
                            : ModeLifecycleStatus::modern_refusal));
        CHECK(ports.events.size() == (fail_at == 0 ? 12 : fail_at));
        CHECK(object.ready_8a3b1 == (fail_at == 0 ? 1 : 0));
        CHECK(object.member_20.bytes[0] ==
              (fail_at == 0 ? std::byte{0} : std::byte{0x2A}));
        CHECK(replacement.ready_8a3b1 == 0);
        CHECK(state.areas_76fd0.initialized_13 ==
              (fail_at > 0 && fail_at <= 4 ? 0 : 1));
        CHECK(state.sectors_9c0.initialized_0b == (fail_at == 0 ? 1 : 0));
        if (fail_at != 0) {
            CHECK(state.sectors_9c0.byte_09 == 77);
            CHECK(service->last_forward ==
                  (fail_at <= 4 ? ServiceCycleOutcome::refused_prepare_708d50
                                : ServiceCycleOutcome::refused_prepare_7053e0));
        }
        if (fail_at == 0) {
            globals.object_fa48b0 = &object;
            CHECK(reset_mode_subsystems(runtime) ==
                  ModeLifecycleStatus::success);
            CHECK(ports.events.size() ==
                  12); // Guards suppress both repeated drivers.
            std::array<std::byte, 3> storage{};
            service->allocation_04 = {storage.data(), storage.size()};
            CHECK(shutdown_mode_subsystems(runtime) ==
                  ModeLifecycleStatus::success);
            CHECK(ports.freed.size() == 1 &&
                  ports.freed[0].data == storage.data());
        }
    }
    for (auto guard : {1U, 0x80U, 0xFFU}) {
        ServicePreparations state;
        initialize(state);
        state.areas_76fd0.initialized_13 = static_cast<std::uint8_t>(guard);
        state.sectors_9c0.initialized_0b = static_cast<std::uint8_t>(guard);
        Ports ports;
        CHECK(prepare_areas_708d50(state, {}, {}, ports) ==
              PreparationOutcome::native_return);
        CHECK(prepare_sectors_7053e0(state, {}, ports) ==
              PreparationOutcome::native_return);
        CHECK(ports.events.empty());
        CHECK(state.sectors_9c0.byte_09 == 77 &&
              state.sectors_9c0.active_08 == 0);
        CHECK(state.areas_76fd0.current_3764 == nullptr);
        CHECK(state.sectors_9c0.current_76600 == nullptr);
    }
    ServicePreparations state;
    initialize(state);
    ForwardCycleObject object, other;
    Ports ports;
    PreparedServiceCyclePorts binding(object, state, ports);
    const Prepare708d50Call correct_a{
        {&object, 8}, {&object, 0x76FD0}, {{{&object, 0}, {&object, 0x7A750}}}};
    for (int i = 0; i < 4; ++i) {
        auto bad = correct_a;
        CycleMemberRef* values[]{&bad.eax, &bad.esi, &bad.arguments[0],
                                 &bad.arguments[1]};
        values[i]->object = &other;
        CHECK(binding.prepare_708d50(bad) == ModeCallOutcome::modern_refusal);
        CHECK(binding.last_area == PreparationOutcome::refused_member_identity);
        CHECK(ports.events.empty());
    }
    const Prepare7053e0Call correct_s{
        {&object, 0x9C0}, {{{&object, 0}, {&object, 8}, {&object, 0x7A750}}}};
    for (int i = 0; i < 4; ++i) {
        auto bad = correct_s;
        CycleMemberRef* values[]{&bad.edi, &bad.arguments[0], &bad.arguments[1],
                                 &bad.arguments[2]};
        ++values[i]->offset;
        CHECK(binding.prepare_7053e0(bad) == ModeCallOutcome::modern_refusal);
        CHECK(binding.last_sector ==
              PreparationOutcome::refused_member_identity);
        CHECK(ports.events.empty());
    }
    state.sectors_9c0.areas_76608 = nullptr;
    state.sector_position_08 = {7, 3};
    CHECK(prepare_sectors_7053e0(state, {}, ports) ==
          PreparationOutcome::refused_missing_area_link);
    CHECK(ports.events.size() == 2); // Center and drain precede first edge.
    CHECK(state.sectors_9c0.initialized_0b == 0);
}
} // namespace

int main() {
    area_neighbors();
    sector_neighbors();
    area_driver();
    sector_driver();
    binding_and_refusals();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

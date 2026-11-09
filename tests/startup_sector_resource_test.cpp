#include "tdur/game/startup_sector_resource.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

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

struct Ports : SectorResourcePorts {
    SectorLoadPayload* active = nullptr;
    std::vector<std::string> events;
    std::function<void(std::string_view)> hook;
    std::size_t fail_at = 0;
    std::optional<std::uint8_t> selector = std::uint8_t{0},
                                al = std::uint8_t{1};
    bool unterminated = false;
    CycleMemberRef cycle_seen;
    Allocation freed;
    std::uint32_t tracked_esi = 0;
    SectorRegistryObject* tag_seen = nullptr;
    SpatialLoadCall spatial{};
    BankPathFormat format;
    BankLoadCall bank;
    std::string path;
    ModeCallOutcome step(std::string_view event) {
        events.emplace_back(event);
        if (hook)
            hook(event);
        return events.size() == fail_at ? no : ok;
    }
    ModeCallOutcome detach_6fa640(OpaqueWord owner,
                                  SectorLoadPayload& r) override {
        CHECK(owner == 19 && &r == active);
        return step("context");
    }
    ModeCallOutcome detach_8a5520(SectorLoadPayload& r,
                                  SectorRegistryObject& o) override {
        CHECK(&r == active && o.tag_08 == 0x726F7061);
        tag_seen = &o;
        return step("tag_a");
    }
    ModeCallOutcome detach_8a4f60(SectorLoadPayload& r,
                                  SectorRegistryObject& o) override {
        CHECK(&r == active && o.tag_08 == 0x70617274);
        tag_seen = &o;
        return step("tag_b");
    }
    ModeCallOutcome detach_7d7a60(SectorLoadPayload& r,
                                  std::uint32_t esi) override {
        CHECK(&r == active);
        tracked_esi = esi;
        return step("tracked");
    }
    ModeCallOutcome detach_901ab0(NativeIdentity owner,
                                  SectorLoadPayload& r) override {
        CHECK(owner.va == 0xFA4CF8 && &r == active);
        CHECK(r.property_10e5c == 0);
        return step("detach");
    }
    ModeCallOutcome reset_6fc800(CycleMemberRef object,
                                 SectorLoadPayload& r) override {
        CHECK(&r == active);
        cycle_seen = object;
        return step("cycle");
    }
    ModeCallOutcome reset_6ff980(LoadResourcePart& part) override {
        CHECK(&part == &active->member_7d0);
        return step("member");
    }
    ModeCallOutcome reset_701fe0(LoadResourcePart& part) override {
        CHECK(&part == &active->spatial_2c0);
        return step("spatial_reset");
    }
    ModeCallOutcome free_674e0c(Allocation value) override {
        freed = value;
        return step("free");
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        return selector;
    }
    ModeCallOutcome format_bank_9ec378(std::span<char> output,
                                       const BankPathFormat& value) override {
        format = value;
        path = std::string("root/Level\\Oahu\\") +
               std::string(value.resource_path) + ".bnk";
        CHECK(output.size() == 512 && path.size() < output.size());
        if (!unterminated) {
            std::copy(path.begin(), path.end(), output.begin());
            output[path.size()] = '\0';
        }
        return step("format");
    }
    std::optional<std::uint8_t> load_9a4d40(SectorLoadPayload& r,
                                            const BankLoadCall& call) override {
        CHECK(&r == active && call.path == path);
        CHECK(call.service.va == 0x10E80C0 && call.callback.va == 0x9A6D50);
        CHECK(call.capacity == 0x480000 && call.eax == 0);
        CHECK(call.source_file.va == 0xF3B154 && call.source_line == 0x36 &&
              call.final_argument == 1);
        CHECK(r.allocator_38.va == 0xFA9CB0 && r.byte_3c == 0);
        bank = call;
        bank.path = path;
        return step("bank") == ok ? al : std::nullopt;
    }
    ModeCallOutcome load_701340(LoadResourcePart& part,
                                const SpatialLoadCall& call) override {
        CHECK(&part == &active->spatial_2c0);
        spatial = call;
        return step("spatial_load");
    }
};
struct Node : SectorResourceNode {
    std::function<ModeCallOutcome()> callback;
    ModeCallOutcome release_slot8() override { return callback(); }
};
std::shared_ptr<SectorRegistryObject> tagged(std::uint32_t tag) {
    auto item = std::make_shared<SectorRegistryObject>();
    item->tag_08 = tag;
    return item;
}
struct Fixture {
    ForwardCycleObject object, replacement;
    ForwardCycleGlobals forward{&object};
    SectorResourceGlobals globals;
    SectorPreparationSlot slot;
    AreaPreparationSlot area;
    std::array<std::byte, 4> storage, alternative;
    Ports ports;
    SectorLoadCall call{"Area-1\\Area-1-2\\Sector-1-2-3-4",
                        "Sector-1-2-3-4",
                        6144,
                        12288,
                        8448,
                        15360,
                        0x40400000,
                        {&object, 0x7A750}};
    Fixture() {
        ports.active = &slot.payload_10;
        globals.context_10e7fa0 = 19;
        globals.registry_13d3e10 = {tagged(0x726F7061), tagged(0x70617274)};
        globals.count_13d3e38 = 2;
        auto& r = slot.payload_10;
        r.ready_2c = 1;
        r.buffer_04 = {storage.data(), 4};
        r.allocator_38 = {7};
        r.area_2ac = &area;
        r.byte_2b0 = 8;
        r.status_2a4 = 77;
        r.property_10e5c = 42;
        r.size_30 = 11;
        r.byte_3c = 17;
        auto& entry = globals.tracked_120be0c[6];
        entry.identity = 42;
        entry.words = {1, 2, 3};
        entry.active_10 = 1;
        entry.preserved_11.fill(std::byte{0xCD});
    }
    SectorResetContext context(std::size_t budget = 32) {
        return {globals, forward, ports, budget};
    }
    SectorResourceOutcome reset(std::size_t budget = 32) {
        return reset_sector_705cb0(slot.payload_10, context(budget));
    }
    SectorResourceOutcome load() {
        return load_sector_706030(slot.payload_10, call, ports);
    }
};

void alias_views() {
    SectorPreparationSlot slot;
    slot.status_2b4() = 13;
    CHECK(slot.payload_10.status_2a4 == 13);
    slot.payload_10.status_2a4 = 17;
    CHECK(slot.status_2b4() == 17);
    slot.property_10e6c() = 19;
    CHECK(slot.payload_10.property_10e5c == 19);
    slot.payload_10.property_10e5c = 23;
    CHECK(slot.property_10e6c() == 23);
    CHECK(&slot.status_2b4() == &slot.payload_10.status_2a4);
    CHECK(&slot.property_10e6c() == &slot.payload_10.property_10e5c);
    auto copy = slot;
    copy.status_2b4() = 29;
    copy.property_10e6c() = 31;
    CHECK(slot.status_2b4() == 17 && slot.property_10e6c() == 23);
    auto moved = std::move(copy);
    moved.status_2b4() = 37;
    CHECK(moved.payload_10.status_2a4 == 37 && copy.status_2b4() == 29);
    const auto& const_slot = moved;
    CHECK(&const_slot.property_10e6c() == &moved.payload_10.property_10e5c);
}

void reset_order() {
    Fixture f;
    CHECK(f.reset() == SectorResourceOutcome::native_return);
    CHECK((f.ports.events ==
           std::vector<std::string>{"context", "tag_a", "tag_b", "tracked",
                                    "detach", "cycle", "member",
                                    "spatial_reset", "free"}));
    CHECK(f.ports.tracked_esi == 30);
    CHECK(f.slot.status_2b4() == 0 && f.slot.property_10e6c() == 0);
    CHECK(f.slot.payload_10.area_2ac == nullptr &&
          f.slot.payload_10.byte_2b0 == 0);
    CHECK(f.slot.payload_10.buffer_04.is_null() &&
          f.slot.payload_10.ready_2c == 0);
    CHECK(f.slot.payload_10.size_30 == 11 && f.slot.payload_10.byte_3c == 17);
    const auto& entry = f.globals.tracked_120be0c[6];
    CHECK(entry.identity == 0 && entry.active_10 == 0);
    CHECK((entry.words == std::array<std::uint32_t, 3>{0, 0, 0}));
    CHECK((entry.preserved_11 == std::array<std::byte, 3>{std::byte{0xCD},
                                                          std::byte{0xCD},
                                                          std::byte{0xCD}}));
    CHECK((f.ports.cycle_seen == CycleMemberRef{&f.object, 0x20}));
    for (std::size_t selected = 0; selected < 7; ++selected) {
        Fixture m;
        m.globals.tracked_120be0c[selected].identity = 42;
        m.ports.hook = [&](std::string_view event) {
            if (event == "tracked") {
                m.globals.tracked_120be0c[selected].identity = 88;
                m.globals.tracked_120be0c[selected].words = {4, 5, 6};
            }
            if (event == "detach")
                m.forward.object_fa48b0 = &m.replacement;
            if (event == "spatial_reset")
                m.slot.payload_10.buffer_04 = {m.alternative.data(), 0};
            if (event == "free")
                m.slot.payload_10.buffer_04 = {m.storage.data(), 4};
        };
        CHECK(m.reset() == SectorResourceOutcome::native_return);
        CHECK(m.ports.tracked_esi == selected * 5);
        CHECK(m.globals.tracked_120be0c[selected].identity == 0);
        if (selected != 6)
            CHECK(m.globals.tracked_120be0c[6].identity == 42);
        CHECK((m.ports.cycle_seen == CycleMemberRef{&m.replacement, 0x20}));
        CHECK(m.ports.freed.data == m.alternative.data() &&
              m.ports.freed.capacity == 0);
        CHECK(m.slot.payload_10.buffer_04.is_null());
    }
}

void list_and_registry() {
    Fixture f;
    auto first = std::make_shared<Node>(), second = std::make_shared<Node>(),
         skipped = std::make_shared<Node>();
    first->next_0c = second;
    f.slot.payload_10.list_10e58 = first;
    first->callback = [&] {
        first->next_0c = skipped;
        f.slot.payload_10.list_10e58.reset();
        return f.ports.step("node1");
    };
    second->callback = [&] { return f.ports.step("node2"); };
    skipped->callback = [&] {
        CHECK(false);
        return no;
    };
    f.globals.count_13d3e38 = 1;
    f.ports.hook = [&](std::string_view event) {
        if (event == "tag_a") {
            f.globals.registry_13d3e10 = {tagged(0), tagged(0),
                                          tagged(0x70617274)};
            f.globals.count_13d3e38 = 3;
        }
    };
    CHECK(f.reset() == SectorResourceOutcome::native_return);
    CHECK(f.ports.events[1] == "node1" && f.ports.events[2] == "node2");
    CHECK(f.ports.tag_seen == f.globals.registry_13d3e10[2].get());
    Fixture destroyed;
    std::weak_ptr<Node> weak_first, weak_second;
    {
        auto one = std::make_shared<Node>(), two = std::make_shared<Node>();
        weak_first = one;
        weak_second = two;
        one->next_0c = two;
        destroyed.slot.payload_10.list_10e58 = one;
        const auto raw = one.get();
        one->callback = [&, raw] {
            raw->next_0c.reset();
            destroyed.slot.payload_10.list_10e58.reset();
            CHECK(!weak_first.expired() && !weak_second.expired());
            return destroyed.ports.step("owned_node1");
        };
        two->callback = [&] {
            CHECK(weak_first.expired());
            return destroyed.ports.step("owned_node2");
        };
    }
    CHECK(destroyed.reset() == SectorResourceOutcome::native_return);
    CHECK(weak_first.expired() && weak_second.expired());
    Fixture cyclic;
    auto node = std::make_shared<Node>();
    node->next_0c = node;
    cyclic.slot.payload_10.list_10e58 = node;
    node->callback = [&] { return cyclic.ports.step("node"); };
    CHECK(cyclic.reset(1) == SectorResourceOutcome::refused_node_budget);
    CHECK((cyclic.ports.events == std::vector<std::string>{"context", "node"}));
    CHECK(cyclic.slot.status_2b4() == 77);
    node->next_0c.reset();
    Fixture failure;
    auto bad = std::make_shared<Node>();
    bad->callback = [] { return no; };
    failure.slot.payload_10.list_10e58 = bad;
    CHECK(failure.reset() == SectorResourceOutcome::refused_node_release);
    Fixture short_table;
    short_table.globals.count_13d3e38 = 3;
    short_table.globals.registry_13d3e10 = {tagged(0)};
    CHECK(short_table.reset() == SectorResourceOutcome::refused_registry);
    Fixture null_entry;
    null_entry.globals.registry_13d3e10[0].reset();
    CHECK(null_entry.reset() == SectorResourceOutcome::refused_registry);
    Fixture negative;
    negative.globals.count_13d3e38 = std::numeric_limits<std::int32_t>::min();
    negative.globals.registry_13d3e10.clear();
    CHECK(negative.reset() == SectorResourceOutcome::native_return);
    CHECK(std::count(negative.ports.events.begin(), negative.ports.events.end(),
                     "tag_a") == 0);
}

void reset_refusals_and_guards() {
    constexpr SectorResourceOutcome expected[]{
        SectorResourceOutcome::refused_context_detach,
        SectorResourceOutcome::refused_tag_detach,
        SectorResourceOutcome::refused_tag_detach,
        SectorResourceOutcome::refused_tracked_detach,
        SectorResourceOutcome::refused_resource_detach,
        SectorResourceOutcome::refused_reset_6fc800,
        SectorResourceOutcome::refused_reset_6ff980,
        SectorResourceOutcome::refused_reset_701fe0,
        SectorResourceOutcome::refused_free};
    for (std::size_t i = 1; i <= 9; ++i) {
        Fixture f;
        f.ports.fail_at = i;
        CHECK(f.reset() == expected[i - 1]);
        CHECK(f.ports.events.size() == i);
        CHECK(f.slot.status_2b4() == 77 && f.slot.payload_10.ready_2c == 1);
        CHECK(f.slot.payload_10.buffer_04.data == f.storage.data());
        CHECK(f.slot.property_10e6c() == (i >= 5 ? 0U : 42U));
    }
    for (bool borrowed : {false, true})
        for (bool buffer : {false, true})
            for (bool allocator : {false, true}) {
                Fixture f;
                f.slot.payload_10.borrowed_34 = borrowed ? 0x80 : 0;
                if (!buffer)
                    f.slot.payload_10.buffer_04 = {};
                if (!allocator)
                    f.slot.payload_10.allocator_38 = {};
                f.slot.payload_10.ready_2c = 0;
                f.slot.payload_10.area_2ac = nullptr;
                f.globals.context_10e7fa0 = 0;
                CHECK(f.reset() == SectorResourceOutcome::native_return);
                CHECK(std::count(f.ports.events.begin(), f.ports.events.end(),
                                 "free") ==
                      ((!borrowed && buffer && allocator) ? 1 : 0));
                CHECK(f.slot.property_10e6c() == 42 &&
                      f.slot.payload_10.byte_2b0 == 8);
                CHECK(f.globals.tracked_120be0c[6].identity == 42);
            }
    Fixture absent;
    absent.ports.hook = [&](std::string_view event) {
        if (event == "detach")
            absent.forward.object_fa48b0 = nullptr;
    };
    CHECK(absent.reset() == SectorResourceOutcome::refused_global);
    CHECK(absent.slot.property_10e6c() == 0 && absent.slot.status_2b4() == 77);
}

void loading() {
    for (std::uint8_t ready : {std::uint8_t{0}, std::uint8_t{0x80}})
        for (std::uint8_t al :
             {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}})
            for (bool supplied : {false, true}) {
                Fixture f;
                f.slot.payload_10.ready_2c = ready;
                f.ports.al = al;
                if (supplied)
                    f.slot.payload_10.supplied_2b4 = {f.alternative.data(), 0};
                CHECK(f.load() == SectorResourceOutcome::native_return);
                const bool accepted = ready != 0 || al != 0;
                CHECK(f.slot.status_2b4() == (accepted ? 2U : 3U));
                CHECK((f.slot.payload_10.link_2330 ==
                       (accepted ? CycleMemberRef{&f.object, 0x7F6D0}
                                 : CycleMemberRef{})));
                CHECK(f.slot.payload_10.data_2a8 == f.call.data);
                CHECK(f.slot.payload_10.x_29c == 8448 &&
                      f.slot.payload_10.y_2a0 == 15360);
                CHECK(std::string_view(f.slot.payload_10.name_10e00.data()) ==
                      f.call.name);
                CHECK(f.slot.property_10e6c() == 42 &&
                      f.slot.payload_10.ready_2c == ready);
                CHECK(f.ports.format.prefix.va == 0x1140444 &&
                      f.ports.format.level.va == 0x11405C4);
                if (ready == 0)
                    CHECK(f.slot.payload_10.buffer_04.data ==
                          (supplied ? f.alternative.data() : nullptr));
                if (accepted)
                    CHECK((f.ports.spatial.arguments ==
                           std::array<std::uint32_t, 6>{6144, 12288, 8448,
                                                        15360, 0x40400000, 0}));
            }
    for (std::size_t i = 1; i <= 3; ++i) {
        Fixture f;
        f.slot.payload_10.ready_2c = 0;
        f.ports.fail_at = i;
        CHECK(f.load() == (i == 1 ? SectorResourceOutcome::refused_format
                           : i == 2
                               ? SectorResourceOutcome::refused_bank_load
                               : SectorResourceOutcome::refused_spatial_load));
        CHECK(f.ports.events.size() == i && f.slot.status_2b4() == 77);
        CHECK(f.slot.payload_10.data_2a8 == CycleMemberRef{});
        if (i == 3)
            CHECK((f.slot.payload_10.link_2330 ==
                   CycleMemberRef{&f.object, 0x7F6D0}));
    }
    Fixture names;
    names.ports.selector = std::nullopt;
    CHECK(names.load() == SectorResourceOutcome::refused_selector);
    CHECK(std::string_view(names.slot.payload_10.name_10e00.data()) ==
          names.call.name);
    names.ports.selector = std::uint8_t{1};
    names.ports.unterminated = true;
    CHECK(names.load() == SectorResourceOutcome::refused_unterminated_path);
    std::string oversized(88, 'x');
    names.call.name = oversized;
    names.ports.events.clear();
    CHECK(names.load() == SectorResourceOutcome::refused_name_capacity);
    CHECK(std::all_of(names.slot.payload_10.name_10e00.begin(),
                      names.slot.payload_10.name_10e00.end(),
                      [](char c) { return c == 'x'; }));
    CHECK(names.ports.events.empty());
    Fixture mutation;
    mutation.ports.selector = std::uint8_t{0x80};
    mutation.ports.hook = [&](std::string_view event) {
        if (event == "format") {
            mutation.call = {};
            mutation.slot.payload_10.ready_2c = 0;
            mutation.slot.payload_10.supplied_2b4 = {
                mutation.alternative.data(), 0};
        }
    };
    CHECK(mutation.load() == SectorResourceOutcome::native_return);
    CHECK(mutation.slot.payload_10.x_29c == 8448 &&
          mutation.ports.format.prefix.va == 0x11404C4);
    CHECK(mutation.slot.payload_10.borrowed_34 == 1);
}

struct Inner : WorldLoaderPorts {
    WorldPathFormatter load_formatter_9ec378() override {
        return format_world_path;
    }
    ModeCallOutcome reset_area_709200(AreaLoadPayload&) override {
        CHECK(false);
        return no;
    }
    ModeCallOutcome load_area_709590(AreaLoadPayload&,
                                     const AreaLoadCall&) override {
        CHECK(false);
        return no;
    }
    ModeCallOutcome reset_sector_705cb0(SectorLoadPayload&) override {
        CHECK(false);
        return no;
    }
    ModeCallOutcome load_sector_706030(SectorLoadPayload&,
                                       const SectorLoadCall&) override {
        CHECK(false);
        return no;
    }
    std::optional<std::uint8_t> pending_9a2a20(NativeIdentity) override {
        return {};
    }
    ModeCallOutcome pump_9a2bd0(NativeIdentity) override { return no; }
    ModeCallOutcome finish_609a20(NativeIdentity, std::uint32_t) override {
        return no;
    }
    ModeCallOutcome finish_9b4ba0(GameModeRuntime&, std::uint32_t) override {
        return no;
    }
    ModeCallOutcome free_674e0c(Allocation) override { return no; }
};
void composition() {
    Fixture f;
    Inner inner;
    SectorResourceBinding binding(inner, f.context());
    AreaPreparation areas;
    SectorPreparation sectors;
    GridPair scale{8, 8}, area{1, 2}, sector{3, 4};
    areas.dimensions = {11, 11};
    areas.scale_376c = &scale;
    sectors.dimensions = {8, 8};
    sectors.areas_76608 = &areas;
    sectors.data_76604 = {&f.object, 0x7A750};
    constexpr auto property_index = 3 + (4 + (2 * 11 + 1) * 8) * 8;
    f.object.sector_properties_84090.resize(property_index + 1);
    f.object.sector_properties_84090[property_index] = 1234;
    f.ports.al = std::uint8_t{0};
    CHECK(load_sector_705190(sectors, f.slot, area, sector, f.forward,
                             binding) == WorldLoadOutcome::native_return);
    CHECK(binding.last_reset == SectorResourceOutcome::native_return);
    CHECK(binding.last_load == SectorResourceOutcome::native_return);
    CHECK(f.slot.status_2b4() == 3 && f.slot.payload_10.status_2a4 == 3);
    CHECK(f.slot.property_10e6c() == 1234 &&
          f.slot.payload_10.property_10e5c == 1234);
    CHECK(f.slot.area_10e80 == area && f.slot.sector_10e88 == sector);
    CHECK(f.slot.payload_10.x_29c == 8448 && f.slot.payload_10.y_2a0 == 15360);
    CHECK(f.slot.payload_10.data_2a8 == sectors.data_76604);
    CHECK(f.slot.payload_10.link_2330 == CycleMemberRef{});
    CHECK(std::count(f.ports.events.begin(), f.ports.events.end(),
                     "spatial_load") == 0);
    CHECK(std::string_view(f.slot.payload_10.name_10e00.data()) ==
          "Sector-1-2-3-4");
    // A modern refusal stops the caller before its property/coordinate stores.
    f.ports.al = std::nullopt;
    f.slot.area_10e80 = {99, 99};
    f.slot.sector_10e88 = {98, 98};
    f.slot.payload_10.property_10e5c = 7654;
    CHECK(load_sector_705190(sectors, f.slot, area, sector, f.forward,
                             binding) == WorldLoadOutcome::refused_load);
    CHECK(binding.last_load == SectorResourceOutcome::refused_bank_load);
    CHECK((f.slot.area_10e80 == GridPair{99, 99} &&
           f.slot.sector_10e88 == GridPair{98, 98}));
    CHECK(f.slot.property_10e6c() == 7654 && f.slot.status_2b4() == 0);
}
} // namespace
int main() {
    alias_views();
    reset_order();
    list_and_registry();
    reset_refusals_and_guards();
    loading();
    composition();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

#include "tdur/game/startup_area_resource.hpp"

#include <algorithm>
#include <cstdio>
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

struct Ports : AreaResourcePorts {
    AreaLoadPayload* active = nullptr;
    std::vector<std::string> events;
    std::function<void(std::string_view)> hook;
    std::size_t fail_at = 0;
    std::optional<std::uint8_t> selector = std::uint8_t{0},
                                load_al = std::uint8_t{1};
    bool unterminated = false;
    BankPathFormat last_format;
    BankLoadCall last_bank;
    SpatialLoadCall last_spatial;
    NativeIdentity free_allocator;
    Allocation freed;
    std::string saved_path;
    bool step(std::string_view name) {
        events.emplace_back(name);
        if (hook)
            hook(name);
        return events.size() != fail_at;
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        return selector;
    }
    ModeCallOutcome format_bank_9ec378(std::span<char> output,
                                       const BankPathFormat& call) override {
        last_format = call;
        saved_path = (call.prefix.va == 0x11404C4 ? "alternate/" : "base/") +
                     std::string("Level\\Oahu\\") +
                     std::string(call.resource_path) + ".bnk";
        if (!unterminated) {
            CHECK(saved_path.size() + 1 <= output.size());
            std::copy(saved_path.begin(), saved_path.end(), output.begin());
            output[saved_path.size()] = '\0';
        }
        return step("format") ? ok : no;
    }
    ModeCallOutcome reset_706210(LoadResourcePart& part) override {
        CHECK(active && &part == &active->member_7c0);
        part.port_state = 71;
        return step("reset_first") ? ok : no;
    }
    ModeCallOutcome reset_701fe0(LoadResourcePart& part) override {
        CHECK(active && &part == &active->spatial_2b0);
        part.port_state = 72;
        return step("reset_second") ? ok : no;
    }
    ModeCallOutcome free_674e0c(NativeIdentity allocator,
                                Allocation value) override {
        free_allocator = allocator;
        freed = value;
        return step("free") ? ok : no;
    }
    std::optional<std::uint8_t> load_9a4d40(AreaLoadPayload& resource,
                                            const BankLoadCall& call) override {
        CHECK(active == &resource);
        CHECK(call.path == saved_path);
        CHECK(resource.allocator_38.va == 0xFA9CB0 && resource.byte_3c == 0);
        CHECK(call.service.va == 0x10E80C0 && call.callback.va == 0x9A6D50);
        CHECK(call.eax == 0 && call.capacity == 0x180000);
        CHECK(call.source_file.va == 0xF3B0F4);
        last_bank = call;
        last_bank.path = saved_path;
        return step("bank") ? load_al : std::nullopt;
    }
    ModeCallOutcome load_701340(LoadResourcePart& part,
                                const SpatialLoadCall& call) override {
        CHECK(active && &part == &active->spatial_2b0);
        last_spatial = call;
        return step("spatial") ? ok : no;
    }
};
struct Fixture {
    std::array<std::byte, 4> storage, alternate;
    ForwardCycleObject object;
    AreaLoadPayload resource;
    AreaLoadCall call{
        "Area-2\\Area-2-1\\Area-2-1", {2, 1}, 3072, 6144, 0x41C00000,
        {&object, 0x7A750},           0};
    Ports ports;
    Fixture() {
        ports.active = &resource;
        resource.buffer_04 = {storage.data(), 4};
        resource.ready_2c = 7;
        resource.size_30 = 29;
        resource.allocator_38 = {0x123456};
        resource.byte_3c = 23;
        resource.status_ca4 = 9;
    }
};

void reset_cases() {
    for (bool borrowed : {false, true})
        for (bool present : {false, true})
            for (bool allocator : {false, true}) {
                Fixture f;
                f.resource.borrowed_34 = borrowed ? 0x80 : 0;
                if (!present)
                    f.resource.buffer_04 = {};
                if (!allocator)
                    f.resource.allocator_38 = {};
                f.resource.supplied_ca0 = {f.alternate.data(), 0};
                CHECK(reset_area_709200(f.resource, f.ports) ==
                      AreaResourceOutcome::native_return);
                CHECK(f.ports.events.size() ==
                      ((!borrowed && present && allocator) ? 3U : 2U));
                CHECK(f.resource.buffer_04.is_null());
                CHECK(f.resource.ready_2c == 0 && f.resource.status_ca4 == 0);
                CHECK(f.resource.size_30 == 29 && f.resource.byte_3c == 23);
                CHECK(f.resource.borrowed_34 == (borrowed ? 0x80 : 0));
                CHECK(f.resource.supplied_ca0.data == f.alternate.data());
                if (!borrowed && present && allocator) {
                    CHECK(f.ports.freed.data == f.storage.data());
                    CHECK(f.ports.free_allocator.va == 0x123456);
                }
            }
    for (std::size_t failure = 1; failure <= 3; ++failure) {
        Fixture f;
        f.ports.fail_at = failure;
        CHECK(reset_area_709200(f.resource, f.ports) ==
              (failure == 1   ? AreaResourceOutcome::refused_reset_706210
               : failure == 2 ? AreaResourceOutcome::refused_reset_701fe0
                              : AreaResourceOutcome::refused_free));
        CHECK(f.ports.events.size() == failure);
        CHECK(f.resource.buffer_04.data == f.storage.data());
        CHECK(f.resource.ready_2c == 7 && f.resource.status_ca4 == 9);
    }
    Fixture f;
    f.resource.borrowed_34 = 1;
    f.ports.hook = [&](std::string_view event) {
        if (event == "reset_second") {
            f.resource.borrowed_34 = 0;
            f.resource.buffer_04 = {f.alternate.data(), 0};
            f.resource.allocator_38 = {0x777};
        }
        if (event == "free")
            f.resource.buffer_04 = {f.storage.data(), 4};
    };
    CHECK(reset_area_709200(f.resource, f.ports) ==
          AreaResourceOutcome::native_return);
    CHECK(f.ports.freed.data == f.alternate.data() &&
          f.ports.freed.capacity == 0);
    CHECK(f.ports.free_allocator.va == 0x777);
    CHECK(f.resource.buffer_04.is_null());
}

void loading_cases() {
    for (std::uint8_t ready :
         {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}})
        for (std::uint8_t al :
             {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0x80}})
            for (std::uint32_t flags : {0U, 1U, 0x80U, 0x100U, 0xFFFFFFFFU})
                for (bool supplied : {false, true}) {
                    Fixture f;
                    f.resource.ready_2c = ready;
                    f.ports.load_al = al;
                    f.call.flags = flags;
                    if (supplied)
                        f.resource.supplied_ca0 = {f.alternate.data(), 0};
                    CHECK(load_area_709590(f.resource, f.call, f.ports) ==
                          AreaResourceOutcome::native_return);
                    const bool spatial = ready != 0 || al != 0;
                    CHECK(f.resource.status_ca4 == (spatial ? 2U : 0U));
                    CHECK(f.resource.ready_2c == ready);
                    CHECK(f.resource.data_c9c == f.call.data);
                    CHECK(f.resource.position_29c == f.call.position);
                    CHECK(f.resource.x_2a4 == 3072 && f.resource.y_2a8 == 6144);
                    CHECK(f.ports.last_format.format.va == 0xF3B084);
                    CHECK(f.ports.last_format.prefix.va == 0x1140444);
                    CHECK(f.ports.last_format.level.va == 0x11405C4);
                    if (ready == 0) {
                        CHECK(f.resource.borrowed_34 == (supplied ? 1 : 0));
                        CHECK(f.resource.buffer_04.data ==
                              (supplied ? f.alternate.data() : nullptr));
                        CHECK(f.ports.last_bank.source_line ==
                              ((flags & 0xFFU) ? 0x2FU : 0x31U));
                        CHECK(f.ports.last_bank.final_argument ==
                              ((flags & 0xFFU) ? 1U : 0U));
                    } else {
                        CHECK(f.resource.buffer_04.data == f.storage.data());
                        CHECK(f.resource.allocator_38.va == 0x123456);
                        CHECK(f.resource.byte_3c == 23);
                    }
                    if (spatial)
                        CHECK((f.ports.last_spatial.arguments ==
                               std::array<std::uint32_t, 6>{
                                   3072, 6144, 3072, 6144, 0x41C00000, 0}));
                    CHECK(f.ports.events.size() ==
                          1U + (ready == 0 ? 1U : 0U) + (spatial ? 1U : 0U));
                }
    Fixture mutate;
    mutate.ports.selector = std::uint8_t{0x80};
    mutate.ports.hook = [&](std::string_view event) {
        if (event == "format") {
            mutate.resource.ready_2c = 0;
            mutate.resource.supplied_ca0 = {mutate.alternate.data(), 0};
            mutate.call = {};
        }
        if (event == "bank") {
            mutate.resource.buffer_04 = {mutate.storage.data(), 3};
            mutate.resource.size_30 = 3;
        }
    };
    CHECK(load_area_709590(mutate.resource, mutate.call, mutate.ports) ==
          AreaResourceOutcome::native_return);
    CHECK(mutate.ports.last_format.prefix.va == 0x11404C4);
    CHECK(mutate.resource.buffer_04.capacity == 3 &&
          mutate.resource.size_30 == 3);
    CHECK((mutate.resource.position_29c == GridPair{2, 1}));
    CHECK(mutate.resource.x_2a4 == 3072 && mutate.resource.y_2a8 == 6144);
    CHECK(
        (mutate.resource.data_c9c == CycleMemberRef{&mutate.object, 0x7A750}));
}

void loading_refusals() {
    for (std::size_t failure = 1; failure <= 3; ++failure) {
        Fixture f;
        f.resource.ready_2c = 0;
        f.ports.fail_at = failure;
        CHECK(load_area_709590(f.resource, f.call, f.ports) ==
              (failure == 1   ? AreaResourceOutcome::refused_format
               : failure == 2 ? AreaResourceOutcome::refused_bank_load
                              : AreaResourceOutcome::refused_spatial_load));
        CHECK(f.ports.events.size() == failure);
        CHECK(f.resource.status_ca4 == 9);
        CHECK((f.resource.position_29c == GridPair{0, 0}));
        CHECK(f.resource.x_2a4 == 0 && f.resource.y_2a8 == 0);
        if (failure >= 2) {
            CHECK(f.resource.buffer_04.is_null());
            CHECK(f.resource.allocator_38.va == 0xFA9CB0);
        }
    }
    Fixture f;
    f.ports.selector = std::nullopt;
    CHECK(load_area_709590(f.resource, f.call, f.ports) ==
          AreaResourceOutcome::refused_selector);
    CHECK(f.ports.events.empty());
    f.ports.selector = std::uint8_t{1};
    f.ports.unterminated = true;
    CHECK(load_area_709590(f.resource, f.call, f.ports) ==
          AreaResourceOutcome::refused_unterminated_path);
    CHECK(f.ports.events == std::vector<std::string>{"format"});
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
        return no;
    }
    ModeCallOutcome load_sector_706030(SectorLoadPayload&,
                                       const SectorLoadCall&) override {
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
    // Native bank rejection must still allow the caller's coordinate stores.
    // It does not establish a successful resource load or a playable world.
    Fixture f;
    Inner inner;
    AreaResourceBinding binding(inner, f.ports);
    AreaPreparation areas;
    areas.dimensions = {3, 4};
    GridPair scale{8, 8}, position{2, 1};
    areas.scale_376c = &scale;
    areas.data_3770 = {&f.object, 0x7A750};
    f.ports.active = &areas.center_2690.payload_10;
    f.ports.load_al = std::uint8_t{0};
    CHECK(load_area_708c80(areas, areas.center_2690, position, 0, binding) ==
          WorldLoadOutcome::native_return);
    CHECK(areas.center_2690.position_cc0 == position);
    CHECK(areas.center_2690.payload_10.status_ca4 == 0);
    CHECK(areas.center_2690.payload_10.position_29c == position);
    CHECK(areas.center_2690.payload_10.x_2a4 == 12288);
    CHECK(areas.center_2690.payload_10.y_2a8 == 6144);
    CHECK((f.ports.events == std::vector<std::string>{"reset_first",
                                                      "reset_second", "format",
                                                      "bank"}));
    f.ports.events.clear();
    f.ports.load_al = std::nullopt;
    areas.center_2690.position_cc0 = {99, 99};
    CHECK(load_area_708c80(areas, areas.center_2690, position, 0, binding) ==
          WorldLoadOutcome::refused_load);
    CHECK((areas.center_2690.position_cc0 == GridPair{99, 99}));
    CHECK(binding.last_load == AreaResourceOutcome::refused_bank_load);
}
} // namespace
int main() {
    reset_cases();
    loading_cases();
    loading_refusals();
    composition();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

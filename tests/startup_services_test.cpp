#include "tdur/game/startup_services.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

using namespace tdur::game;
using namespace tdur::game::startup;

namespace {

static_assert(sizeof(void*) == 8);
static_assert(std::is_abstract_v<StartupServiceBase>);

int failures = 0;
int checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
}

// Test witness only: cycle slots record calls, they are not services.
class Witness final : public StartupServiceBase {
  public:
    std::function<bool(GameModeRuntime&)> init;
    ModeInitOutcome initialize(GameModeRuntime& owner) override {
        return init(owner) ? ModeInitOutcome::initialized
                           : ModeInitOutcome::native_rejected;
    }
    void on_forward_pass() override {}
    void on_reverse_pass() override {}
    void on_shutdown() override {}
};

struct Buffer {
    std::vector<std::byte> bytes;
    explicit Buffer(std::size_t n) : bytes(n, std::byte{0x11}) {}
    Allocation view() { return {bytes.data(), bytes.size()}; }
};

class Allocator final : public AllocatorPort {
  public:
    std::vector<std::vector<std::uint32_t>> calls;
    std::vector<Allocation> results;
    std::function<void()> side_effect;
    Allocation allocate_slot20(std::uint32_t size, std::uint32_t a2,
                               std::uint32_t a3, std::uint32_t a4) override {
        calls.push_back({size, a2, a3, a4});
        if (side_effect)
            side_effect();
        if (results.empty())
            return {};
        auto result = results.front();
        results.erase(results.begin());
        return result;
    }
};

class Slot final : public HelperContextSlot {
  public:
    std::vector<HelperContext*> sequence;
    std::function<void()> on_load;
    int loads = 0;
    HelperContext* load() override {
        if (on_load)
            on_load();
        auto* result = sequence[static_cast<std::size_t>(loads)];
        ++loads;
        return result;
    }
};

class Calls final : public StartupCallPorts {
  public:
    std::vector<std::string> log;
    std::vector<std::uintptr_t> values;
    std::function<void()> after_432fb0;
    std::function<void()> after_9ec484;
    std::function<void()> after_9ec480;
    std::function<void()> after_41ee80;
    StartupGlobals* globals = nullptr;
    OpaqueWord result_9ec484 = 0x5150;
    std::uint32_t result_6754a0 = 0;
    std::uint32_t result_457e50 = 0;
    std::uint32_t next_41ee80 = 0xA0;

    void call_432fb0(Block1699070& self) override {
        log.push_back("432fb0");
        values.push_back(self.field_7c);
        if (after_432fb0)
            after_432fb0();
    }
    OpaqueWord call_via_9ec484(OpaqueWord a1, std::uint32_t a2,
                               std::uint32_t a3, std::uint32_t a4,
                               std::uint32_t a5, std::uint32_t a6) override {
        log.push_back("9ec484");
        values.insert(values.end(), {a1, a2, a3, a4, a5, a6});
        if (after_9ec484)
            after_9ec484();
        return result_9ec484;
    }
    void call_via_9ec480(OpaqueWord a1, std::int32_t a2,
                         OpaqueWord a3) override {
        log.push_back("9ec480");
        values.insert(values.end(),
                      {a1, static_cast<std::uintptr_t>(a2 + 100), a3});
        if (after_9ec480)
            after_9ec480();
    }
    void call_via_9ec47c(OpaqueWord a1, NativeIdentity a2) override {
        log.push_back("9ec47c");
        values.insert(values.end(), {a1, a2.va});
    }
    void call_438730(Block1699140& self) override {
        log.push_back("438730");
        values.push_back(self.field_4c);
    }
    std::uint32_t call_6754a0(Subobject5B4E4&) override {
        log.push_back("6754a0");
        return result_6754a0;
    }
    void call_6755a0(BlockF5B4B0& self) override {
        log.push_back("6755a0");
        values.push_back(self.field_38.is_null() ? 0 : 1);
    }
    std::uint32_t call_41ee80(Subobject169AA04& self,
                              std::uint32_t eax) override {
        log.push_back("41ee80");
        values.insert(values.end(), {eax, self.field_08});
        if (after_41ee80)
            after_41ee80();
        return next_41ee80++;
    }
    std::uint32_t call_457e50() override {
        log.push_back("457e50");
        return result_457e50;
    }
};

bool all_bytes(const Buffer& b, std::size_t from, std::size_t to, std::byte v) {
    for (std::size_t i = from; i < to; ++i)
        if (b.bytes[i] != v)
            return false;
    return true;
}

void test_helpers() {
    Allocator allocator;
    Witness member;
    // Inactive: no allocation, no store, one load.
    {
        HelperContext g{0, 8};
        Slot slot;
        slot.sequence = {&g};
        check(initialization_helper_9b40c0(member, slot, allocator) ==
                  HelperOutcome::inactive,
              "helper inactive outcome");
        check(allocator.calls.empty() && slot.loads == 1,
              "helper inactive without allocation");
    }
    // Nominal 9B40C0: G reloaded before the store, length from second G,
    // flag not retested, old block kept intact.
    Buffer old_block(4);
    member.allocation_04 = old_block.view();
    Buffer block(16);
    HelperContext g1{1, 12};
    HelperContext g2{0, 7};
    Slot slot;
    slot.sequence = {&g1, &g2};
    std::byte* seen_at_reload = nullptr;
    slot.on_load = [&] {
        if (slot.loads == 1)
            seen_at_reload = member.allocation_04.data;
    };
    allocator.results = {block.view()};
    check(initialization_helper_9b40c0(member, slot, allocator) ==
              HelperOutcome::filled,
          "helper 9b40c0 filled");
    check(allocator.calls.size() == 1 &&
              allocator.calls[0] == std::vector<std::uint32_t>{12, 0, 0, 0},
          "helper allocates first G +EC with zeros");
    check(seen_at_reload == old_block.bytes.data(),
          "helper reloads G before storing");
    check(member.allocation_04.data == block.bytes.data(),
          "helper stores into +04");
    check(all_bytes(block, 0, 7, std::byte{0xBD}) &&
              all_bytes(block, 7, 16, std::byte{0x11}),
          "helper fills second G length with BD");
    check(all_bytes(old_block, 0, 4, std::byte{0x11}),
          "helper leaves previous block untouched");
    // 9B4060 writes +08 with AD and leaves +04.
    Buffer block8(8);
    HelperContext g3{1, 3};
    Slot slot2;
    slot2.sequence = {&g3, &g3};
    allocator.results = {block8.view()};
    check(initialization_helper_9b4060(member, slot2, allocator) ==
              HelperOutcome::filled,
          "helper 9b4060 filled");
    check(member.allocation_08.data == block8.bytes.data() &&
              member.allocation_04.data == block.bytes.data(),
          "helper 9b4060 stores +08 only");
    check(all_bytes(block8, 0, 3, std::byte{0xAD}) &&
              all_bytes(block8, 3, 8, std::byte{0x11}),
          "helper 9b4060 fills with AD");
    // Fill larger than owned storage: store kept, no write, refusal.
    Buffer small(4);
    HelperContext g4{1, 2};
    HelperContext g5{1, 9};
    Slot slot3;
    slot3.sequence = {&g4, &g5};
    allocator.results = {small.view()};
    check(initialization_helper_9b40c0(member, slot3, allocator) ==
              HelperOutcome::refused_fill_exceeds_storage,
          "helper refuses fill beyond storage");
    check(member.allocation_04.data == small.bytes.data() &&
              all_bytes(small, 0, 4, std::byte{0x11}),
          "helper refusal stores without clamped fill");
    // Null allocation with zero length: valid, no access.
    HelperContext g6{1, 0};
    Slot slot4;
    slot4.sequence = {&g6, &g6};
    allocator.results = {};
    check(initialization_helper_9b4060(member, slot4, allocator) ==
                  HelperOutcome::filled &&
              member.allocation_08.is_null(),
          "helper zero length null allocation");
    // Missing contexts.
    Slot slot5;
    slot5.sequence = {nullptr};
    const auto calls_before = allocator.calls.size();
    check(initialization_helper_9b40c0(member, slot5, allocator) ==
                  HelperOutcome::refused_missing_context &&
              allocator.calls.size() == calls_before,
          "helper refuses missing first G");
    Slot slot6;
    HelperContext g7{1, 4};
    slot6.sequence = {&g7, nullptr};
    Buffer block9(4);
    allocator.results = {block9.view()};
    check(initialization_helper_9b40c0(member, slot6, allocator) ==
                  HelperOutcome::refused_missing_context &&
              member.allocation_04.data == block9.bytes.data(),
          "helper refuses missing second G after store");
}

struct Fixture {
    StartupGlobals globals;
    Calls calls;
    Allocator allocator;
    Object143F520 object{0x55};
    GameModeRuntime owner;
    Witness self;
    StartupContext context() { return {globals, calls, allocator}; }
};

void test_98f020() {
    Fixture f;
    f.globals.global_143f520 = &f.object;
    f.globals.global_10e8058 = 0x400000;
    f.globals.block_1699070.field_4c = 0x77;
    f.self.flag_18 = 9;
    f.calls.after_432fb0 = [&] { f.globals.global_10e8058 = 0x410000; };
    f.calls.after_9ec484 = [&] { f.globals.block_1699070.field_4c = 0x88; };
    f.calls.after_9ec480 = [&] { f.globals.block_1699070.field_4c = 0x99; };
    check(initialize_98f020(f.self, f.owner, f.context()) ==
              InitOutcome::completed,
          "98f020 completed");
    check(f.object.byte_14 == 0 && f.globals.global_fa9e24 == &f.object &&
              f.globals.global_fa8fd4 == &f.object &&
              f.globals.block_1699070.field_2c == &f.object,
          "98f020 object writes");
    check(f.self.owner_10 == &f.owner && f.self.flag_18 == 0,
          "98f020 owner and flag");
    check((f.calls.log ==
           std::vector<std::string>{"432fb0", "9ec484", "9ec480", "9ec47c"}),
          "98f020 call order");
    const std::vector<std::uintptr_t> expected{
        0x7530, 0x410000, 0x65,      1,      0,    0,
        0x40,   0x88,     100 - 0xE, 0x5150, 0x99, 0xF08820};
    check(f.calls.values == expected, "98f020 live arguments");
    check(f.globals.block_1699070.field_80 == 0x1F400000 &&
              f.globals.block_1699070.field_84 == 0x100000 &&
              f.globals.block_1699070.field_64.va == 0x98EF90,
          "98f020 constants");
    Fixture g;
    check(initialize_98f020(g.self, g.owner, g.context()) ==
                  InitOutcome::refused_missing_object_143f520 &&
              g.calls.log.empty() && g.self.owner_10 == nullptr,
          "98f020 refuses missing object without effect");
}

void test_9cfa40() {
    Fixture f;
    auto& b = f.globals.block_1699140;
    b.field_3c = 0xFFFFFFF0u;
    b.field_40 = 3;
    b.field_44 = 0x20000001u;
    Buffer block(4);
    f.allocator.results = {block.view()};
    std::uint32_t seen_4c = 0;
    f.allocator.side_effect = [&] {
        seen_4c = b.field_4c;
        b.field_3c = 0;
    };
    initialize_9cfa40(f.self, f.owner, f.context());
    const std::uint32_t expected =
        (0xFFFFFFF0u + 0x1Du) * 3u + 0x20000001u * 8u + 0x10u;
    check(f.allocator.calls.size() == 1 &&
              f.allocator.calls[0][0] == expected && seen_4c == expected,
          "9cfa40 DWORD size stored before allocation");
    check(b.field_50.data == block.bytes.data() && b.field_2c.va == 0xFA7890,
          "9cfa40 stores allocation");
    check(f.calls.log == std::vector<std::string>{"438730"} &&
              f.calls.values[0] == expected,
          "9cfa40 final call");
}

void test_small_bodies() {
    Fixture f;
    Fields982C40 fields{5, 5, 5, 5, 5};
    initialize_982c40(f.self, fields, f.owner);
    check(fields.field_28 == 1.0F && fields.field_2c == 1.0F &&
              fields.field_30 == 0.0F && fields.field_34 == 0.0F &&
              fields.field_38 == 0.0F && f.self.owner_10 == &f.owner,
          "982c40 fields");
    Witness other;
    other.flag_18 = 4;
    check(initialize_998dd0(other, f.owner) == InitOutcome::completed &&
              other.owner_10 == nullptr && other.flag_18 == 4,
          "998dd0 writes nothing");
    initialize_9b3f80(other, f.owner);
    check(other.owner_10 == &f.owner && other.flag_18 == 0,
          "9b3f80 owner and flag");
}

void test_9a7e10() {
    // Branch not taken: tail only, wrapped size.
    {
        Fixture f;
        auto& r = f.globals.region_169a808;
        r.v169a894 = 0x80000001u;
        Fields9A7E10 fields{1, 1};
        initialize_9a7e10(f.self, fields, f.owner, f.context());
        const std::uint32_t size = ((0x80000001u * 4u) >> 5) + 0x80000001u;
        check(r.v169a89c == size && f.allocator.calls[0][0] == size,
              "9a7e10 DWORD size wraps");
        check(f.calls.log.empty() && r.v169a874 == 0xC &&
                  r.v169a8ac.va == 0x9A7CD0 && fields.field_1c == 7 &&
                  fields.field_20 == 0,
              "9a7e10 skipped branch tail");
    }
    // Taken, null allocation: no 41ee80, flag 828 set on zero status.
    {
        Fixture f;
        f.globals.global_143f520 = &f.object;
        auto& r = f.globals.region_169a808;
        r.v169a808 = 1;
        r.v169a80c = 4;
        r.v169a86c = 0x86C;
        r.v169a870 = 0x870;
        r.v169a8dc.fill(5);
        Fields9A7E10 fields;
        initialize_9a7e10(f.self, fields, f.owner, f.context());
        check(f.calls.log == std::vector<std::string>{"457e50"} &&
                  r.v169a828 == 1 && r.v169a8b8 == 0x86C &&
                  r.v169a8bc == 0x870 && r.v169a8dc[63] == 0 &&
                  r.v169a824 == &f.object,
              "9a7e10 null allocation path");
    }
    // Taken, allocation: count reloaded after the allocator callback.
    {
        Fixture f;
        auto& r = f.globals.region_169a808;
        r.v169a808 = 1;
        r.v169a80c = 4;
        r.v169a894 = 64;
        Buffer block(4);
        f.allocator.results = {block.view()};
        f.allocator.side_effect = [&] { r.v169a894 = 0x41; };
        f.calls.result_457e50 = 3;
        f.calls.after_41ee80 = [&] { r.v169a894 = 0x999; };
        Fields9A7E10 fields;
        initialize_9a7e10(f.self, fields, f.owner, f.context());
        const std::uint32_t size = ((64u * 4u) >> 5) + 64u;
        check((f.calls.log ==
               std::vector<std::string>{"41ee80", "41ee80", "457e50"}),
              "9a7e10 call order");
        check((f.calls.values ==
               std::vector<std::uintptr_t>{0x41, size, 8, size}),
              "9a7e10 41ee80 arguments");
        check(r.v169a9f8 == 0xA0 && r.v169a9f0 == 0xA1 && r.v169aa00 == 0x41 &&
                  r.v169a9fc == 2 &&
                  r.v169aa04.field_00.data == block.bytes.data() &&
                  r.v169a8a0 == 3 && r.v169a828 == 0,
              "9a7e10 stored results");
    }
    // Branch refused by a nonzero 8A0.
    {
        Fixture f;
        auto& r = f.globals.region_169a808;
        r.v169a808 = 1;
        r.v169a80c = 4;
        r.v169a8a0 = 2;
        Fields9A7E10 fields;
        initialize_9a7e10(f.self, fields, f.owner, f.context());
        check(f.calls.log.empty() && r.v169a8a0 == 2,
              "9a7e10 nonzero 8a0 skips");
    }
}

void test_999e80() {
    Fixture f;
    f.globals.global_143f520 = &f.object;
    Buffer stale(2);
    f.globals.block_f5b4b0.field_38 = stale.view();
    initialize_999e80(f.self, f.owner, f.context());
    check((f.calls.log == std::vector<std::string>{"6754a0", "6755a0"}) &&
              f.allocator.calls.empty() &&
              f.globals.block_f5b4b0.field_38.is_null() &&
              f.calls.values == std::vector<std::uintptr_t>{0},
          "999e80 zero size path");
    Fixture g;
    g.calls.result_6754a0 = 24;
    Buffer block(24);
    g.allocator.results = {block.view()};
    initialize_999e80(g.self, g.owner, g.context());
    check(g.allocator.calls.size() == 1 && g.allocator.calls[0][0] == 24 &&
              g.globals.block_f5b4b0.field_3c == 24 &&
              g.calls.values == std::vector<std::uintptr_t>{1},
          "999e80 allocation path");
}

class ControllerPorts final : public ModeLifecyclePorts {
  public:
    Slot* slot = nullptr;
    Allocator* allocator = nullptr;
    std::vector<std::string> log;
    std::uint32_t startup_gate() override { return 0; }
    ModeSubsystemPtr boot_subsystem(std::size_t) override { return {}; }
    ModeCallOutcome begin_initialization(ModeSubsystem& member) override {
        log.push_back("begin");
        auto& base = dynamic_cast<StartupServiceBase&>(member);
        const auto result =
            initialization_helper_9b40c0(base, *slot, *allocator);
        return result == HelperOutcome::filled ||
                       result == HelperOutcome::inactive
                   ? ModeCallOutcome::native_returned
                   : ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome end_initialization(ModeSubsystem& member) override {
        log.push_back("end");
        auto& base = dynamic_cast<StartupServiceBase&>(member);
        const auto result =
            initialization_helper_9b4060(base, *slot, *allocator);
        return result == HelperOutcome::filled ||
                       result == HelperOutcome::inactive
                   ? ModeCallOutcome::native_returned
                   : ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome configure_startup(GameModeRuntime&,
                                      ModeSubsystem&) override {
        return ModeCallOutcome::native_returned;
    }
    ModeLifecycleStatus complete_startup(GameModeRuntime&) override {
        return ModeLifecycleStatus::success;
    }
};

void test_controller_link() {
    GameModeRuntime runtime;
    Allocator allocator;
    Buffer a(4);
    Buffer b(4);
    allocator.results = {a.view(), b.view()};
    HelperContext g{1, 4};
    Slot slot;
    slot.sequence = {&g, &g, &g, &g};
    ControllerPorts ports;
    ports.slot = &slot;
    ports.allocator = &allocator;
    auto service = std::make_shared<Witness>();
    std::int32_t status_seen = 0;
    service->init = [&](GameModeRuntime& owner) {
        status_seen = service->startup_status;
        ports.log.push_back("init");
        return initialize_9b3f80(*service, owner) == InitOutcome::completed;
    };
    check(register_mode_subsystem(runtime, service, ports) ==
              ModeLifecycleStatus::success,
          "controller registers witness");
    check((ports.log == std::vector<std::string>{"begin", "init", "end"}) &&
              status_seen == -1 && runtime.subsystem_count == 1 &&
              service->owner_10 == &runtime,
          "controller order and owner");
    check(all_bytes(a, 0, 4, std::byte{0xBD}) &&
              all_bytes(b, 0, 4, std::byte{0xAD}),
          "controller helpers fill both blocks");
}

// Independent cases from the native branches, rather than precomputed
// expected values. Compare whole DWORD selectors, including high bits.
void test_branch_widths_and_live_selectors() {
    struct Case {
        std::uint32_t first;
        std::uint32_t second;
        std::uint32_t status;
        bool enters;
    };
    constexpr Case cases[]{{1, 4, 0, true},        {0, 4, 0, false},
                           {1, 3, 0, false},       {0x10001, 4, 0, false},
                           {1, 0x10004, 0, false}, {1, 4, 0x100, false}};
    for (const auto& row : cases) {
        Fixture f;
        auto& r = f.globals.region_169a808;
        r.v169a808 = row.first;
        r.v169a80c = row.second;
        r.v169a8a0 = row.status;
        r.v169a828 = 0x80;
        Fields9A7E10 fields;
        initialize_9a7e10(f.self, fields, f.owner, f.context());
        check((f.calls.log == std::vector<std::string>{"457e50"}) == row.enters,
              "9a7e10 compares all selector bits");
        check(r.v169a828 == 0x80,
              "9a7e10 preserves a nonzero flag rather than normalizing it");
    }
    Fixture f;
    auto& r = f.globals.region_169a808;
    r.v169a894 = 32;
    Buffer storage(8);
    f.allocator.results = {storage.view()};
    f.allocator.side_effect = [&] {
        r.v169a808 = 1;
        r.v169a80c = 4;
        r.v169a89c = 55;
        r.v169a894 = 96;
    };
    Fields9A7E10 fields;
    initialize_9a7e10(f.self, fields, f.owner, f.context());
    check(
        (f.calls.log == std::vector<std::string>{"41ee80", "41ee80", "457e50"}),
        "9a7e10 reloads selectors after allocation");
    check((f.calls.values == std::vector<std::uintptr_t>{96, 55, 12, 55}) &&
              r.v169aa00 == 96 && r.v169a9fc == 3,
          "9a7e10 reloads size and count before the two opaque calls");
}

void test_allocation_failure_keeps_native_continuation() {
    Fixture first;
    first.globals.block_1699140.field_3c = 1;
    first.globals.block_1699140.field_40 = 2;
    check(initialize_9cfa40(first.self, first.owner, first.context()) ==
                  InitOutcome::completed &&
              first.globals.block_1699140.field_50.is_null() &&
              first.calls.log == std::vector<std::string>{"438730"},
          "9cfa40 still calls the next port with a null allocation");

    Fixture second;
    second.calls.result_6754a0 = 12;
    second.allocator.side_effect = [&] {
        second.globals.block_f5b4b0.field_3c = 73;
    };
    check(
        initialize_999e80(second.self, second.owner, second.context()) ==
                InitOutcome::completed &&
            second.globals.block_f5b4b0.field_38.is_null() &&
            second.globals.block_f5b4b0.field_3c == 73 &&
            second.calls.log == std::vector<std::string>{"6754a0", "6755a0"},
        "999e80 continues after null allocation and preserves callback writes");

    Witness member;
    Buffer untouched(4);
    member.allocation_08 = untouched.view();
    HelperContext g{1, 4};
    Slot slot;
    slot.sequence = {&g, &g};
    Allocator allocator;
    check(initialization_helper_9b4060(member, slot, allocator) ==
                  HelperOutcome::refused_fill_exceeds_storage &&
              member.allocation_08.is_null() &&
              all_bytes(untouched, 0, 4, std::byte{0x11}),
          "helper stores null but refuses a nonempty fill without access");
}

void test_opaque_words_keep_x64_values() {
    Fixture f;
    f.globals.global_143f520 = &f.object;
    constexpr OpaqueWord opaque = (OpaqueWord{1} << 40) | 0x1234;
    f.globals.global_10e8058 = opaque;
    f.globals.block_1699070.field_4c = opaque + 1;
    f.calls.result_9ec484 = opaque + 2;
    initialize_98f020(f.self, f.owner, f.context());
    check(f.calls.values[1] == opaque && f.calls.values[7] == opaque + 1 &&
              f.calls.values[9] == opaque + 2 &&
              f.calls.values[10] == opaque + 1,
          "98f020 forwards opaque x64 values without truncating them");
}

} // namespace

int main() {
    test_helpers();
    test_98f020();
    test_9cfa40();
    test_small_bodies();
    test_9a7e10();
    test_999e80();
    test_controller_link();
    test_branch_widths_and_live_selectors();
    test_allocation_failure_keeps_native_continuation();
    test_opaque_words_keep_x64_values();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

#include "tdur/game/startup_driver_980af0.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace tdur::game;
using namespace tdur::game::startup;
using tdur::input::NativeInputDescription;
using tdur::input::NativeInputRecord;
using Outcome = Init980AF0Outcome;

int checks = 0;
int failures = 0;

void check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

// Test double: no cycle behavior, only satisfies the abstract interface.
class TestService final : public StartupServiceBase {
  public:
    ModeInitOutcome initialize(GameModeRuntime&) override {
        return ModeInitOutcome::native_rejected;
    }
    void on_forward_pass() override {}
    void on_reverse_pass() override {}
    void on_shutdown() override {}
};

struct Ports final : Driver980AF0Ports {
    std::vector<std::string> log;
    std::function<void()> on_4358d0;
    std::function<void()> on_433bc0;
    std::function<void(FrameBuffer)> on_97fbd0;
    std::function<void()> on_b8;
    std::function<void()> on_c8;
    std::function<void()> on_c0;
    std::uint32_t ret_4358d0 = 0;
    std::uint32_t ret_97f860 = 0;
    OpaqueWord ret_9ec464 = 0x1234;
    std::uint32_t ret_b8 = 0, ret_c4 = 0, ret_d8 = 0, ret_e0 = 0;
    Object143F520* seen_ecx = nullptr;
    NativeIdentity seen_esi;
    std::uint32_t seen_ebx = 1;
    OpaqueWord seen_b8 = 0;
    std::uint32_t seen_c8 = 0, seen_c0 = 0;
    const std::byte* buffer_fbd0 = nullptr;
    const std::byte* buffer_f860 = nullptr;
    std::byte first_f860{};
    std::uint32_t seen_c4 = 0xFFFF'FFFFu;

    std::uint32_t call_4358d0(Object143F520* ecx, NativeIdentity esi,
                              std::uint32_t ebx) override {
        log.push_back("4358d0");
        seen_ecx = ecx;
        seen_esi = esi;
        seen_ebx = ebx;
        if (on_4358d0)
            on_4358d0();
        return ret_4358d0;
    }
    void call_433bc0(DescriptionRegistry&) override {
        log.push_back("433bc0");
        if (on_433bc0)
            on_433bc0();
    }
    void call_97fbd0(FrameBuffer buffer) override {
        log.push_back("97fbd0");
        buffer_fbd0 = buffer.data();
        if (on_97fbd0)
            on_97fbd0(buffer);
    }
    std::uint32_t call_97f860(FrameBuffer buffer) override {
        log.push_back("97f860");
        buffer_f860 = buffer.data();
        first_f860 = buffer[0];
        return ret_97f860;
    }
    OpaqueWord call_via_9ec464() override {
        log.push_back("9ec464");
        return ret_9ec464;
    }
    std::uint32_t call_via_10e72b8(OpaqueWord arg) override {
        log.push_back("b8");
        seen_b8 = arg;
        if (on_b8)
            on_b8();
        return ret_b8;
    }
    std::uint32_t call_via_10e72c4(std::uint32_t& value) override {
        log.push_back("c4");
        seen_c4 = value;
        value = 0xDEAD'BEEFu;
        return ret_c4;
    }
    void call_via_10e72c8(std::uint32_t arg) override {
        log.push_back("c8");
        seen_c8 = arg;
        if (on_c8)
            on_c8();
    }
    void call_via_10e72c0(std::uint32_t arg) override {
        log.push_back("c0");
        seen_c0 = arg;
        if (on_c0)
            on_c0();
    }
    std::uint32_t call_via_10e72d8() override {
        log.push_back("d8");
        return ret_d8;
    }
    std::uint32_t call_via_10e72e0() override {
        log.push_back("e0");
        return ret_e0;
    }
};

struct Allocator final : AllocatorPort {
    std::vector<std::string>* log = nullptr;
    std::uint32_t size = 0, a2 = 9, a3 = 9, a4 = 9;
    std::array<std::byte, 16> block{};
    bool return_null = false;
    Allocation allocate_slot20(std::uint32_t s, std::uint32_t b,
                               std::uint32_t c, std::uint32_t d) override {
        log->push_back("alloc");
        size = s;
        a2 = b;
        a3 = c;
        a4 = d;
        return return_null ? Allocation{}
                           : Allocation{block.data(), block.size()};
    }
};

struct Lookup final : tdur::input::NativeInputNameLookup {
    std::vector<std::string>* log = nullptr;
    std::int32_t result = -1;
    std::int32_t command_index(NativeInputDescription&,
                               std::string_view name) override {
        log->push_back(std::string("lookup:") + std::string(name));
        return result;
    }
};

std::shared_ptr<NativeInputRecord> record_of(std::uint32_t kind) {
    auto record = std::make_shared<NativeInputRecord>();
    for (auto& value : record->bytes)
        value = std::byte{0xAB};
    std::memcpy(record->bytes.data(), &kind, sizeof kind);
    return record;
}

std::uint32_t dword_at(const NativeInputRecord& record, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, record.bytes.data() + offset, sizeof value);
    return value;
}

constexpr std::uint32_t bits_08 = 0x3F4C'CCCDu;

struct Fixture {
    TestService service;
    Fields980AF0 fields;
    GameModeRuntime owner;
    StartupGlobals startup;
    Driver980AF0Globals globals;
    Ports ports;
    Allocator allocator;
    Lookup lookup;
    Fixture() {
        allocator.log = &ports.log;
        lookup.log = &ports.log;
    }
    Outcome run() {
        return initialize_980af0(
            service, fields, owner,
            Driver980AF0Context{startup, globals, ports, allocator, lookup});
    }
    void add_description(std::uint32_t id,
                         std::shared_ptr<NativeInputDescription> description,
                         DescriptionCounters counters) {
        globals.registry_fa9d30.ids.push_back(id);
        globals.registry_fa9d30.descriptions.push_back(std::move(description));
        globals.registry_fa9d30.counters.push_back(counters);
    }
    void all_present() {
        globals.present_10e72b8 = globals.present_10e72c4 = true;
        globals.present_10e72c8 = globals.present_10e72c0 = true;
        globals.present_10e72d8 = globals.present_10e72e0 = true;
    }
};

void test_prologue() {
    Fixture f;
    Object143F520 object;
    f.startup.global_143f520 = &object;
    f.startup.global_10e8058 = 0x5555;
    f.startup.block_1699070.field_4c = 0x6666;
    f.globals.global_fa9db0 = 7;
    f.globals.global_fa9dc4 = 0x4242;
    f.service.flag_18 = 9;
    f.ports.ret_4358d0 = 0xFFF0'0000u;
    // Live reads after the callback: values set by 0x4358D0 are used.
    f.ports.on_4358d0 = [&] {
        f.globals.global_fa9da0 = 0x0444'4445u;
        f.globals.global_fa9da4 = 0x2000'0001u;
    };
    check(f.run() == Outcome::completed, "prologue completes");
    check(f.service.owner_10 == &f.owner, "owner +0x10");
    check(f.service.flag_18 == 0, "flag +0x18 cleared");
    check(f.globals.global_fa9d5c == &object, "FA9D5C = [143F520]");
    check(f.globals.global_fa9d9c == 0x5555, "FA9D9C = [10E8058]");
    check(f.globals.global_fa9d98 == 0x6666, "FA9D98 = [16990BC]");
    check(f.globals.global_fa9db0 == 0, "FA9DB0 = 0");
    check(f.globals.global_fa9dc8 == 0x4242, "FA9DC8 = FA9DC4");
    check(f.ports.seen_ecx == &object && f.ports.seen_esi.va == 0xFA9D8C &&
              f.ports.seen_ebx == 0,
          "4358D0 registers");
    const std::uint32_t expected =
        0xFFF0'0000u + 0x2000'0001u * 8u + 0x10'0000u + 0x0444'4445u * 0x3Cu;
    check(f.globals.global_fa9dac == expected, "FA9DAC formula mod 2^32");
    check(f.allocator.size == expected && f.allocator.a2 == 0 &&
              f.allocator.a3 == 0 && f.allocator.a4 == 0,
          "slot +0x20 arguments");
    check(f.globals.global_fa9da8.data == f.allocator.block.data(),
          "FA9DA8 = allocation");
    // No ID 1 description: 980550 clears outputs without lookups.
    const std::vector<std::string> order{"4358d0", "alloc",  "433bc0",
                                         "97fbd0", "97f860", "9ec464"};
    check(f.ports.log == order, "call order without description 1");
}

void test_description_ranks() {
    Fixture f;
    auto description = std::make_shared<NativeInputDescription>();
    for (std::uint32_t kind : {0u, 1u, 2u, 1u, 1u})
        description->records.push_back(record_of(kind));
    f.add_description(2, description, {5, 2});
    f.globals.global_fa9d94 = 1;
    check(f.run() == Outcome::completed, "ranks complete");
    const auto& r = description->records;
    check(dword_at(*r[1], 0x38) == bits_08 && dword_at(*r[1], 0x3C) == bits_08,
          "rank 0 -> record 1 at +0x38/+0x3C");
    check(dword_at(*r[3], 0x38) == bits_08, "rank 1 -> record 3");
    check(dword_at(*r[4], 0x38) == 0xABABABABu, "rank 2 not requested");
    check(dword_at(*r[0], 0x38) == 0xABABABABu, "type 0 untouched");
    check(dword_at(*r[1], 0x34) == 0xABABABABu &&
              dword_at(*r[1], 0x40) == 0xABABABABu,
          "neighbour bytes preserved");
}

void test_description_counts() {
    {
        Fixture f; // Negative +0x14 (signed JLE): no write.
        auto d = std::make_shared<NativeInputDescription>();
        d->records.push_back(record_of(1));
        f.add_description(2, d, {1, 0x8000'0000u});
        f.globals.global_fa9d94 = 1;
        check(f.run() == Outcome::completed, "negative +0x14 completes");
        check(dword_at(*d->records[0], 0x38) == 0xABABABABu,
              "negative +0x14 writes nothing");
    }
    {
        Fixture f; // Negative pass count: loop skipped.
        auto d = std::make_shared<NativeInputDescription>();
        d->records.push_back(record_of(1));
        f.add_description(2, d, {1, 1});
        f.globals.global_fa9d94 = 0xFFFF'FFFFu;
        check(f.run() == Outcome::completed, "negative FA9D94 completes");
        check(dword_at(*d->records[0], 0x38) == 0xABABABABu,
              "negative FA9D94 skips");
    }
    {
        Fixture f; // Two ID 2 descriptions by ordinal; 433BC0 sets count.
        auto a = std::make_shared<NativeInputDescription>();
        auto b = std::make_shared<NativeInputDescription>();
        a->records.push_back(record_of(1));
        b->records.push_back(record_of(1));
        f.add_description(2, a, {1, 1});
        f.add_description(3, std::make_shared<NativeInputDescription>(),
                          {0, 0});
        f.add_description(2, b, {1, 1});
        f.ports.on_433bc0 = [&] { f.globals.global_fa9d94 = 2; };
        check(f.run() == Outcome::completed, "ordinals complete");
        check(dword_at(*a->records[0], 0x3C) == bits_08 &&
                  dword_at(*b->records[0], 0x3C) == bits_08,
              "ordinal 0 and 1 stamped after live 433BC0 update");
    }
    {
        Fixture f; // Null slot sentinel ends the search.
        auto d = std::make_shared<NativeInputDescription>();
        d->records.push_back(record_of(1));
        f.globals.registry_fa9d30.ids.push_back(std::nullopt);
        f.globals.registry_fa9d30.descriptions.push_back(nullptr);
        f.globals.registry_fa9d30.counters.push_back({});
        f.add_description(2, d, {1, 1});
        f.globals.global_fa9d94 = 1;
        check(f.run() == Outcome::completed, "sentinel completes");
        check(dword_at(*d->records[0], 0x38) == 0xABABABABu,
              "sentinel hides later slots");
    }
    {
        Fixture f; // Declared count beyond owned records, match found early.
        auto d = std::make_shared<NativeInputDescription>();
        d->records.push_back(record_of(1));
        f.add_description(2, d, {9, 1});
        f.globals.global_fa9d94 = 1;
        check(f.run() == Outcome::completed, "early match within extent");
    }
    {
        Fixture f; // Scan reaches beyond owned records: refusal.
        auto d = std::make_shared<NativeInputDescription>();
        d->records.push_back(record_of(0));
        f.add_description(2, d, {2, 1});
        f.globals.global_fa9d94 = 1;
        check(f.run() == Outcome::refused_record_extent,
              "record extent refusal");
        check(f.ports.log.size() == 3, "refusal before 97FBD0");
    }
    {
        Fixture f; // Inconsistent registry views.
        f.globals.registry_fa9d30.ids.push_back(2u);
        f.globals.global_fa9d94 = 1;
        check(f.run() == Outcome::refused_registry_views,
              "registry view refusal");
    }
}

struct RowsFixture : Fixture {
    std::array<CellRow, 4> rows{};
    std::array<CellRow*, 4> table{};
    std::array<Record11403D0, 1> records{};
    std::array<std::byte, 64> storage{};
    RowsFixture() {
        for (auto& row : rows) {
            row.bytes_00.fill(std::byte{0x11});
            row.text_1c.fill(std::byte{0x22});
            row.field_14 = 0x77;
        }
        storage.fill(std::byte{0xCC});
        globals.rows_fa9dc0 = rows;
        globals.global_fa9da0 = 4;
        records[0].field_130 = 1;
        records[0].table_134 = table;
        globals.records_11403d0 = records;
        globals.count_11403c4 = 1;
        globals.value_11403cc = 2;
        globals.pool.dword_1699130 = 1;
        globals.pool.cursor_1699134 = {true, 8};
        globals.pool.limit_1699138 = 0x100;
        globals.pool.used_169913c = 0x10;
        globals.pool.storage = storage;
    }
};

void test_rows_block() {
    RowsFixture f;
    check(f.run() == Outcome::completed, "row with block completes");
    const CellRow& row = f.rows[0];
    check(f.table[0] == &row, "T[0] = row 0");
    check(f.globals.global_fa9db8 == 1, "FA9DB8 incremented");
    check(row.text_1c[0] == std::byte{0} && row.text_1c[1] == std::byte{0x22},
          "only first byte of +0x1C cleared");
    check(row.bytes_00[0] == std::byte{0x11}, "row bytes preserved");
    check(row.field_10 == 2, "+0x10 = [11403CC]");
    check(row.field_18.data == f.storage.data() + 8 && row.field_18.bytes == 16,
          "+0x18 = pool block");
    check(row.field_14 == 0, "+0x14 cleared with block");
    bool zero = true;
    for (std::size_t i = 8; i < 24; ++i)
        zero = zero && f.storage[i] == std::byte{0};
    check(zero, "block zeroed");
    check(f.storage[7] == std::byte{0xCC} && f.storage[24] == std::byte{0xCC},
          "outside block untouched");
    check(f.globals.pool.cursor_1699134.offset == 24 &&
              f.globals.pool.used_169913c == 0x20,
          "pool counters advanced");
}

void test_rows_pool_states() {
    {
        RowsFixture f;
        f.globals.pool.dword_1699130 = 0;
        check(f.run() == Outcome::completed, "no pool completes");
        check(f.rows[0].field_18.is_null() && f.rows[0].field_14 == 0x77,
              "no pool: +0x18 null, +0x14 kept");
        check(f.globals.pool.used_169913c == 0x10, "no pool: counters kept");
    }
    {
        RowsFixture f; // Unsigned JA: exhausted pool.
        f.globals.pool.limit_1699138 = 0x1F;
        check(f.run() == Outcome::completed, "exhausted pool completes");
        check(f.rows[0].field_18.is_null() &&
                  f.globals.pool.cursor_1699134.offset == 8,
              "exhausted pool: no block, cursor kept");
    }
    {
        RowsFixture f; // Used + size wraps modulo 2^32, then passes.
        f.globals.pool.used_169913c = 0xFFFF'FFF8u;
        f.globals.pool.limit_1699138 = 0x10;
        check(f.run() == Outcome::completed, "wrapping limit completes");
        check(f.globals.pool.used_169913c == 8 && !f.rows[0].field_18.is_null(),
              "wrapping limit takes block");
    }
    {
        RowsFixture f; // JA is unsigned: limit 0x80000000 is not negative.
        f.globals.pool.limit_1699138 = 0x8000'0000u;
        check(f.run() == Outcome::completed, "high limit completes");
        check(!f.rows[0].field_18.is_null(), "high limit is unsigned");
    }
    {
        RowsFixture f; // Null cursor: counters move, block zero.
        f.globals.pool.cursor_1699134 = {false, 0};
        check(f.run() == Outcome::completed, "null cursor completes");
        check(f.rows[0].field_18.is_null() &&
                  f.globals.pool.cursor_1699134.offset == 16 &&
                  f.globals.pool.used_169913c == 0x20,
              "null cursor: counters updated, no block");
    }
    {
        RowsFixture f;
        f.globals.pool.cursor_1699134 = {false, 0x40};
        check(f.run() == Outcome::refused_cell_block_address,
              "non-owned cursor refused");
        check(f.globals.pool.used_169913c == 0x20, "counters kept on refusal");
    }
    {
        RowsFixture f;
        f.globals.pool.cursor_1699134 = {true, 56};
        check(f.run() == Outcome::refused_cell_block_extent,
              "block beyond storage refused");
        check(f.storage[56] == std::byte{0xCC}, "no write before refusal");
    }
    {
        RowsFixture f; // Count 0x80000000: size 0, no entry loop.
        f.globals.value_11403cc = 0x8000'0000u;
        check(f.run() == Outcome::completed, "negative count completes");
        check(f.rows[0].field_18.data == f.storage.data() + 8 &&
                  f.rows[0].field_18.bytes == 0 && f.rows[0].field_14 == 0,
              "negative count: empty block");
        check(f.storage[8] == std::byte{0xCC}, "negative count: no zeroing");
    }
    {
        RowsFixture f; // Count wraps size to 8; entry loop exceeds storage.
        f.globals.value_11403cc = 0x2000'0001u;
        check(f.run() == Outcome::refused_cell_block_extent,
              "wrapped size refused");
    }
}

void test_rows_refusals() {
    {
        RowsFixture f; // Capacity reached: P = 0 refused at P+0x10.
        f.globals.global_fa9da0 = 1;
        f.records[0].field_130 = 2;
        f.table[1] = &f.rows[3];
        check(f.run() == Outcome::refused_null_row, "P = 0 refused");
        check(f.table[0] == &f.rows[0] && f.table[1] == nullptr,
              "T[1] = 0 kept before refusal");
        check(f.rows[0].field_14 == 0, "earlier row effects kept");
        check(f.ports.log.size() == 3, "no later call after P = 0");
    }
    {
        RowsFixture f; // Signed JL with negative used index.
        f.globals.global_fa9db8 = 0xFFFF'FFFFu;
        check(f.run() == Outcome::refused_row_index, "negative row refused");
        check(f.globals.global_fa9db8 == 0, "FA9DB8 incremented first");
    }
    {
        RowsFixture f;
        f.globals.rows_fa9dc0 = {};
        check(f.run() == Outcome::refused_row_storage, "null rows refused");
    }
    {
        RowsFixture f;
        f.globals.global_fa9da0 = 9;
        f.globals.global_fa9db8 = 4;
        check(f.run() == Outcome::refused_row_index, "row beyond storage");
    }
    {
        RowsFixture f;
        f.records[0].table_134 = {};
        check(f.run() == Outcome::refused_cell_table, "null table refused");
        check(f.rows[0].text_1c[0] == std::byte{0}, "name before table write");
    }
    {
        RowsFixture f;
        f.records[0].field_130 = 5;
        f.globals.value_11403cc = 0; // Empty blocks keep the pool in range.
        check(f.run() == Outcome::refused_cell_table, "table capacity refused");
        check(f.table[3] == &f.rows[3], "first four entries written");
    }
    {
        RowsFixture f;
        f.globals.count_11403c4 = 2;
        check(f.run() == Outcome::refused_record_array_extent,
              "record array extent refused");
        check(f.table[0] == &f.rows[0], "first record processed");
    }
    {
        RowsFixture f; // Negative declared count and n <= 0: skipped.
        f.globals.count_11403c4 = 0x8000'0000u;
        check(f.run() == Outcome::completed && f.table[0] == nullptr,
              "negative 11403C4 skips");
        RowsFixture g;
        g.records[0].field_130 = 0xFFFF'FFFFu;
        check(g.run() == Outcome::completed && g.table[0] == nullptr,
              "negative +0x130 skips");
    }
}

void test_chain() {
    {
        Fixture f;
        f.all_present();
        f.ports.on_97fbd0 = [](FrameBuffer b) { b[0] = std::byte{0x5A}; };
        check(f.run() == Outcome::completed, "chain completes");
        check(f.fields.field_48 == 1, "+0x48 = 1 on full chain");
        const std::vector<std::string> order{
            "97fbd0", "97f860", "9ec464", "b8", "c4", "c8", "c0", "d8", "e0"};
        check(std::vector<std::string>(f.ports.log.begin() + 3,
                                       f.ports.log.end()) == order,
              "chain order");
        check(f.ports.seen_b8 == 0x1234 && f.ports.seen_c8 == 6 &&
                  f.ports.seen_c0 == 0x3391,
              "chain arguments");
        check(f.ports.buffer_fbd0 == f.ports.buffer_f860 &&
                  f.ports.first_f860 == std::byte{0x5A},
              "same frame buffer for 97FBD0 and 97F860");
    }
    {
        Fixture f;
        f.all_present();
        f.globals.present_10e72c8 = f.globals.present_10e72c0 = false;
        f.run();
        check(f.fields.field_48 == 1, "C8/C0 absent do not interrupt");
    }
    {
        Fixture f;
        f.all_present();
        f.fields.field_48 = 1;
        f.ports.ret_97f860 = 0x100;
        f.run();
        check(f.fields.field_48 == 0 && f.ports.log.size() == 5,
              "97F860 nonzero: +0x48 = 0, chain skipped");
    }
    struct Case {
        const char* name;
        std::function<void(Fixture&)> setup;
        std::size_t calls;
    };
    const Case cases[] = {
        {"B8 absent", [](Fixture& f) { f.globals.present_10e72b8 = false; }, 6},
        {"B8 nonzero", [](Fixture& f) { f.ports.ret_b8 = 1; }, 7},
        {"C4 absent", [](Fixture& f) { f.globals.present_10e72c4 = false; }, 7},
        {"C4 nonzero", [](Fixture& f) { f.ports.ret_c4 = 0x8000'0000u; }, 8},
        {"D8 absent", [](Fixture& f) { f.globals.present_10e72d8 = false; },
         10},
        {"D8 nonzero", [](Fixture& f) { f.ports.ret_d8 = 2; }, 11},
        {"E0 absent", [](Fixture& f) { f.globals.present_10e72e0 = false; },
         11},
        {"E0 nonzero", [](Fixture& f) { f.ports.ret_e0 = 3; }, 12},
        {"B8 clears C4 live",
         [](Fixture& f) {
             f.ports.on_b8 = [&f] { f.globals.present_10e72c4 = false; };
         },
         7},
    };
    for (const Case& c : cases) {
        Fixture f;
        f.all_present();
        c.setup(f);
        f.run();
        const std::string a = std::string(c.name) + ": +0x48 = 0";
        const std::string b = std::string(c.name) + ": call count";
        check(f.fields.field_48 == 0, a.c_str());
        check(f.ports.log.size() == c.calls, b.c_str());
    }
}

void test_selection() {
    {
        Fixture f;
        auto d = std::make_shared<NativeInputDescription>();
        d->records.push_back(record_of(1));
        f.add_description(1, d, {1, 1});
        f.lookup.result = 0;
        check(f.run() == Outcome::completed, "selection completes");
        check(f.fields.selection_58.description == d, "+0x58 description");
        check(f.fields.selection_58.commands[6] == d->records[0],
              "same record shared with the registry");
        check(f.ports.log.back() == "lookup:BUTTON_1", "lookups last");
    }
    {
        Fixture f;
        auto d = std::make_shared<NativeInputDescription>();
        f.add_description(1, d, {0, 0});
        f.lookup.result = 3;
        check(f.run() == Outcome::refused_selection_record_index,
              "helper index refusal propagated");
    }
    {
        Fixture f; // 97FBD0 breaks the registry views before 0x980550.
        f.ports.on_97fbd0 = [&f](FrameBuffer) {
            f.globals.registry_fa9d30.ids.push_back(1u);
        };
        check(f.run() == Outcome::refused_selection_source,
              "helper source refusal propagated");
        check(f.fields.field_48 == 0, "+0x48 cleared before refusal");
    }
}

void test_owned_pool_and_multiple_records() {
    {
        RowsFixture f;
        f.globals.value_11403cc = 0;
        f.globals.pool.storage = {};
        f.globals.pool.cursor_1699134 = {true, 0};
        check(f.run() == Outcome::refused_cell_block_address,
              "claimed owned cursor without backing is refused");
        check(f.globals.pool.used_169913c == 0x10 &&
                  f.globals.pool.cursor_1699134.offset == 0,
              "zero-size counters retain native update order on refusal");
        check(f.table[0] == &f.rows[0] && f.rows[0].field_14 == 0x77,
              "row selection persists, later row clear is not performed");
    }
    {
        RowsFixture f;
        f.globals.value_11403cc = 0;
        f.globals.pool.cursor_1699134 = {true, 0};
        f.globals.pool.storage = std::span{f.storage}.first(0);
        check(f.run() == Outcome::completed,
              "zero-length owned view with nonnull backing is valid");
        check(f.rows[0].field_18.data == f.storage.data() &&
                  f.rows[0].field_18.bytes == 0 && f.rows[0].field_14 == 0,
              "present empty block stays distinct from null block");
        check(f.storage[0] == std::byte{0xCC}, "empty view is never accessed");
    }
    {
        RowsFixture f;
        std::array<CellRow*, 2> second_table{};
        std::array<Record11403D0, 3> records{};
        records[0] = {1, f.table};
        records[1] = {2, second_table};
        records[2] = {0xFFFF'FFFFu, {}};
        f.globals.records_11403d0 = records;
        f.globals.count_11403c4 = 3;
        f.globals.value_11403cc = 1;
        f.all_present();
        check(f.run() == Outcome::completed, "three-record row pass completes");
        check(f.table[0] == &f.rows[0] && second_table[0] == &f.rows[1] &&
                  second_table[1] == &f.rows[2],
              "table index restarts for each record, pool index does not");
        check(f.globals.global_fa9db8 == 3 &&
                  f.globals.pool.cursor_1699134.offset == 32 &&
                  f.globals.pool.used_169913c == 0x28,
              "both pools span all positive records");
        check(f.ports.seen_c4 == 0 && f.fields.field_48 == 1,
              "row counter reaches zero and C4 output is not a status");
    }
}

void test_live_optional_chain_and_null_allocation() {
    {
        Fixture f;
        f.all_present();
        f.globals.present_10e72c0 = false;
        f.ports.ret_9ec464 = OpaqueWord{0x1'0000'1234ull};
        f.ports.on_c8 = [&] { f.globals.present_10e72c0 = true; };
        f.ports.on_c0 = [&] { f.globals.present_10e72d8 = false; };
        check(f.run() == Outcome::completed, "live optional changes complete");
        check(f.ports.seen_b8 == OpaqueWord{0x1'0000'1234ull},
              "opaque callback value retains host width");
        check(f.ports.seen_c0 == 0x3391 && f.ports.log.back() == "c0" &&
                  f.fields.field_48 == 0,
              "C8 enables C0 live, C0 disables the later D8 call");
    }
    {
        Fixture f;
        f.allocator.return_null = true;
        check(f.run() == Outcome::completed, "allocation return is not a gate");
        check(f.globals.global_fa9da8.data == nullptr &&
                  f.ports.log[2] == "433bc0" && f.ports.log.back() == "9ec464",
              "null allocation is stored and downstream calls are kept");
    }
}

} // namespace

int main() {
    test_prologue();
    test_description_ranks();
    test_description_counts();
    test_rows_block();
    test_rows_pool_states();
    test_rows_refusals();
    test_chain();
    test_selection();
    test_owned_pool_and_multiple_records();
    test_live_optional_chain_and_null_allocation();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

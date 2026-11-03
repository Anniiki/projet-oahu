// Behavioural checks of the mode-probe core against a fake target memory.
#include "runtime_mode_probe_core.hpp"

#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
namespace probe = tdur::diagnostics::mode_probe;
using probe::Guard;
using probe::Reason;
using probe::Sample;
using probe::Space;
using probe::Status;

int failures = 0;
void check(bool condition, const char* what, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << what << '\n';
    }
}
#define CHECK(x) check((x), #x, __LINE__)

struct Access {
    Space space;
    std::uint32_t address;
    std::size_t length;
};

// Sparse byte memory; unmapped bytes fail the whole read. A hook may mutate
// memory after a given read to simulate the game writing between reads.
class FakeReader final : public probe::Reader {
  public:
    std::map<std::uint32_t, std::byte> memory;
    std::vector<Access> log;
    std::function<void(std::size_t)> after_read;

    void put32(std::uint32_t address, std::uint32_t value) {
        for (std::uint32_t i = 0; i < 4; ++i)
            memory[address + i] = static_cast<std::byte>(value >> (8 * i));
    }
    void put8(std::uint32_t address, unsigned value) {
        memory[address] = static_cast<std::byte>(value);
    }
    bool read(Space space, std::uint32_t address,
              std::span<std::byte> out) override {
        log.push_back({space, address, out.size()});
        bool ok = true;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto found =
                memory.find(address + static_cast<std::uint32_t>(i));
            if (found == memory.end()) {
                ok = false;
                break;
            }
            out[i] = found->second;
        }
        if (after_read)
            after_read(log.size());
        return ok;
    }
    std::size_t bytes() const {
        std::size_t total = 0;
        for (const auto& a : log)
            total += a.length;
        return total;
    }
};

constexpr std::uint32_t object = 0x02000000;
const probe::Layout nominal{0x010E7238, 0x00F0B138};

FakeReader healthy(std::uint32_t at = object) {
    FakeReader r;
    r.put32(nominal.global, at);
    r.put32(at, nominal.vptr);
    r.put32(at + 0x100, 0x18);
    r.put32(at + 0x104, 0xFFFFFFFFU);
    r.put8(at + 0x108, 0);
    // Neighbour bytes the probe must ignore.
    r.put8(at + 0x109, 0xAA);
    r.put32(at + 0x0FC, 0x11111111);
    return r;
}

void test_layout() {
    const auto at_preferred = probe::make_layout(0x400000, 0x1000000, 0x400000);
    CHECK(at_preferred && at_preferred->global == 0x010E7238 &&
          at_preferred->vptr == 0x00F0B138);
    const auto moved = probe::make_layout(0x500000, 0x1000000, 0x400000);
    CHECK(moved && moved->global == 0x011E7238 && moved->vptr == 0x0100B138);
    CHECK(!probe::make_layout(0x400000, 0x1000000, 0x10000));
    CHECK(!probe::make_layout(0, 0x1000000, 0x400000));
    // Global rva is 0xCE7238: the image must hold its 4 bytes.
    CHECK(!probe::make_layout(0x400000, 0xCE723B, 0x400000));
    CHECK(probe::make_layout(0x400000, 0xCE723C, 0x400000).has_value());
    CHECK(!probe::make_layout(0xFFFF0000, 0x1000000, 0x400000));
}

void test_nominal_and_budget() {
    auto r = healthy();
    const auto s = probe::take_sample(r, nominal);
    CHECK(s.status == Status::available && s.mode == 0x18 && s.old_mode == -1 &&
          s.guard == Guard::zero);
    CHECK(r.log.size() == 6 && r.bytes() == 34);
    const std::vector<Access> expected{
        {Space::image, nominal.global, 4},  {Space::object, object, 4},
        {Space::object, object + 0x100, 9}, {Space::image, nominal.global, 4},
        {Space::object, object, 4},         {Space::object, object + 0x100, 9}};
    bool same = r.log.size() == expected.size();
    for (std::size_t i = 0; same && i < expected.size(); ++i)
        same = r.log[i].space == expected[i].space &&
               r.log[i].address == expected[i].address &&
               r.log[i].length == expected[i].length;
    CHECK(same);
}

void test_guard_is_a_byte() {
    auto one = healthy();
    one.put8(object + 0x108, 1);
    one.put8(object + 0x109, 0xFF);
    one.put8(object + 0x10A, 0xFF);
    one.put8(object + 0x10B, 0xFF);
    CHECK(probe::take_sample(one, nominal).guard == Guard::one);
    auto other = healthy();
    other.put8(object + 0x108, 7);
    CHECK(probe::take_sample(other, nominal).guard == Guard::other);
    auto high = healthy();
    high.put8(object + 0x108, 0x80);
    CHECK(probe::take_sample(high, nominal).guard == Guard::other);
}

void test_mode_values() {
    auto r = healthy();
    r.put32(object + 0x100, 0x80000001U);
    r.put32(object + 0x104, 0x12);
    const auto s = probe::take_sample(r, nominal);
    CHECK(s.mode == -2147483647 && s.old_mode == 0x12);
}

void test_pointer_refusals() {
    auto null = healthy(0);
    auto s = probe::take_sample(null, nominal);
    CHECK(s.status == Status::unavailable && s.reason == Reason::null_pointer);
    CHECK(null.log.size() == 1);
    auto odd = healthy(object + 2);
    s = probe::take_sample(odd, nominal);
    CHECK(s.reason == Reason::misaligned && odd.log.size() == 1);
    // Last field byte is object + 0x108: highest aligned object 0xFFFFFEF4.
    auto edge = healthy(0xFFFFFEF4);
    s = probe::take_sample(edge, nominal);
    CHECK(s.status == Status::available && edge.log.size() == 6);
    auto past = healthy(0xFFFFFEF8);
    s = probe::take_sample(past, nominal);
    CHECK(s.reason == Reason::overflow && past.log.size() == 1);
}

void test_type_refusal() {
    for (const std::uint32_t wrong : {0x00F03590U, 0x00F0B13CU, 0U}) {
        auto r = healthy();
        r.put32(object, wrong);
        const auto s = probe::take_sample(r, nominal);
        CHECK(s.status == Status::unavailable &&
              s.reason == Reason::unexpected_type);
        CHECK(r.log.size() == 2 && r.bytes() == 8);
    }
}

void test_memory_refusals() {
    auto no_global = healthy();
    no_global.memory.erase(nominal.global + 3);
    CHECK(probe::take_sample(no_global, nominal).reason == Reason::memory);
    auto no_vptr = healthy();
    no_vptr.memory.erase(object + 1);
    CHECK(probe::take_sample(no_vptr, nominal).reason == Reason::memory);
    CHECK(no_vptr.log.size() == 2);
    auto no_guard = healthy();
    no_guard.memory.erase(object + 0x108);
    CHECK(probe::take_sample(no_guard, nominal).reason == Reason::memory);
    CHECK(no_guard.log.size() == 3);
    // Object freed between the passes.
    auto freed = healthy();
    freed.after_read = [&freed](std::size_t n) {
        if (n == 4)
            freed.memory.erase(object + 0x100);
    };
    CHECK(probe::take_sample(freed, nominal).reason == Reason::memory);
}

void test_instability() {
    struct Case {
        std::size_t after;
        std::uint32_t address;
        bool byte;
        bool unstable;
    };
    const Case cases[] = {
        {3, nominal.global, false, true}, {3, object, false, true},
        {3, object + 0x100, false, true}, {3, object + 0x104, false, true},
        {3, object + 0x108, true, true},  {3, object + 0x109, true, false},
        {6, object + 0x100, false, false}};
    for (const auto& c : cases) {
        auto r = healthy();
        r.after_read = [&r, c](std::size_t n) {
            if (n != c.after)
                return;
            if (c.byte)
                r.put8(c.address, 1);
            else
                r.put32(c.address, 0x0000BEEC);
        };
        const auto s = probe::take_sample(r, nominal);
        CHECK((s.status == Status::unstable) == c.unstable);
        if (!c.unstable)
            CHECK(s.status == Status::available);
    }
    // A change between the two field reads is rejected...
    auto changed = healthy();
    changed.after_read = [&changed](std::size_t n) {
        if (n == 1)
            changed.put32(object + 0x100, 0x13);
        if (n == 3)
            changed.put32(object + 0x100, 0x18);
    };
    CHECK(probe::take_sample(changed, nominal).status == Status::unstable);
    // ...but one made and undone between them is not seen: this is double-read
    // stability, not an atomic snapshot.
    auto reverted = healthy();
    reverted.after_read = [&reverted](std::size_t n) {
        if (n == 3)
            reverted.put32(object + 0x100, 0x13);
        if (n == 5)
            reverted.put32(object + 0x100, 0x18);
    };
    const auto unseen = probe::take_sample(reverted, nominal);
    CHECK(unseen.status == Status::available && unseen.mode == 0x18);
}

void test_changed_identity_stops_reads() {
    // A changed G must stop before following the old object again. A changed
    // class must stop before reading fields that no longer have that type.
    for (const auto change_global : {false, true}) {
        auto r = healthy();
        r.after_read = [&r, change_global](std::size_t n) {
            if (n == 3)
                r.put32(change_global ? nominal.global : object, 0x00F03590);
        };
        const auto sample = probe::take_sample(r, nominal);
        CHECK(sample.status == Status::unstable);
        CHECK(r.log.size() == (change_global ? 4U : 5U));
        std::size_t field_reads = 0;
        for (const auto& access : r.log)
            field_reads += access.address == object + 0x100;
        CHECK(field_reads == 1);
    }
}

Sample available(std::int32_t mode, std::int32_t old_mode,
                 Guard guard = Guard::zero) {
    return Sample{Status::available, Reason::none, mode, old_mode, guard};
}

void test_journal() {
    probe::Journal j;
    j.record(0, available(0x18, -1));
    j.record(50, available(0x18, -1));
    j.record(100, available(0x18, -1, Guard::one));
    j.record(150, Sample{Status::unavailable, Reason::null_pointer});
    j.record(200, Sample{Status::unavailable, Reason::null_pointer});
    j.record(250, Sample{Status::unavailable, Reason::memory});
    j.record(300, Sample{Status::unstable, Reason::none});
    j.record(350, available(0x12, 0x18));
    j.record(400, available(0x12, 0x13));
    const auto& c = j.counters();
    CHECK(c.samples == 9 && c.available == 5 && c.unstable == 1);
    CHECK(c.null_pointer == 2 && c.memory == 1 && c.misaligned == 0);
    CHECK(c.changes == 7 && c.logged == 7 && c.dropped == 0);
    CHECK(c.previous_changed_mode_same == 1);
    CHECK(j.changes().size() == 7 && j.changes()[1].index == 2 &&
          j.changes()[1].elapsed_ms == 100);
    CHECK(probe::render_change(j.changes()[0]) ==
          "{\"t_ms\":0,\"sample\":0,\"status\":\"available\",\"mode\":24,"
          "\"old_mode\":-1,\"guard\":\"zero\"}");
    CHECK(probe::render_change(j.changes()[2]) ==
          "{\"t_ms\":150,\"sample\":3,\"status\":\"unavailable\","
          "\"reason\":\"null_pointer\"}");
}

void test_journal_cap() {
    probe::Journal j;
    for (std::int32_t i = 0; i < 100; ++i)
        j.record(static_cast<std::uint64_t>(i), available(i % 2, 0));
    j.record(100, available(1, 0));
    const auto& c = j.counters();
    CHECK(c.samples == 101 && c.changes == 100);
    CHECK(c.logged == 64 && c.dropped == 36 && j.changes().size() == 64);
    CHECK(j.changes().back().index == 63);
}

void test_no_pointer_in_output() {
    auto r = healthy(0x02A0B0C0);
    probe::Journal j;
    j.record(0, probe::take_sample(r, nominal));
    const auto text = probe::render_change(j.changes()[0]) +
                      probe::render_counters(j.counters());
    for (const char* forbidden :
         {"2A0B0C0", "2a0b0c0", "44085440", "F0B138", "f0b138", "15774008"})
        CHECK(text.find(forbidden) == std::string::npos);
}

std::expected<probe::Options, std::string>
parse(std::vector<std::wstring_view> args) {
    return probe::parse_options(args);
}

void test_options() {
    const std::vector<std::wstring_view> good{L"--pid",
                                              L"4242",
                                              L"--creation-filetime",
                                              L"133000000000000000",
                                              L"--exe",
                                              L"C:\\g\\TestDriveUnlimited.exe",
                                              L"--preferred-base",
                                              L"0x400000",
                                              L"--out",
                                              L"C:\\o\\new.json",
                                              L"--duration-s",
                                              L"120"};
    const auto o = parse(good);
    CHECK(o && o->pid == 4242 && o->creation == 133000000000000000ULL &&
          o->preferred == 0x400000 && o->duration_s == 120 &&
          o->interval_ms == 50);
    auto with = [&good](std::size_t at, std::wstring_view value) {
        auto copy = good;
        copy[at] = value;
        return parse(copy);
    };
    CHECK(!with(11, L"121") && !with(11, L"0") && with(11, L"1"));
    CHECK(!with(7, L"0x10000") && with(7, L"4194304"));
    CHECK(!with(1, L"0") && !with(1, L"4294967296") && !with(1, L"12a"));
    CHECK(!with(3, L"0") && !with(3, L"99999999999999999999999"));
    CHECK(!with(0, L"--address"));
    auto interval = [&good](std::wstring_view value) {
        auto copy = good;
        copy.push_back(L"--interval-ms");
        copy.push_back(value);
        return parse(copy);
    };
    CHECK(!interval(L"24") && interval(L"25") && !interval(L"1001"));
    CHECK(interval(L"25") && interval(L"25")->interval_ms == 25);
    auto missing = good;
    missing.resize(10);
    CHECK(!parse(missing));
    auto duplicate = good;
    duplicate.push_back(L"--pid");
    duplicate.push_back(L"4242");
    CHECK(!parse(duplicate));
    auto odd = good;
    odd.push_back(L"--interval-ms");
    CHECK(!parse(odd));
}

} // namespace

int main() {
    test_layout();
    test_nominal_and_budget();
    test_guard_is_a_byte();
    test_mode_values();
    test_pointer_refusals();
    test_type_refusal();
    test_memory_refusals();
    test_instability();
    test_changed_identity_stops_reads();
    test_journal();
    test_journal_cap();
    test_no_pointer_in_output();
    test_options();
    if (failures)
        std::cerr << failures << " failure(s)\n";
    else
        std::cout << "all mode-probe core checks passed\n";
    return failures ? 1 : 0;
}

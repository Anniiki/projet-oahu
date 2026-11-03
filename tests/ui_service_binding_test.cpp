#include "tdur/diagnostics/ui_service_binding.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace tdur::diagnostics;
using Status = UiServiceBindingStatus;
using Field = UiServiceBindingField;
constexpr UiServiceExpectedProcess expected{71, 123456};
constexpr std::uint32_t host_cell = 0x114c15c, catalog_cell = 0x11f2cdc;
unsigned checks{}, failures{};
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << label << '\n';
    }
}
struct Reader final : UiServiceBindingReader {
    UiServiceBindingIdentity id{71,         0x400000, 123456, 0x12ad000,
                                0x7fffffff, 1000,     true,   true};
    std::map<std::uint32_t, std::uint32_t> words{
        {host_cell, 0x2000000}, {0x2000000, 0xf20000},
        {0xf2001c, 0x600010},   {0xf20030, 0x600020},
        {0xf2004c, 0x600030},   {catalog_cell, 0x3000000},
        {0x300008c, 1},         {0x3000088, 0x4000000},
        {0x3000090, 0x5000003}, {0x30002a0, 2},
        {0x300029c, 0x4000010}, {0x30002a4, 0x500000b},
    };
    std::map<std::uint32_t, UiServiceBindingPage> overrides;
    std::vector<std::uint32_t> reads, queries;
    std::optional<std::uint32_t> fail_address;
    std::optional<unsigned> fail_read, changed_identity, failed_clock;
    std::size_t transfer_bytes{};
    bool transfer_success{}, mutate_second{};
    unsigned identities{}, clocks{};
    std::array<std::uint64_t, 4> ticks{10, 11, 12, 13};
    UiServiceBindingIdentity identity() noexcept override {
        auto value = id;
        if (changed_identity && identities == *changed_identity)
            ++value.creation_filetime;
        ++identities;
        return value;
    }
    bool timestamp(std::uint64_t& tick) noexcept override {
        const auto index = clocks++;
        if (index >= ticks.size() || (failed_clock && index == *failed_clock))
            return false;
        tick = ticks[index];
        return true;
    }
    bool query(std::uint32_t address,
               UiServiceBindingPage& page) noexcept override {
        queries.push_back(address);
        if (const auto found = overrides.find(address);
            found != overrides.end()) {
            page = found->second;
            return true;
        }
        page = {};
        page.begin = address & ~std::uint64_t{0xfff};
        page.size = 0x1000;
        page.committed = page.readable = true;
        if (address >= 0x400000 && address < 0x16ad000) {
            page.type = UiServiceMemoryType::image;
            page.allocation_base = 0x400000;
            page.executable = address >= 0x600000 && address < 0x601000;
            page.writable = !page.executable &&
                            !(address >= 0xf20000 && address < 0xf21000);
            return true;
        }
        for (const auto base : {0x2000000U, 0x3000000U, 0x4000000U, 0x5000000U})
            if (address >= base && address < base + 0x1000U) {
                page.type = UiServiceMemoryType::private_memory;
                page.allocation_base = base;
                page.writable = true;
                return true;
            }
        return false;
    }
    UiServiceBindingTransfer
    read(std::uint32_t address, std::span<std::byte> bytes) noexcept override {
        reads.push_back(address);
        if (mutate_second && reads.size() == 13) {
            words[host_cell] = 0x2000040;
            words[0x2000040] = 0xf20000;
        }
        if ((fail_address && address == *fail_address) ||
            (fail_read && reads.size() == *fail_read))
            return {transfer_success, transfer_bytes};
        const auto found = words.find(address);
        if (found == words.end() || bytes.size() != 4)
            return {};
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] =
                static_cast<std::byte>((found->second >> (8 * i)) & 0xffU);
        return {true, 4};
    }
};
auto collect(Reader& reader) {
    const auto result = collect_ui_service_binding(reader, expected);
    check(result.requested_bytes <= 96 &&
              result.received_bytes <= result.requested_bytes,
          "bounded metadata accounting on every path");
    check(result.requested_bytes == reader.reads.size() * 4,
          "every requested word includes failed transfers");
    check(std::all_of(reader.reads.begin(), reader.reads.end(),
                      [](auto address) {
                          return address != 0x600010 && address != 0x600020 &&
                                 address != 0x600030 &&
                                 !(address >= 0x4000000 && address < 0x6000000);
                      }),
          "queries never turn into method/record/string payload reads");
    return result;
}
void complete_and_guards() {
    Reader r;
    const auto result = collect(r);
    const std::vector<std::uint32_t> pass{
        host_cell, 0x2000000, 0xf2001c,  0xf20030,  0xf2004c,  catalog_cell,
        0x300008c, 0x3000088, 0x3000090, 0x30002a0, 0x300029c, 0x30002a4};
    auto wanted = pass;
    wanted.insert(wanted.end(), pass.begin(), pass.end());
    check(result.status == Status::candidate_concordant &&
              result.requested_bytes == 96 && r.reads == wanted,
          "exact two-pass order, guarded descriptor metadata and query-only "
          "targets");
    check(result.passes[0].values[8] == 0x5000003U,
          "unaligned char pool is queried without DWORD read");
    for (auto count : {0U, 0xffffffffU, 0x80000000U}) {
        Reader empty;
        empty.words[0x300008c] = empty.words[0x30002a0] = count;
        const auto got = collect(empty);
        check(got.status == Status::candidate_concordant &&
                  got.requested_bytes == 64 && !got.passes[0].values[7] &&
                  !got.passes[0].values[8] && !got.passes[0].values[10],
              "both nonpositive signed counts skip pointer fields");
    }
    Reader one;
    one.words[0x300008c] = 0;
    check(collect(one).requested_bytes == 80,
          "one empty descriptor saves eight bytes per pass");
    Reader host;
    host.words[host_cell] = 0;
    const auto missing = collect(host);
    check(missing.status == Status::partial && missing.requested_bytes == 64 &&
              missing.passes[0].host.status == Status::absent &&
              missing.passes[0].descriptors[1].status == Status::pass_complete,
          "absent host does not suppress catalog or become concordant success");
    Reader catalog;
    catalog.words[catalog_cell] = 0;
    const auto no_catalog = collect(catalog);
    check(no_catalog.status == Status::partial &&
              no_catalog.requested_bytes == 48 &&
              no_catalog.passes[0].host.status == Status::pass_complete &&
              no_catalog.passes[0].descriptors[0].status ==
                  Status::not_attempted,
          "absent catalog preserves host and unattempted descriptors");
    Reader descriptor;
    descriptor.words.erase(0x300008c);
    const auto broken = collect(descriptor);
    check(
        broken.passes[0].descriptors[0].status == Status::unreadable &&
            broken.passes[0].descriptors[1].status == Status::pass_complete,
        "one unreadable descriptor leaves other fixed descriptor independent");
}
void failures_and_changes() {
    for (unsigned call = 1; call <= 24; ++call) {
        Reader r;
        r.fail_read = call;
        r.transfer_bytes = 2;
        const auto got = collect(r);
        check(got.status != Status::candidate_concordant,
              "partial transfer never becomes a successful snapshot");
        check(got.received_bytes + 2 == got.requested_bytes,
              "partial transfer bytes are retained on failure");
    }
    for (std::size_t received : {0U, 1U, 3U, 4U, 5U}) {
        Reader r;
        r.fail_address = host_cell;
        r.transfer_bytes = received;
        const auto got = collect(r);
        check(got.passes[0].host.status == (received > 4
                                                ? Status::invalid_transfer
                                                : Status::unreadable) &&
                  got.passes[0].descriptors[1].status == Status::pass_complete,
              "failed/overreported host transfers keep independent catalog "
              "statuses");
    }
    Reader r;
    r.mutate_second = true;
    check(collect(r).status == Status::changed,
          "changed root between passes is explicit");
    for (unsigned at = 0; at < 5; ++at) {
        Reader changed;
        changed.changed_identity = at;
        check(collect(changed).status == Status::identity_refused,
              "identity checked initially and on both sides of each pass");
    }
    for (unsigned at = 0; at < 4; ++at) {
        Reader clock;
        clock.failed_clock = at;
        check(collect(clock).status == Status::timestamp_refused,
              "all QPC failures stop observation");
    }
    Reader reverse;
    reverse.ticks[2] = 0;
    check(collect(reverse).requested_bytes == 48,
          "backward second begin stops before another payload");
    Reader slow;
    slow.ticks[1] = 1011;
    check(collect(slow).status == Status::timestamp_refused,
          "pass exceeding one QPC second is refused");
    Reader wrong;
    wrong.id.official_sha_verified = false;
    check(collect(wrong).requested_bytes == 0,
          "unverified official SHA permits no metadata reads");
    Reader overflow;
    overflow.id.maximum_user_address = 0xffffffff;
    overflow.words[catalog_cell] = 0xfffffff0;
    const auto refused = collect(overflow);
    check(
        refused.passes[0].descriptors[0].status == Status::address_refused &&
            refused.passes[0].descriptors[1].status == Status::address_refused,
        "root plus descriptor offsets never silently wrap to unrelated memory");
    Reader pages;
    UiServiceBindingPage page{};
    pages.query(0xf2001c, page);
    page.writable = true;
    pages.overrides[0xf2001c] = page;
    check(
        collect(pages).passes[0].host.status == Status::page_refused,
        "writable vtable entry is not accepted as established readonly table");
    Reader guard;
    guard.query(0x2000000, page);
    page.guard = true;
    guard.overrides[0x2000000] = page;
    check(collect(guard).passes[0].host.status == Status::page_refused,
          "guarded object is not read");
    Reader code;
    code.query(0x600010, page);
    page.executable = false;
    code.overrides[0x600010] = page;
    check(collect(code).passes[0].host.status == Status::page_refused,
          "nonexecutable target is refused without code read");
    Reader malformed;
    malformed.query(0x2000000, page);
    page.size = 0xffffffffffffffffULL;
    malformed.overrides[0x2000000] = page;
    check(collect(malformed).passes[0].host.status == Status::page_refused,
          "overflowing page extent is rejected");
}
} // namespace
int main() {
    complete_and_guards();
    failures_and_changes();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

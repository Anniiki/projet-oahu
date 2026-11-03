#include "tdur/ui/vmf_catalog.hpp"

#include "tdur/assets/bank_archive.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace allocations {
thread_local bool enabled{};
thread_local std::size_t calls{},
    fail_at = std::numeric_limits<std::size_t>::max();
} // namespace allocations
void* operator new(std::size_t size) {
    if (allocations::enabled && allocations::calls++ == allocations::fail_at)
        throw std::bad_alloc();
    if (void* value = std::malloc(size ? size : 1))
        return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::ui;
namespace assets = tdur::assets;
using Code = VmfCatalogErrorCode;
std::size_t checks{}, failures{};
void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << text << '\n';
    }
}
template <class T, class E>
T take(std::expected<T, E> result, const char* text) {
    check(result.has_value(), text);
    if (!result)
        throw std::runtime_error("required catalog fixture failed");
    return std::move(*result);
}
std::span<const std::byte> bytes(std::string_view value) {
    return std::as_bytes(std::span{value.data(), value.size()});
}
void word(std::vector<std::byte>& output, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        output.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xffU));
}
void record(std::vector<std::byte>& output, std::uint64_t id,
            std::uint32_t offset) {
    word(output, static_cast<std::uint32_t>(id));
    word(output, static_cast<std::uint32_t>(id >> 32));
    word(output, offset);
    word(output, 0xfeedface);
}
VmfCatalogTableView table(std::int32_t count,
                          const std::vector<std::byte>& rows,
                          std::string_view pool) {
    return {count, std::span<const std::byte>{rows},
            std::span<const char>{pool.data(), pool.size()}};
}
void raw_lookup() {
    for (const auto count : {0, -1, std::numeric_limits<std::int32_t>::min()}) {
        const auto found = find_vmf_catalog_text({count, {}, {}}, 0, 0);
        check(found && !*found, "nonpositive count needs neither records nor "
                                "pool even with zero budget");
    }
    auto found = find_vmf_catalog_text({1, {}, {}}, 1, 0);
    check(!found && found.error().code == Code::records_unbound,
          "positive count captures records before modern budget refusal");
    std::vector<std::byte> rows;
    word(rows, 7);
    found = find_vmf_catalog_text({1, std::span<const std::byte>{rows}, {}}, 8);
    check(found && !*found,
          "low mismatch on four-byte backing never needs high offset or pool");
    found = find_vmf_catalog_text({1, std::span<const std::byte>{rows}, {}}, 7);
    check(!found && found.error().code == Code::record_range &&
              found.error().byte_offset == 4,
          "matching low requires only next high word");
    word(rows, 0);
    found = find_vmf_catalog_text({1, std::span<const std::byte>{rows}, {}}, 7);
    check(!found && found.error().code == Code::pool_unbound,
          "matched ID requires pool binding before missing offset word");
    found = find_vmf_catalog_text(table(1, rows, "pool"), 7);
    check(!found && found.error().code == Code::record_range &&
              found.error().byte_offset == 8,
          "bound pool precedes matched offset read");
    word(rows, 2);
    found = find_vmf_catalog_text(table(1000, rows, "ABCDE"), 7);
    check(found && *found && (**found).row == 0 && (**found).pool_offset == 2 &&
              std::string_view{(**found).bytes.data(),
                               (**found).bytes.size()} == "CDE",
          "first match ignores opaque word and later records; nonterminated "
          "text is not inspected");
    auto before = rows;
    rows.clear();
    record(rows, 7, 5);
    found = find_vmf_catalog_text(table(1, rows, "ABCDE"), 7);
    check(found && *found && (**found).bytes.empty(),
          "offset at pool end gives present implied empty string");
    rows.clear();
    record(rows, 7, 6);
    found = find_vmf_catalog_text(table(1, rows, "ABCDE"), 7);
    check(!found && found.error().code == Code::pool_range &&
              found.error().byte_offset == 6,
          "offset beyond bound pool is error rather than missing ID");
    rows.clear();
    record(rows, 7, 0xffffffff);
    found = find_vmf_catalog_text({1, std::span<const std::byte>{rows}, {}}, 8);
    check(found && !*found,
          "unmatched ID does not validate unused pool or offset");
    rows.clear();
    record(rows, 0, 0);
    record(rows, 0xffff000000000007ULL, 4);
    record(rows, 0, 8);
    const std::string pool{"one\0two\0last", 12};
    const auto source_before = rows;
    found = find_vmf_catalog_text(table(3, rows, pool), 0);
    check(found && *found && (**found).row == 0 && (**found).pool_offset == 0,
          "ID zero is valid and first duplicate wins");
    found = find_vmf_catalog_text(table(3, rows, pool), 0xffff000000000007ULL);
    check(found && *found && (**found).row == 1 && (**found).pool_offset == 4,
          "both full ID halves participate without truncation");
    check(rows == source_before,
          "lookup preserves record bytes including opaque words");
    found =
        find_vmf_catalog_text(table(3, rows, pool), 0xffff000000000007ULL, 1);
    check(!found && found.error().code == Code::record_limit &&
              found.error().row == 1 && found.error().byte_offset == 16,
          "record budget refuses next row rather than reporting no-match");
    found =
        find_vmf_catalog_text(table(3, rows, pool), 0xffff000000000007ULL, 2);
    check(found && *found && (**found).row == 1,
          "larger budget admits actual later match");
    found = find_vmf_catalog_text(table(3, rows, pool), 0, 1);
    check(found && *found, "early match needs no remaining record budget");
    rows.resize(4);
    found = find_vmf_catalog_text({1, std::span<const std::byte>{rows}, {}},
                                  0x100000000ULL);
    check(!found && found.error().code == Code::record_range,
          "matching zero low still requires high before no-match");
    rows = before;
    found = find_vmf_catalog_text(table(2, rows, "ABCDE"), 8);
    check(!found && found.error().code == Code::record_range &&
              found.error().row == 1 && found.error().byte_offset == 16,
          "missing later low is refused only when visited");
}
void selectors() {
    std::vector<std::byte> rows;
    record(rows, 1, 0);
    const auto first = table(1, rows, "first");
    const auto second = table(1, rows, "second");
    const std::array bindings{VmfCatalogBinding{0x48, first},
                              VmfCatalogBinding{0x25c, second},
                              VmfCatalogBinding{0xfffffffc, second}};
    const auto registry = std::span<const VmfCatalogBinding>{bindings};
    for (const auto selector : {0U, 0x40000000U, 0x80000000U, 0xc0000000U}) {
        const auto found = select_vmf_catalog_text(registry, selector, 1);
        check(found && *found && (**found).bytes.data() == first.pool->data(),
              "all four selector aliases reach same wrapped descriptor in x64");
    }
    for (const auto selector :
         {7U, 0x40000007U, 0x80000007U, 0xc0000007U, 0xffffffffU}) {
        const auto found = select_vmf_catalog_text(registry, selector, 1);
        check(found && *found && (**found).bytes.data() == second.pool->data(),
              "wrapped selector offset is a descriptor identity, not an "
              "unchecked size_t index");
    }
    auto found = select_vmf_catalog_text({}, 0, 1);
    check(!found && found.error().code == Code::registry_unbound,
          "unknown registry differs from known empty registry");
    found = select_vmf_catalog_text(std::span<const VmfCatalogBinding>{}, 0, 1);
    check(!found && found.error().code == Code::descriptor_unbound &&
              found.error().descriptor_offset == 0x48,
          "missing descriptor is distinct from absent ID");
    const std::array duplicated{bindings[0], bindings[1], bindings[1]};
    found = select_vmf_catalog_text(
        std::span<const VmfCatalogBinding>{duplicated}, 0, 1);
    check(found && *found,
          "unselected duplicate descriptors do not affect selected table");
    found = select_vmf_catalog_text(
        std::span<const VmfCatalogBinding>{duplicated}, 7, 1);
    check(!found && found.error().code == Code::duplicate_descriptor,
          "ambiguous selected descriptor is modern binding error");
}
assets::DatabaseStrings catalog(std::string_view source) {
    return take(assets::DatabaseStrings::parse(bytes(source)),
                "parse owned synthetic catalog");
}
void owned_catalogs() {
    const auto strings = catalog(
        "{first} 1\n{ignored alias} 001\n{same} 2\n{same} 3\n{} 0\n{top} "
        "18446744073709551615\n{étiquette} 9223372036854775808\n");
    const auto other = catalog("{second table} 1\n");
    const std::array sources{VmfCatalogSource{0, &strings},
                             VmfCatalogSource{7, &other}};
    auto owned = take(VmfLocalizedTextCatalog::from_catalogs(sources),
                      "construct normalized owned catalogs");
    check(owned.table_count() == 2,
          "explicit selector bindings are not guessed from resource names");
    auto first = take(owned.lookup(0, 1), "owned first ID lookup");
    auto alias =
        take(owned.lookup(0x40000000, 1), "owned wrapped selector alias");
    check(
        first && alias && first->text() == "first" &&
            first->c_str() == alias->c_str(),
        "first numeric definition wins and repeated references share identity");
    const auto two = take(owned.lookup(0, 2), "owned second ID");
    const auto three = take(owned.lookup(0, 3), "owned third ID");
    check(two && three && two->text() == three->text() &&
              two->c_str() != three->c_str(),
          "different normalized IDs retain distinct backings even with equal "
          "text");
    const auto zero = take(owned.lookup(0, 0), "owned zero ID");
    check(zero && zero->text().empty() && zero->extent_bytes() == 1,
          "present empty zero-ID text remains distinct from missing ID");
    const auto top =
        take(owned.lookup(0, std::numeric_limits<std::uint64_t>::max()),
             "owned max uint64");
    const auto high =
        take(owned.lookup(0, 0x8000000000000000ULL), "owned high uint64");
    check(top && top->text() == "top" && high && high->text() == "étiquette",
          "full uint64 identity and UTF8 bytes retained");
    const auto selected =
        take(owned.lookup(0x40000007, 1), "other bound catalog");
    check(selected && selected->text() == "second table",
          "same ID resolves in the explicitly selected catalog");
    check(!take(owned.lookup(0, 999), "owned absent ID"),
          "known no-match stays successful absence");
    check(!owned.lookup(8, 1) && !owned.lookup(0, 1, 0),
          "unbound selector and exhausted budget remain errors");
    allocations::enabled = true;
    allocations::calls = 0;
    allocations::fail_at = 0;
    const auto no_alloc = owned.lookup(0, 1);
    const auto no_alloc_missing = owned.lookup(0, 999);
    const auto allocation_calls = allocations::calls;
    allocations::enabled = false;
    check(no_alloc && *no_alloc && no_alloc_missing && !*no_alloc_missing &&
              allocation_calls == 0,
          "existing and missing lookups allocate no ordinary heap");
    const auto survivor = *first;
    auto moved = std::move(owned);
    check(!owned.lookup(0, 1) && moved.table_count() == 2 &&
              survivor.text() == "first",
          "moved-from catalog is invalid while references survive moves");
    const auto detached = [&]() {
        const auto local_source = catalog("{detached} 1\n");
        const std::array local{VmfCatalogSource{0, &local_source}};
        auto owner = take(VmfLocalizedTextCatalog::from_catalogs(local),
                          "temporary owned catalog");
        return *take(owner.lookup(0, 1),
                     "retain text before owner destruction");
    }();
    check(detached.text() == "detached",
          "returned reference survives catalog and source destruction");
    const std::array aliases{sources[0], VmfCatalogSource{0x40000000, &other}};
    const auto duplicate = VmfLocalizedTextCatalog::from_catalogs(aliases);
    check(!duplicate && duplicate.error().code == Code::duplicate_descriptor,
          "factory rejects colliding wrapped descriptor identities");
    const std::array missing{VmfCatalogSource{0, nullptr}};
    check(!VmfLocalizedTextCatalog::from_catalogs(missing),
          "absent parsed source is not an empty catalog");
    for (const auto limits :
         {VmfCatalogLimits{0}, VmfCatalogLimits{32, 0},
          VmfCatalogLimits{32, 100, 0}, VmfCatalogLimits{32, 100, 100, 0}})
        check(!VmfLocalizedTextCatalog::from_catalogs(sources, limits),
              "table entry ID and text quotas are explicit");
    const auto duplicate_large =
        catalog(std::string("{ok} 1\n{") + std::string(4096, 'x') + "} 0001\n");
    const std::array small{VmfCatalogSource{0, &duplicate_large}};
    check(VmfLocalizedTextCatalog::from_catalogs(small, {1, 2, 16, 6})
              .has_value(),
          "oversized duplicate value is not copied or charged");
    for (const auto id : {"-1", "bad"})
        check(
            !assets::DatabaseStrings::parse(
                bytes(std::string("{bad} ") + id + "\n")),
            "asset decoder rejects nondecimal IDs before catalog construction");
    const auto invalid = catalog("{bad} 18446744073709551616\n");
    const std::array input{VmfCatalogSource{0, &invalid}};
    const auto overflow = VmfLocalizedTextCatalog::from_catalogs(input);
    check(!overflow && overflow.error().code == Code::invalid_identifier,
          "decimal ID overflow is refused by uint64 catalog conversion");
    allocations::calls = 0;
    allocations::fail_at = std::numeric_limits<std::size_t>::max();
    allocations::enabled = true;
    const auto baseline = VmfLocalizedTextCatalog::from_catalogs(sources);
    const auto total_allocations = allocations::calls;
    allocations::enabled = false;
    check(baseline.has_value() && total_allocations > 0,
          "measure constructor allocation failure points");
    for (std::size_t at = 0; at < total_allocations; ++at) {
        allocations::calls = 0;
        allocations::fail_at = at;
        allocations::enabled = true;
        const auto result = VmfLocalizedTextCatalog::from_catalogs(sources);
        allocations::enabled = false;
        check(!result && result.error().code == Code::allocation_failure,
              "every construction bad_alloc becomes an explicit error");
    }
}
void installation(const std::filesystem::path& root) {
    const auto bank = take(
        assets::BankArchive::read_file(root / "Euro/Bnk/DataBase/DB_FR.bnk"),
        "read-only localized bank");
    const auto entries = bank.entries();
    const auto member = std::ranges::find_if(entries, [](const auto& entry) {
        return entry.name == "TDU_Menus.fr";
    });
    check(member != entries.end(), "actual localized menu member exists");
    if (member == entries.end())
        return;
    const auto content = take(
        bank.stored_bytes(static_cast<std::size_t>(member - entries.begin())),
        "read localized member bytes");
    const auto strings = take(assets::DatabaseStrings::parse(content),
                              "parse actual installation text catalog");
    const std::array sources{VmfCatalogSource{7, &strings}};
    const auto owned = take(VmfLocalizedTextCatalog::from_catalogs(sources),
                            "normalize actual installation strings with "
                            "explicit synthetic selector7");
    std::map<std::uint64_t, std::string_view> first;
    for (const auto& entry : strings.entries()) {
        std::uint64_t id{};
        const auto parsed = std::from_chars(
            entry.reference_token.data(),
            entry.reference_token.data() + entry.reference_token.size(), id);
        if (parsed.ec != std::errc{} ||
            parsed.ptr !=
                entry.reference_token.data() + entry.reference_token.size())
            continue;
        first.try_emplace(id, std::string_view{entry.value.c_str()});
    }
    std::size_t checked{};
    for (const auto& [id, expected] : first) {
        const auto found =
            take(owned.lookup(7, id), "resolve actual installation ID");
        check(found && found->text() == expected,
              "first actual catalog ID returns its decoded text");
        if (++checked == 12)
            break;
    }
    check(checked == 12, "installation sample covers twelve actual IDs without "
                         "exporting strings");
}
} // namespace

int main(int argc, char** argv) {
    try {
        raw_lookup();
        selectors();
        owned_catalogs();
        if (argc == 3 && std::string_view{argv[1]} == "--installation")
            installation(std::filesystem::path{argv[2]});
        else if (argc != 1)
            throw std::runtime_error(
                "Usage: vmf_catalog_test [--installation ROOT]");
    } catch (const std::exception& failure) {
        allocations::enabled = false;
        std::cerr << failure.what() << '\n';
        ++failures;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

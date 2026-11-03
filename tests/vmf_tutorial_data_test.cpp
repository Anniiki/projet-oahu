#include "tdur/ui/vmf_help_selection.hpp"
#include "tdur/ui/vmf_tutorial_data.hpp"

#include "tdur/assets/bank_archive.hpp"
#include "tdur/game/database_resource_key.hpp"

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

namespace allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
thread_local std::size_t fail_at = std::numeric_limits<std::size_t>::max();
} // namespace allocation_fixture
void* operator new(std::size_t size) {
    if (allocation_fixture::enabled &&
        allocation_fixture::calls++ == allocation_fixture::fail_at)
        throw std::bad_alloc();
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {
using namespace tdur::ui;
namespace assets = tdur::assets;
std::size_t checks{}, failures{};
void check(bool condition, std::string_view detail) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << detail << '\n';
    }
}
template <class T, class E>
T take(std::expected<T, E> value, std::string_view detail) {
    check(value.has_value(), detail);
    if (!value)
        throw std::runtime_error("required fixture/operation failed");
    return std::move(*value);
}
std::span<const std::byte> bytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}
assets::DatabaseTable table(std::string_view rows, std::size_t count,
                            std::string_view first_type = "u") {
    const auto source = std::string("{TDU_Tutorials} 99\n{Text_ID_} ") +
                        std::string(first_type) +
                        "\n{TextIndex_} u\n{Libelle_} h\n{File_} u\n"
                        "{Time_cent} i\n// items: " +
                        std::to_string(count) + "\n" + std::string(rows);
    return take(assets::DatabaseTable::parse_plaintext(bytes(source)),
                "synthetic native tutorial schema");
}
assets::DatabaseStrings strings() {
    return take(assets::DatabaseStrings::parse(bytes(
                    "{tip_test} 1\n{wrong key alias} 001\n{index} 2\n"
                    "{none} 3\n{none} 4\n{media} 5\n{wrong duplicate} 1\n")),
                "owned catalog with duplicate IDs and equal text");
}
void synthetic() {
    const auto source = table("1;2;3;5;500;\n0001;2;4;5;-1;\n", 2);
    const auto catalog = strings();
    auto data = take(VmfTutorialDatabase::from_table(source, catalog),
                     "resolve first numeric ID and keep owned backing");
    check(data.rows().size() == 2 && data.rows()[0].key.text() == "tip_test" &&
              data.rows()[1].time_cent == -1,
          "four resolved fields and signed unscaled time");
    check(data.rows()[0].key.c_str() == data.rows()[1].key.c_str(),
          "leading-zero aliases share the SAME reference");
    check(data.rows()[0].text.text() == data.rows()[1].text.text() &&
              data.rows()[0].text.c_str() != data.rows()[1].text.c_str(),
          "different IDs with identical text have different identities");
    auto found = take(data.find_help("tip_test"), "first help lookup");
    check(found == std::size_t{0},
          "help uses the first matching key without filtering");
    found = take(data.find_tutorial("tip_test", 3), "tutorial sentinel lookup");
    check(found == std::size_t{1},
          "sentinel identity skips first matching row, not equal text");
    found =
        take(data.find_tutorial("tip_test", 4), "different sentinel lookup");
    check(found == std::size_t{0}, "first nonsentinel duplicate wins");
    found = take(data.find_tutorial("tip_test", 999), "known missing sentinel");
    check(found == std::size_t{0},
          "missing sentinel is NULL, not an invented none string");
    check(!take(data.find_help("TIP_TEST"), "case-sensitive lookup") &&
              !take(data.find_help("absent"), "absent routing key"),
          "native strcmp is case-sensitive and absence is explicit");
    const std::string embedded("tip_test\0suffix", 15);
    check(take(data.find_help(embedded), "C-string prefix") == std::size_t{0},
          "bounded prefix before NUL");
    auto first = take(data.text_by_id(1), "catalog text lookup");
    auto missing = take(data.text_by_id(999), "missing catalog lookup");
    check(first && first->text() == "tip_test" && !missing,
          "first-definition lookup and known absence");
    const auto surviving_reference = data.rows()[0].text;
    auto moved = std::move(data);
    check(data.rows().empty() && !data.find_help("tip_test") &&
              !data.find_tutorial("tip_test", 3) && !data.text_by_id(1),
          "moved-from data cannot act as known empty catalog");
    check(take(moved.find_help("tip_test"), "moved data remains usable") ==
                  std::size_t{0} &&
              surviving_reference.text() == "none",
          "owned references survive moving the database");
    const auto all_sentinel =
        take(VmfTutorialDatabase::from_table(
                 table("1;2;3;5;0;\n1;2;3;5;0;\n", 2), catalog),
             "duplicate sentinel fixture");
    check(!take(all_sentinel.find_tutorial("tip_test", 3),
                "all matching rows excluded"),
          "sentinel filtering continues to end of table");
    for (const auto row :
         {"9;2;3;5;0;\n", "bad;2;3;5;0;\n", "18446744073709551616;2;3;5;0;\n",
          "1;2;3;5;2147483648;\n", "1;2;3;5;-2147483649;\n"}) {
        check(!VmfTutorialDatabase::from_table(table(row, 1), catalog),
              "missing/malformed IDs or signed32 overflow refused");
    }
    check(!VmfTutorialDatabase::from_table(table("1;2;3;5;0;\n", 1, "h"),
                                           catalog),
          "schema type is not guessed");
    for (const auto limits :
         {VmfMenuDataLimits{1}, VmfMenuDataLimits{10, 1},
          VmfMenuDataLimits{10, 10, 0}, VmfMenuDataLimits{10, 10, 128, 1}}) {
        check(!VmfTutorialDatabase::from_table(source, catalog, limits),
              "row/catalog/ID/backing quotas");
    }
    const auto empty = take(VmfTutorialDatabase::from_table(
                                table("", 0), assets::DatabaseStrings{}),
                            "known empty database");
    check(empty.rows().empty() &&
              !take(empty.find_help("tip_test"), "known empty help lookup"),
          "real empty data is distinct from moved-from data");
    auto tight_limits = VmfMenuDataLimits{};
    tight_limits.max_reference_bytes = 4;
    const auto tight =
        take(VmfTutorialDatabase::from_table(table("1;2;3;5;0;\n", 1), catalog,
                                             tight_limits),
             "small query quota still admits the ID tokens");
    check(!tight.find_help("tip_test"), "query quota before row comparison");

    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    const auto counted = VmfTutorialDatabase::from_table(source, catalog);
    allocation_fixture::enabled = false;
    const auto creation_allocations = allocation_fixture::calls;
    check(counted.has_value() && creation_allocations > 0,
          "allocation fault fixture exercised real construction");
    for (std::size_t at = 0; at < creation_allocations; ++at) {
        allocation_fixture::calls = 0;
        allocation_fixture::fail_at = at;
        allocation_fixture::enabled = true;
        const auto failed = VmfTutorialDatabase::from_table(source, catalog);
        allocation_fixture::enabled = false;
        check(!failed && failed.error().code ==
                             VmfMenuDataErrorCode::allocation_failure,
              "each construction allocation fails as an explicit error");
    }
    allocation_fixture::fail_at = std::numeric_limits<std::size_t>::max();
    allocation_fixture::calls = 0;
    allocation_fixture::enabled = true;
    const auto noalloc_help = moved.find_help("tip_test");
    const auto noalloc_tutorial = moved.find_tutorial("tip_test", 3);
    const auto noalloc_text = moved.text_by_id(1);
    allocation_fixture::enabled = false;
    check(noalloc_help && noalloc_tutorial && noalloc_text &&
              allocation_fixture::calls == 0,
          "lookups retain references without ordinary heap allocations");
}

std::uint64_t id(std::string_view value) {
    std::uint64_t result{};
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), result);
    check(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
          "independent numeric source token");
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("source ID rejected");
    return result;
}
void official(const std::filesystem::path& root, std::uint64_t sentinel_id,
              std::uint64_t special_id) {
    const auto key = take(tdur::game::read_database_asset_key(root),
                          "audited official format adapter");
    const auto db =
        take(assets::BankArchive::read_file(root / "Euro/Bnk/DataBase/DB.bnk"),
             "read-only database bank");
    const auto fr = take(
        assets::BankArchive::read_file(root / "Euro/Bnk/DataBase/DB_FR.bnk"),
        "read-only localized bank");
    const auto member = [](const assets::BankArchive& bank,
                           std::string_view name) {
        const auto entries = bank.entries();
        const auto at = std::ranges::find_if(
            entries, [&](const auto& entry) { return entry.name == name; });
        check(at != entries.end(), "required tutorial member exists");
        if (at == entries.end())
            throw std::runtime_error("tutorial member absent");
        check(static_cast<std::size_t>(at - entries.begin()) == 11,
              "independent inventory's member index");
        return take(
            bank.stored_bytes(static_cast<std::size_t>(at - entries.begin())),
            "read-only stored tutorial member");
    };
    const auto source = take(
        assets::DatabaseTable::parse_asset(member(db, "TDU_Tutorials.db"), key),
        "tutorial table decoded in RAM");
    const auto catalog =
        take(assets::DatabaseStrings::parse(member(fr, "TDU_Tutorials.fr")),
             "tutorial catalog decoded in RAM");
    const auto data = take(VmfTutorialDatabase::from_table(source, catalog),
                           "all official references resolve");
    check(data.rows().size() == 92 && catalog.entries().size() == 562,
          "independent official inventory counts");
    std::map<std::uint64_t, std::string_view> first;
    for (const auto& entry : catalog.entries())
        first.try_emplace(id(entry.reference_token), entry.value);
    for (std::size_t i = 0; i < source.rows().size(); ++i) {
        const auto& row = data.rows()[i];
        const std::array<const VmfTextReference*, 4> fields{
            &row.key, &row.text_index, &row.text, &row.file};
        for (std::size_t column = 0; column < fields.size(); ++column) {
            const auto number = id(source.rows()[i].values[column]);
            const auto expected = first.find(number);
            check(expected != first.end(),
                  "source ID exists in actual catalog");
            if (expected == first.end())
                throw std::runtime_error("official reference missing");
            const auto resolved =
                take(data.text_by_id(number), "individual catalog lookup");
            check(resolved && fields[column]->text() == expected->second &&
                      fields[column]->c_str() == resolved->c_str(),
                  "actual first definition, content and shared identity");
        }
        check(row.time_cent == take(source.integer_value(i, 4),
                                    "original integer Time_cent"),
              "actual time preserved without scaling");
    }
    const auto sentinel = take(data.text_by_id(sentinel_id),
                               "caller-supplied native sentinel ID");
    const auto special = take(data.text_by_id(special_id),
                              "caller-supplied native special-text ID");
    check(sentinel.has_value() && special.has_value(),
          "both native service IDs exist without embedding their text");
    std::size_t help_hits = 0, tutorial_hits = 0;
    for (std::int32_t index = 0; index < 32; ++index) {
        const auto routing =
            take(vmf_help_routing_tag(index), "native routing");
        check(routing.has_value(), "selector table slot exists");
        const auto help =
            take(data.find_help(*routing), "actual routing-to-help lookup");
        const auto tutorial = take(data.find_tutorial(*routing, sentinel_id),
                                   "actual routing-to-tutorial lookup");
        std::optional<std::size_t> expected_help, expected_tutorial;
        for (std::size_t i = 0; i < source.rows().size(); ++i) {
            const auto& values = source.rows()[i].values;
            if (first.at(id(values[0])) != *routing)
                continue;
            if (!expected_help)
                expected_help = i;
            if (!expected_tutorial && id(values[2]) != sentinel_id)
                expected_tutorial = i;
        }
        check(help == expected_help && tutorial == expected_tutorial,
              "native routing and sentinel skip match independent ID oracle");
        help_hits += help.has_value();
        tutorial_hits += tutorial.has_value();
    }
    check(help_hits > 0, "actual routing keys resolve");
    std::cout << "official_tutorials rows=" << data.rows().size()
              << " catalog_entries=" << catalog.entries().size()
              << " help_routing_hits=" << help_hits
              << " tutorial_routing_hits=" << tutorial_hits
              << " commercial_game_launched=false\n";
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 5 && std::string_view(argv[1]) == "--official") {
            official(argv[2], id(argv[3]), id(argv[4]));
        } else {
            std::cerr << "Usage: vmf_tutorial_data_test "
                         "[--official GAME_DIR SENTINEL_ID SPECIAL_ID]\n";
            return EXIT_FAILURE;
        }
    } catch (const std::exception& error) {
        allocation_fixture::enabled = false;
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

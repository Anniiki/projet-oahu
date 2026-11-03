#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/database.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace tdur::assets;
using Bytes = std::vector<std::byte>;
std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures < 30) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }
}

Bytes bytes(std::string_view text) {
    Bytes result;
    for (const auto c : text) {
        result.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return result;
}

Bytes utf16(std::u16string_view text) {
    Bytes result{std::byte{0xff}, std::byte{0xfe}};
    for (const auto c : text) {
        result.push_back(std::byte(c & 255U));
        result.push_back(std::byte(c >> 8U));
    }
    return result;
}

bool near(const std::expected<double, DatabaseError>& value, double expected) {
    return value && std::abs(*value - expected) < 1e-12;
}

void cipher_vectors() {
    // Independent Bouncy Castle XTEATest vector0, written as LE DWORDs.
    // This all-zero synthetic key is not the game's resource constant.
    const DatabaseAssetKey synthetic{};
    const std::array<unsigned int, 8> cipher{0xd8, 0xd4, 0xe9, 0xde,
                                             0xd9, 0x1e, 0x13, 0xf7};
    Bytes encoded(8);
    for (const auto c : cipher) {
        encoded.push_back(std::byte(c));
    }
    const auto decoded = decode_database_resource(encoded, synthetic);
    check(
        decoded && decoded->size() == 8 &&
            std::all_of(decoded->begin(), decoded->end(),
                        [](std::byte value) { return value == std::byte{0}; }),
        "independent known XTEA zero vector");
    for (std::size_t i = 0; i < 8; ++i) {
        encoded[i] = std::byte((i + 1) * 16);
    }
    for (const auto c : cipher) {
        encoded.push_back(std::byte(c));
    }
    const auto cbc = decode_database_resource(encoded, synthetic);
    check(cbc && cbc->size() == 16 &&
              std::equal(cbc->begin(), cbc->begin() + 8, encoded.begin()) &&
              std::equal(cbc->begin() + 8, cbc->end(), encoded.begin() + 8),
          "CBC XOR uses original IV and previous ciphertext, not plaintext");
    Bytes second(8);
    for (const auto c :
         {0x89U, 0x1bU, 0x5cU, 0x06U, 0x16U, 0xa8U, 0xc6U, 0x75U}) {
        second.push_back(std::byte(c));
    }
    const auto vector1 = decode_database_resource(second, synthetic);
    const Bytes expected{std::byte{4}, std::byte{3}, std::byte{2},
                         std::byte{1}, std::byte{8}, std::byte{7},
                         std::byte{6}, std::byte{5}};
    check(vector1 && *vector1 == expected,
          "independent nonzero XTEA plaintext vector and DWORD byte order");
    for (std::size_t size = 0; size < 16; ++size) {
        check(!decode_database_resource(Bytes(size), synthetic),
              "truncated IV/block rejected");
    }
    check(!decode_database_resource(Bytes(17), synthetic),
          "partial final cipher block rejected");
    DatabaseLimits limits;
    limits.max_bytes = 15;
    check(!decode_database_resource(second, synthetic, limits),
          "cipher byte budget enforced");
}

std::string small_table() {
    return "// synthetic\r\r\n{TDU_Test} 42\r\r\n"
           "{REF} x\r\n{Ratio} f\n{Count} i\n{Other} z 77\n"
           "{Same} i\n{Same} i\n// items: 2\n"
           "001;3,54;-2;;4;5;\r\r\n002;0.5; +7 ; x ;6;7\n";
}

void table_values() {
    auto source = bytes(small_table());
    const auto table = DatabaseTable::parse_plaintext(source);
    check(table && table->topic() == "TDU_Test" &&
              table->reference_token() == "42",
          "topic and raw reference");
    if (!table) {
        return;
    }
    check(table->columns().size() == 6 && table->rows().size() == 2 &&
              table->declared_row_count() == 2,
          "schema and row counts");
    check(table->columns()[3].type_symbol == 'z' &&
              table->columns()[3].reference_token == "77",
          "unknown type/metadata preserved without invented meaning");
    check(table->value(0, 0) == "001" && table->value(0, 3) == "" &&
              table->value(1, 3) == " x ",
          "raw IDs, empty cells and spaces");
    check(near(table->decimal_value(0, 1), 3.54) &&
              near(table->decimal_value(1, 1), 0.5),
          "comma/point decimal conversion is explicit and locale independent");
    check(table->integer_value(0, 2) == std::int64_t{-2} &&
              table->integer_value(1, 2) == std::int64_t{7},
          "signed integers, explicit plus and whitespace");
    check(table->columns()[4].name == "Same" &&
              table->columns()[5].name == "Same" &&
              !table->unique_column("Same") &&
              table->unique_column("Same").error().code ==
                  DatabaseErrorCode::ambiguous_column,
          "duplicate columns retain source order and cause explicit ambiguity");
    check(table->unique_column("Ratio") == std::size_t{1} &&
              !table->unique_column("ratio"),
          "column names are exact");
    check(!table->value(2, 0) && !table->value(0, 6), "row/column bounds");
    check(!table->integer_value(0, 1),
          "decimal text is not silently truncated");
    source.assign(source.size(), std::byte{'x'});
    check(table->value(0, 0) == "001", "table owns copied schema/values");
    for (std::size_t padding = 0; padding <= 8; ++padding) {
        auto padded = bytes(small_table());
        padded.insert(padded.end(), padding, std::byte{0});
        check(DatabaseTable::parse_plaintext(padded).has_value(),
              "zero cipher padding is outside the table");
    }
    auto bom = bytes(small_table());
    bom.insert(bom.begin(),
               {std::byte{0xef}, std::byte{0xbb}, std::byte{0xbf}});
    check(DatabaseTable::parse_plaintext(bom).has_value(), "UTF8 BOM accepted");
    check(DatabaseTable::parse_plaintext(
              bytes("{TDU_Empty} 1\n{REF} x\n// items: 0\n"))
              .has_value(),
          "explicitly empty table accepted");
    for (const auto value : {"nan", "inf", "1,234.5", "1,2,3", "2.3x", ""}) {
        const auto numeric = DatabaseTable::parse_plaintext(
            bytes(std::string("{N} 1\n{V} f\n// items: 1\n") + value + ";\n"));
        check(numeric && !numeric->decimal_value(0, 0),
              "nonfinite/ambiguous/incomplete numbers rejected explicitly");
    }
}

void malformed_tables() {
    for (const auto text :
         {"", "text", "{T} 1\n", "{T} 1\n{A} i\n",
          "{T} bad\n{A} i\n// items: 0\n", "{T} 1\n{A} integer\n// items: 0\n",
          "{T} 1\n{A} i\n// items: -1\n", "{T} 1\n{A} i\n// items: 2\n1;\n",
          "{T} 1\n{A} i\n// items: 0\n1;\n",
          "{T} 1\n{A} i\n{B} i\n// items: 1\n1;\n",
          "{T} 1\n{A} i\n// items: 1\n1;2;\n"}) {
        check(!DatabaseTable::parse_plaintext(bytes(text)),
              "invalid schema, row width or declared count rejected");
    }
    auto nul = bytes(small_table());
    nul[20] = std::byte{0};
    check(!DatabaseTable::parse_plaintext(nul), "embedded NUL rejected");
    auto padding = bytes(small_table());
    padding.insert(padding.end(), 9, std::byte{0});
    check(!DatabaseTable::parse_plaintext(padding),
          "excess zero padding rejected");
    DatabaseLimits limits;
    limits.max_bytes = 1;
    check(!DatabaseTable::parse_plaintext(bytes(small_table()), limits),
          "table byte limit");
    limits = {};
    limits.max_columns = 5;
    check(!DatabaseTable::parse_plaintext(bytes(small_table()), limits),
          "column count limit");
    limits = {};
    limits.max_rows = 1;
    check(!DatabaseTable::parse_plaintext(bytes(small_table()), limits),
          "row count limit");
    limits = {};
    limits.max_cells = 6;
    check(!DatabaseTable::parse_plaintext(bytes(small_table()), limits),
          "cell count limit");
    limits = {};
    limits.max_line_bytes = 1;
    check(!DatabaseTable::parse_plaintext(bytes(small_table()), limits),
          "line byte limit");
}

void string_resources() {
    auto source = utf16(u"// Cars\r\r\n{Caf\u00e9} 0007\r\n"
                        u"{Alfa_GT} 42\n{\U0001f697} 9\n");
    const auto strings = DatabaseStrings::parse(source);
    check(strings && strings->entries().size() == 3,
          "UTF16LE resource entries");
    if (!strings) {
        return;
    }
    check(strings->unique_value("0007") == std::string_view("Caf\xc3\xa9"),
          "UTF16 accent becomes UTF8 while ID zeros survive");
    check(strings->unique_value("9") == std::string_view("\xf0\x9f\x9a\x97"),
          "surrogate pair becomes one UTF8 scalar");
    check(strings->entries()[0].category_comment == "Cars" &&
              !strings->unique_value("7"),
          "category and exact reference");
    source.clear();
    check(strings->unique_value("42") == "Alfa_GT", "resource owns its values");
    const auto repeated = DatabaseStrings::parse(bytes("{one} 1\n{two} 1\n"));
    check(repeated && repeated->entries().size() == 2 &&
              !repeated->unique_value("1") &&
              repeated->unique_value("1").error().code ==
                  DatabaseErrorCode::ambiguous_reference,
          "duplicate resource IDs are preserved, not silently replaced");
    const auto multiline =
        DatabaseStrings::parse(bytes("{line one\nline two} 5"));
    check(multiline && multiline->unique_value("5") == "line one\nline two",
          "multiline resource value");
    for (const auto text : {"bad", "{missing", "{bad} no-id"}) {
        check(!DatabaseStrings::parse(bytes(text)),
              "malformed resource rejected");
    }
    check(!DatabaseStrings::parse(
              Bytes{std::byte{0xff}, std::byte{0xfe}, std::byte{0x41}}),
          "odd UTF16 byte length rejected");
    check(!DatabaseStrings::parse(utf16(u"{\xd800} 1")),
          "unpaired high surrogate rejected");
    check(!DatabaseStrings::parse(utf16(u"{\xdc00} 1")),
          "unpaired low surrogate rejected");
    DatabaseLimits limits;
    limits.max_rows = 0;
    check(!DatabaseStrings::parse(bytes("{one} 1"), limits),
          "localized entry budget");
}

std::string physics_fixture() {
    return "{TDU_CarPhysicsData} 1975083164\n"
           "{REF} x\n{Car_File_Name} u\n{Drive} h\n{GearBox} h\n"
           "{Nb_Gears} i\n{Red_Line} i\n{Power_bhp} i\n{Power_RPM} i\n"
           "{Torque_Nm} i\n{Torque_RPM} i\n{Weight_Kg} i\n"
           "{Engine_Inertia} i\n{Gearbox_Inertia} i\n"
           "{Final_Drive_Ratio} f\n{Ignition_RPM} f\n"
           "{Gear_ratio_1} f\n{Gear_ratio_2} f\n{Gear_ratio_3} f\n"
           "{Gear_ratio_4} f\n{Gear_ratio_5} f\n{Gear_ratio_6} f\n"
           "{Gear_ratio_7} f\n// items: 1\n"
           "00123;42;11;12;3;6500;120;6000;180;3500;1000;45;12;"
           "3,75;1000.5;3.2;2.1;1.5;0;0;0;0;\n";
}

void drivetrain_projection() {
    const auto table = DatabaseTable::parse_plaintext(bytes(physics_fixture()));
    check(table.has_value(), "synthetic physics schema parses");
    if (!table) {
        return;
    }
    const auto drive = read_stored_car_drivetrain(*table, 0);
    check(drive && drive->reference_token == "00123" && drive->nb_gears == 3 &&
              std::abs(drive->final_drive_ratio - 3.75) < 1e-12 &&
              drive->gear_ratios[0] == 3.2 && drive->gear_ratios[6] == 0,
          "named gearbox values retain source scale and inactive zeros");
    check(drive && drive->engine_inertia_raw == 45 &&
              drive->gearbox_inertia_raw == 12 && drive->red_line == 6500 &&
              drive->ignition_rpm == 1000.5 && drive->weight_kg == 1000,
          "engine/inertia values are not converted or reinterpreted");
    check(drive && drive->gearbox_resource_token == "12" &&
              drive->drive_resource_token == "11",
          "opaque localized IDs");
    check(!read_stored_car_drivetrain(*table, 1), "physics row bound");
    auto wrong_type = physics_fixture();
    const auto at = wrong_type.find("{Red_Line} i");
    wrong_type[at + std::string_view("{Red_Line} ").size()] = 'f';
    const auto changed = DatabaseTable::parse_plaintext(bytes(wrong_type));
    check(changed && !read_stored_car_drivetrain(*changed, 0),
          "projection requires proven source type symbols");
    const auto other = DatabaseTable::parse_plaintext(bytes(small_table()));
    check(!read_stored_car_drivetrain(*other, 0),
          "projection requires correct topic");
}

std::optional<DatabaseAssetKey> environment_key() {
    std::string text;
#ifdef _WIN32
    char* environment = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&environment, &length, "TDUR_DATABASE_TEST_KEY") != 0 ||
        environment == nullptr) {
        return std::nullopt;
    }
    text = environment;
    std::free(environment);
#else
    const auto environment = std::getenv("TDUR_DATABASE_TEST_KEY");
    if (environment == nullptr) {
        return std::nullopt;
    }
    text = environment;
#endif
    if (text.size() != 32) {
        return std::nullopt;
    }
    DatabaseAssetKey key{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto parsed = std::from_chars(
            text.data() + i * 8, text.data() + (i + 1) * 8, key[i], 16);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != text.data() + (i + 1) * 8) {
            return std::nullopt;
        }
    }
    return key;
}

struct Corpus {
    std::size_t tables{};
    std::size_t rows{};
    std::size_t cells{};
    std::size_t string_entries{};
    std::optional<DatabaseTable> physics;
    std::optional<DatabaseStrings> physics_strings;
};

void official_bank(const std::filesystem::path& path,
                   const DatabaseAssetKey& key, Corpus& corpus) {
    const auto bank = BankArchive::read_file(path);
    check(bank.has_value(), "official bank reads without modification");
    if (!bank) {
        return;
    }
    for (std::size_t i = 0; i < bank->entries().size(); ++i) {
        const auto& name = bank->entries()[i].name;
        const auto stored = bank->stored_bytes(i);
        if (std::string_view(name).ends_with(".db")) {
            auto table = DatabaseTable::parse_asset(*stored, key);
            check(table.has_value(), "official database decodes and parses");
            if (!table) {
                std::cerr << name << " offset=" << table.error().offset << ' '
                          << table.error().detail << '\n';
                continue;
            }
            ++corpus.tables;
            corpus.rows += table->rows().size();
            corpus.cells += table->rows().size() * table->columns().size();
            std::cout << name << " columns=" << table->columns().size()
                      << " rows=" << table->rows().size() << '\n';
            for (const auto& row : table->rows()) {
                check(row.values.size() == table->columns().size(),
                      "every official row preserves complete source width");
            }
            if (table->topic() == "TDU_CarPhysicsData") {
                check(table->columns().size() == 103 &&
                          table->rows().size() == 211 &&
                          table->reference_token() == "1975083164",
                      "official physics schema identity and counts");
                corpus.physics = std::move(*table);
            }
        } else if (name == "TDU_CarPhysicsData.fr") {
            auto strings = DatabaseStrings::parse(*stored);
            check(strings.has_value(),
                  "official localized UTF16 resource parses");
            if (!strings) {
                std::cerr << name << " offset=" << strings.error().offset << ' '
                          << strings.error().detail << '\n';
                continue;
            }
            corpus.string_entries += strings->entries().size();
            corpus.physics_strings = std::move(*strings);
        }
    }
}

void official_physics(Corpus& corpus) {
    if (!corpus.physics) {
        return;
    }
    std::size_t projections = 0;
    std::size_t alfa_rows = 0;
    for (std::size_t row = 0; row < corpus.physics->rows().size(); ++row) {
        const auto drive = read_stored_car_drivetrain(*corpus.physics, row);
        check(drive.has_value(),
              "official drivetrain fields convert without defaults");
        if (!drive) {
            std::cerr << "physics row=" << row << ' ' << drive.error().detail
                      << '\n';
            continue;
        }
        ++projections;
        if (!corpus.physics_strings) {
            continue;
        }
        const auto name = corpus.physics_strings->unique_value(
            drive->car_file_resource_token);
        check(name.has_value(), "car-file resource token resolves explicitly");
        if (name && *name == "Alfa_GT") {
            ++alfa_rows;
            const auto box = corpus.physics_strings->unique_value(
                drive->gearbox_resource_token);
            const auto layout = corpus.physics_strings->unique_value(
                drive->drive_resource_token);
            std::cout << "Alfa_GT row=" << row
                      << " REF=" << drive->reference_token
                      << " gears=" << drive->nb_gears
                      << " final=" << drive->final_drive_ratio << " ratios=";
            for (const auto ratio : drive->gear_ratios) {
                std::cout << ratio << ',';
            }
            std::cout << " Red_Line=" << drive->red_line
                      << " Power_RPM=" << drive->power_rpm
                      << " Torque_Nm=" << drive->torque_nm
                      << " Torque_RPM=" << drive->torque_rpm
                      << " Weight_Kg=" << drive->weight_kg
                      << " Engine_Inertia_raw=" << drive->engine_inertia_raw
                      << " Gearbox_Inertia_raw=" << drive->gearbox_inertia_raw
                      << " GearBox_label=" << (box ? *box : "unresolved")
                      << " Drive_label=" << (layout ? *layout : "unresolved")
                      << '\n';
        }
    }
    std::cout << "drivetrain_projections=" << projections
              << " Alfa_GT_rows=" << alfa_rows << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    cipher_vectors();
    table_values();
    malformed_tables();
    string_resources();
    drivetrain_projection();
    if (argc > 1) {
        const auto key = environment_key();
        if (!key) {
            std::cerr << "Real-asset validation needs its local "
                         "resource-constant provider\n";
            return 2;
        }
        Corpus corpus;
        for (int i = 1; i < argc; ++i) {
            official_bank(std::filesystem::path(argv[i]), *key, corpus);
        }
        official_physics(corpus);
        std::cout << "tables=" << corpus.tables << " rows=" << corpus.rows
                  << " cells=" << corpus.cells
                  << " localized_entries=" << corpus.string_entries << '\n';
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

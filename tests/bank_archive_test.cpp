#include "tdur/assets/bank_archive.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using tdur::assets::BankArchive;
using tdur::assets::BankErrorCode;
using Bytes = std::vector<std::byte>;
std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view description) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = std::byte((value >> (8U * i)) & 0xffU);
    }
}

std::uint32_t get32(const Bytes& bytes, std::size_t offset) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        result += std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return result;
}

// Slow bitwise reference independent of the reader's table implementation.
std::uint32_t checksum(std::span<const std::byte> bytes) {
    std::uint32_t result = 0xffffffffU;
    for (const auto byte : bytes) {
        result ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned int i = 0; i < 8; ++i) {
            if ((result & 1U) == 0) {
                result >>= 1U;
            } else {
                result = (result >> 1U) ^ 0xedb88320U;
            }
        }
    }
    return result;
}

void update_checksum(Bytes& bytes, std::size_t offset) {
    put32(bytes, offset + 4,
          checksum(std::span<const std::byte>(bytes).subspan(
              offset + 8, get32(bytes, offset))));
}

void append_name(Bytes& bytes, std::string_view name) {
    for (const auto character : name) {
        bytes.push_back(std::byte(static_cast<unsigned char>(character)));
    }
}

struct Fixture {
    Bytes bytes;
    std::size_t records{};
    std::size_t types{};
    std::size_t names{};
    std::size_t order{};
    std::size_t data{};
};

Fixture fixture(std::size_t count = 3, bool optional_control = false,
                bool second_root = false) {
    Bytes tree;
    const auto folder = [&tree](std::string_view name, std::size_t children,
                                bool control = false) {
        tree.push_back(std::byte(256U - name.size()));
        tree.push_back(std::byte(children));
        if (control) {
            tree.push_back(std::byte{1});
        }
        append_name(tree, name);
    };
    const auto root = [&](std::string_view drive, std::size_t first,
                          std::size_t leaf_count) {
        folder(drive, 1);
        folder(".bin", leaf_count, optional_control);
        for (std::size_t i = first; i < first + leaf_count; ++i) {
            const auto name = "f" + std::to_string(i);
            tree.push_back(std::byte(name.size()));
            append_name(tree, name);
        }
    };
    if (count != 0) {
        const auto first_count = second_root ? count - 1 : count;
        root("C:", 0, first_count);
        if (second_root) {
            root("P:", first_count, 1);
        }
    }
    tree.push_back(std::byte{0});

    Fixture result;
    result.records = 72;
    result.types = result.records + 8 + (count + 1) * 16;
    result.names = result.types + 8 + count * 4;
    result.order = result.names + 8 + tree.size();
    const std::size_t width = count + 1 > 255 ? 2 : 1;
    result.data = result.order + 8 + count * width;
    result.bytes.resize(result.data + count * 3 + 16);
    auto& b = result.bytes;
    put32(b, 0, 64);
    put32(b, 8, 0x42414e4bU);
    put32(b, 24, static_cast<std::uint32_t>(b.size()));
    put32(b, 32, 4);
    put32(b, 36, 16);
    put32(b, 40, static_cast<std::uint32_t>(count));
    put32(b, 44, 2006);
    put32(b, 48, static_cast<std::uint32_t>(result.records));
    put32(b, 52, static_cast<std::uint32_t>(result.types));
    put32(b, 56, static_cast<std::uint32_t>(result.names));
    put32(b, 60, static_cast<std::uint32_t>(result.order));
    put32(b, 68, count == 0 ? 0U : static_cast<std::uint32_t>(result.data));
    put32(b, result.records, static_cast<std::uint32_t>((count + 1) * 16));
    put32(b, result.types, static_cast<std::uint32_t>(count * 4));
    put32(b, result.names, static_cast<std::uint32_t>(tree.size()));
    put32(b, result.order, static_cast<std::uint32_t>(count * width));
    std::copy(tree.begin(), tree.end(), b.begin() + result.names + 8);
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = result.records + 8 + i * 16;
        put32(b, record, static_cast<std::uint32_t>(result.data + i * 3));
        put32(b, record + 4, 3);
        put32(b, record + 8, 0x12345678U);
        put32(b, record + 12, 0x90abcdefU);
        put32(b, result.types + 8 + i * 4, 0x76543210U);
        // Deliberately reverse the tree/physical order.
        const auto index = count - i - 1;
        b[result.order + 8 + i * width] = std::byte(index & 0xffU);
        if (width == 2) {
            b[result.order + 9 + i * width] = std::byte(index >> 8U);
        }
        for (std::size_t j = 0; j < 3; ++j) {
            b[result.data + i * 3 + j] = std::byte((i + j) & 0xffU);
        }
    }
    put32(b, result.records + 8 + count * 16,
          static_cast<std::uint32_t>(result.data + count * 3));
    for (const auto offset : {std::size_t{0}, result.records, result.types,
                              result.names, result.order}) {
        update_checksum(b, offset);
    }
    return result;
}

void expect_error(Bytes bytes, BankErrorCode expected,
                  std::string_view description) {
    const auto result = BankArchive::parse(std::move(bytes));
    check(!result && result.error().code == expected, description);
}

void valid_fixtures() {
    const auto f = fixture();
    auto bank = BankArchive::parse(f.bytes);
    check(bank.has_value(), "valid bank parses");
    if (!bank) {
        return;
    }
    check(bank->entries().size() == 3, "sentinel is not exposed as an asset");
    check(bank->byte_size() == f.bytes.size(), "physical length retained");
    check(bank->recorded_year() == 2006U, "recorded year retained");
    check(bank->entries()[0].name == "f2.bin", "permutation maps first record");
    check(bank->entries()[2].name == "f0.bin", "permutation maps last record");
    check(bank->entries()[0].path == "C:/f2.bin", "normal filename path");
    check(bank->entries()[0].tree_path == "C:/.bin/f2", "original group path");
    check(bank->entries()[0].opaque_record_words[0] == 0x12345678U,
          "record metadata remains opaque");
    check(bank->entries()[0].opaque_type_words ==
              std::optional<std::array<std::uint16_t, 2>>{{0x3210, 0x7654}},
          "type words preserved without guessed meaning");
    for (std::size_t i = 0; i < bank->entries().size(); ++i) {
        const auto view = bank->stored_bytes(i);
        auto copy = bank->extract(i);
        check(view && view->size() == 3 && (*view)[0] == std::byte(i) &&
                  (*view)[1] == std::byte(i + 1) &&
                  (*view)[2] == std::byte(i + 2),
              "payload bytes match independent fixture values");
        check(view && copy &&
                  std::equal(view->begin(), view->end(), copy->begin(),
                             copy->end()),
              "extraction equals bounded stored view");
        if (copy) {
            (*copy)[0] = std::byte{0xff};
            check((*bank->stored_bytes(i))[0] != std::byte{0xff},
                  "extracted copy owns its storage");
        }
    }
    const auto invalid_view = bank->stored_bytes(3);
    check(!invalid_view && invalid_view.error().code ==
                               BankErrorCode::member_index_out_of_range,
          "out-of-range view rejected");
    check(!bank->extract(std::numeric_limits<std::size_t>::max()),
          "out-of-range extraction rejected");
    auto moved = std::move(*bank);
    check(moved.stored_bytes(0)->size() == 3, "move retains owned payload");

    for (const auto count :
         {std::size_t{0}, std::size_t{1}, std::size_t{254}, std::size_t{255}}) {
        auto boundary = BankArchive::parse(fixture(count).bytes);
        check(boundary && boundary->entries().size() == count,
              "empty/8-bit/16-bit permutation boundary");
        if (boundary && count != 0) {
            check(boundary->entries()[count - 1].name == "f0.bin",
                  "last permutation index resolves");
        }
    }
    check(BankArchive::parse(fixture(3, true).bytes).has_value(),
          "extra control byte is skipped, not added to child count");
    auto forest = BankArchive::parse(fixture(3, false, true).bytes);
    check(forest && forest->entries()[0].path == "P:/f2.bin",
          "multiple roots retain their own path");

    auto zero_member = fixture();
    put32(zero_member.bytes, zero_member.records + 12, 0);
    update_checksum(zero_member.bytes, zero_member.records);
    auto zero = BankArchive::parse(std::move(zero_member.bytes));
    check(zero && zero->extract(0)->empty(), "zero-byte member is retained");

    auto without_types = fixture();
    put32(without_types.bytes, 52, 0);
    update_checksum(without_types.bytes, 0);
    auto no_types = BankArchive::parse(std::move(without_types.bytes));
    check(no_types && !no_types->entries()[0].opaque_type_words,
          "optional type table may be absent");
}

void malformed_fixtures() {
    const auto original = fixture();
    // Every cut of a valid file is rejected. For cuts inside the payload,
    // update physical length to also exercise section/member bounds.
    for (std::size_t size = 0; size < original.bytes.size(); ++size) {
        auto b = original.bytes;
        b.resize(size);
        if (size >= 72 && size < original.data + 9) {
            put32(b, 24, static_cast<std::uint32_t>(size));
            update_checksum(b, 0);
        }
        check(!BankArchive::parse(std::move(b)),
              "every truncated prefix fails");
    }
    auto b = original.bytes;
    b[8] = std::byte{'Z'};
    expect_error(std::move(b), BankErrorCode::unsupported_format,
                 "unsupported magic is explicit");
    b = original.bytes;
    put32(b, 24, 1);
    expect_error(std::move(b), BankErrorCode::invalid_header,
                 "declared physical size mismatch");
    b = original.bytes;
    b[44] ^= std::byte{1};
    expect_error(std::move(b), BankErrorCode::checksum_mismatch,
                 "header checksum validated");
    for (const auto offset :
         {original.records, original.types, original.names, original.order}) {
        b = original.bytes;
        b[offset + 8] ^= std::byte{1};
        expect_error(std::move(b), BankErrorCode::checksum_mismatch,
                     "each metadata checksum validated");
    }
    b = original.bytes;
    put32(b, 56, 0xfffffff0U);
    update_checksum(b, 0);
    expect_error(std::move(b), BankErrorCode::truncated,
                 "huge section offset cannot wrap");
    b = original.bytes;
    put32(b, 56, static_cast<std::uint32_t>(original.records));
    update_checksum(b, 0);
    expect_error(std::move(b), BankErrorCode::invalid_section,
                 "metadata overlap rejected");
    b = original.bytes;
    put32(b, original.records, 0xfffffff0U);
    expect_error(std::move(b), BankErrorCode::truncated,
                 "huge section length cannot wrap");
    b = original.bytes;
    put32(b, original.records + 8, 0xfffffff0U);
    update_checksum(b, original.records);
    expect_error(std::move(b), BankErrorCode::invalid_member,
                 "huge member offset cannot wrap");
    b = original.bytes;
    put32(b, original.records + 12, 0xffffffffU);
    update_checksum(b, original.records);
    expect_error(std::move(b), BankErrorCode::invalid_member,
                 "huge member length cannot wrap");
    b = original.bytes;
    put32(b, original.records + 8, static_cast<std::uint32_t>(original.names));
    update_checksum(b, original.records);
    expect_error(std::move(b), BankErrorCode::invalid_member,
                 "member may not read metadata as payload");
    b = original.bytes;
    put32(b, original.records + 24,
          static_cast<std::uint32_t>(original.data + 1));
    update_checksum(b, original.records);
    expect_error(std::move(b), BankErrorCode::invalid_member,
                 "overlapping stored payloads rejected");
    b = original.bytes;
    put32(b, original.records + 8 + 3 * 16,
          static_cast<std::uint32_t>(original.data));
    update_checksum(b, original.records);
    expect_error(std::move(b), BankErrorCode::invalid_member,
                 "padding sentinel cannot precede payloads");
    b = original.bytes;
    put32(b, original.records + 12 + 3 * 16, 1);
    update_checksum(b, original.records);
    expect_error(std::move(b), BankErrorCode::invalid_member,
                 "final record must be sentinel");
    b = original.bytes;
    b[original.order + 8] = std::byte{3};
    update_checksum(b, original.order);
    expect_error(std::move(b), BankErrorCode::invalid_order,
                 "permutation cannot refer to sentinel");
    b = original.bytes;
    b[original.order + 9] = b[original.order + 8];
    update_checksum(b, original.order);
    expect_error(std::move(b), BankErrorCode::invalid_order,
                 "duplicate permutation index rejected");
    b = original.bytes;
    // Root marker/count/name = fe 01 C:; extension marker/count/name follows.
    b[original.names + 8 + 5] = std::byte{4};
    update_checksum(b, original.names);
    expect_error(std::move(b), BankErrorCode::invalid_name_tree,
                 "child count may not cross a terminator");
    b = original.bytes;
    b[original.names + 8 + 6] = std::byte{'x'};
    update_checksum(b, original.names);
    expect_error(std::move(b), BankErrorCode::invalid_name_tree,
                 "leaf requires extension group");
    b = original.bytes;
    // The three leaves f0/f1/f2 have equal encoded length.
    b[original.names + 8 + 15] = std::byte{'0'};
    update_checksum(b, original.names);
    expect_error(std::move(b), BankErrorCode::invalid_name_tree,
                 "duplicate reconstructed path rejected");
    b = original.bytes;
    b[original.names + 8 + get32(b, original.names) - 1] = std::byte{1};
    update_checksum(b, original.names);
    expect_error(std::move(b), BankErrorCode::invalid_name_tree,
                 "missing tree terminator rejected");

    tdur::assets::BankReadLimits limits;
    limits.max_bank_bytes = original.bytes.size() - 1;
    check(!BankArchive::parse(original.bytes, limits), "bank budget enforced");
    limits = {};
    limits.max_members = 2;
    check(!BankArchive::parse(original.bytes, limits),
          "member budget enforced");
    limits = {};
    limits.max_name_bytes = 1;
    check(!BankArchive::parse(original.bytes, limits), "name budget enforced");
    limits = {};
    limits.max_tree_depth = 1;
    check(!BankArchive::parse(original.bytes, limits), "depth budget enforced");
    limits = {};
    limits.max_path_bytes = 4;
    check(!BankArchive::parse(original.bytes, limits), "path budget enforced");
    check(!BankArchive::read_file(std::filesystem::path{
              L"__tdur_missing_bank_fixture_761badf8.bnk"}),
          "missing input is an explicit read error");
}

void official_bank(const std::filesystem::path& path, bool verbose = true) {
    const auto bank = BankArchive::read_file(path);
    check(bank.has_value(), "official bank parses");
    if (!bank) {
        std::cerr << "Bank error at " << bank.error().offset << ": "
                  << bank.error().detail << '\n';
        return;
    }
    for (std::size_t i = 0; i < bank->entries().size(); ++i) {
        const auto view = bank->stored_bytes(i);
        const auto copy = bank->extract(i);
        check(view && copy &&
                  std::equal(view->begin(), view->end(), copy->begin(),
                             copy->end()),
              "official member extraction matches stored byte range");
    }
    if (verbose) {
        std::cout << "Official bank: " << bank->byte_size() << " bytes, "
                  << bank->entries().size() << " members checked\n";
    }
}

void official_root(const std::filesystem::path& root) {
    std::error_code traversal_error;
    std::filesystem::recursive_directory_iterator iterator(root,
                                                           traversal_error);
    const std::filesystem::recursive_directory_iterator end;
    std::size_t banks = 0;
    while (!traversal_error && iterator != end) {
        if (iterator->is_regular_file(traversal_error)) {
            auto extension = iterator->path().extension().string();
            for (auto& character : extension) {
                if (character >= 'A' && character <= 'Z') {
                    character = static_cast<char>(character - 'A' + 'a');
                }
            }
            if (extension == ".bnk") {
                official_bank(iterator->path(), false);
                ++banks;
            }
        }
        if (!traversal_error) {
            iterator.increment(traversal_error);
        }
    }
    check(!traversal_error, "official root traversal completes");
    check(banks != 0, "official root contains at least one bank");
    std::cout << "Official banks visited: " << banks << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    valid_fixtures();
    malformed_fixtures();
    if (argc == 3 && std::string_view(argv[1]) == "--official-root") {
        official_root(std::filesystem::path(argv[2]));
    } else {
        for (int i = 1; i < argc; ++i) {
            official_bank(std::filesystem::path(argv[i]));
        }
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

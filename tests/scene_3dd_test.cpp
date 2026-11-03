#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/scene_3dd.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>

using namespace tdur::assets;

namespace {
unsigned checks = 0;
unsigned failures = 0;
void check(bool value, const char* detail) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << detail << '\n';
    }
}
using Data = std::vector<std::byte>;
void word(Data& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = std::byte((value >> (i * 8)) & 255);
    }
}
void text(Data& bytes, std::size_t offset, std::string_view value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        bytes.at(offset + i) = std::byte(static_cast<unsigned char>(value[i]));
    }
}
void chunk(Data& bytes, std::size_t offset, std::string_view tag,
           std::uint32_t size, bool container = false) {
    text(bytes, offset, tag);
    word(bytes, offset + 8, size);
    word(bytes, offset + 12, container ? 16 : size);
}
void matrix(Data& bytes, std::size_t offset, const Scene3DDMatrix& value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        word(bytes, offset + i * 4, std::bit_cast<std::uint32_t>(value[i]));
    }
}
constexpr Scene3DDMatrix Identity{1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};

// Hand-laid-out fixture, independent of the reader's chunk builders. The
// hierarchy is ROOT -> CHILD -> LEAF, while the drawable's key is PART.
Data scene() {
    Data b(1024);
    word(b, 0, 2);
    word(b, 8, 1024);
    text(b, 12, ".3DD");
    chunk(b, 16, "MBNA", 48, true);
    chunk(b, 32, "MBNK", 32);
    text(b, 48, "MAT_A");
    b[56] = std::byte{0x9f};
    chunk(b, 64, "OBJA", 96, true);
    chunk(b, 80, "HASH", 32);
    text(b, 96, "PART");
    word(b, 104, 112);
    chunk(b, 112, "OBJ.", 48);
    word(b, 140, 1);
    chunk(b, 160, "INSA", 608, true);
    chunk(b, 176, "HASH", 64);
    chunk(b, 768, "HIEA", 176, true);
    chunk(b, 784, "HASH", 64);
    constexpr std::string_view labels[]{"ROOT", "CHILD", "LEAF"};
    constexpr std::uint32_t instances[]{240, 416, 592};
    constexpr std::uint32_t hierarchy[]{848, 880, 912};
    for (std::size_t i = 0; i < 3; ++i) {
        text(b, 192 + i * 16, labels[i]);
        word(b, 200 + i * 16, instances[i]);
        chunk(b, instances[i], "INS.", 176);
        text(b, 800 + i * 16, labels[i]);
        word(b, 808 + i * 16, hierarchy[i]);
        chunk(b, hierarchy[i], "HIE.", 32);
        word(b, hierarchy[i] + 20, instances[i]);
        word(b, hierarchy[i] + 24, 0x789a0000U | std::uint32_t(2 - i));
        word(b, hierarchy[i] + 28, i ? hierarchy[i - 1] : 0);
    }
    auto root = Identity;
    root[0] = 2;
    root[5] = 3;
    root[10] = 4;
    root[12] = 10;
    root[13] = 20;
    root[14] = 30;
    auto child = Identity;
    child[12] = 1;
    child[13] = 2;
    child[14] = 3;
    auto leaf = Identity;
    leaf[0] = 0;
    leaf[1] = 1;
    leaf[4] = -1;
    leaf[5] = 0;
    leaf[12] = -1;
    matrix(b, 256, root);
    matrix(b, 432, child);
    matrix(b, 608, leaf);
    word(b, 684, 1U << 16);
    word(b, 676, 32);
    word(b, 688, 96);
    chunk(b, 944, "REAL", 80);
    constexpr std::uint32_t relocations[]{104, 200, 216, 232, 808, 824, 840,
                                          868, 900, 908, 932, 940, 676, 688};
    for (std::size_t i = 0; i < std::size(relocations); ++i) {
        word(b, 960 + i * 4, relocations[i]);
    }
    return b;
}

Data geometry() {
    Data b(128);
    word(b, 0, 0x30003);
    word(b, 8, 128);
    text(b, 12, ".3DG");
    chunk(b, 16, "GEOA", 112, true);
    chunk(b, 32, "HASH", 32);
    text(b, 48, "PART");
    word(b, 56, 64);
    chunk(b, 64, "GEOM", 64, true);
    chunk(b, 80, "PRIM", 48);
    word(b, 120, 0x12345678);
    // Deliberately no vertex data: this tests reference resolution only.
    return b;
}

bool near(float left, float right) { return std::abs(left - right) < 0.0001f; }

void valid_scene() {
    auto bytes = scene();
    auto parsed = Scene3DD::parse(bytes);
    check(bool(parsed), "hand-laid-out scene parses");
    if (!parsed) {
        std::cerr << parsed.error().detail << '\n';
        return;
    }
    check(parsed->nodes().size() == 3 && parsed->objects().size() == 1 &&
              parsed->material_banks().size() == 1,
          "all nodes/object/material bank retained");
    const auto& child = parsed->nodes()[1];
    const auto& leaf = parsed->nodes()[2];
    check(child.parent_index == std::size_t{0} &&
              leaf.parent_index == std::size_t{1},
          "explicit parent references resolve");
    check(near(child.composed_transform[12], 12) &&
              near(child.composed_transform[13], 26) &&
              near(child.composed_transform[14], 42),
          "parent scale affects child translation before parent translation");
    check(near(leaf.composed_transform[12], 10) &&
              near(leaf.composed_transform[13], 26) &&
              near(leaf.composed_transform[14], 42),
          "grandchild origin composes through both ancestors");
    check(
        near(leaf.composed_transform[0] + leaf.composed_transform[12], 10) &&
            near(leaf.composed_transform[1] + leaf.composed_transform[13], 29),
        "grandchild point rotation uses row-vector order");
    check(leaf.object_index == std::size_t{0} &&
              leaf.material_bank_index == std::size_t{0},
          "explicit OBJ-hash and MBNK references resolve");
    check(parsed->material_banks()[0].keys[0].opaque_tail[0] == std::byte{0x9f},
          "material opaque bytes retained");
    const auto linked = parsed->resolve_geometry(geometry());
    check(linked && linked->size() == 1 && (*linked)[0].node_index == 2 &&
              (*linked)[0].geometry_group_offset == 64,
          "geometry binds by referenced PART key, not LEAF label");
    check(linked && (*linked)[0].opaque_primitive_bindings[0] == 0x12345678,
          "unknown primitive binding preserved without guessing material "
          "meaning");
    std::fill(bytes.begin(), bytes.end(), std::byte{0});
    check(near(leaf.composed_transform[12], 10) &&
              parsed->objects()[0].key[0] == std::byte{'P'},
          "parsed scene owns output independently of input");

    auto alias = scene();
    word(alias, 500, 32);
    word(alias, 508, 1U << 16);
    word(alias, 512, 96);
    word(alias, 1016, 500);
    word(alias, 1020, 512);
    const auto aliases = Scene3DD::parse(alias);
    const auto resolved =
        aliases
            ? aliases->resolve_geometry(geometry())
            : std::expected<std::vector<Scene3DDGeometryBinding>,
                            Scene3DDError>(std::unexpected(aliases.error()));
    check(resolved && resolved->size() == 2 &&
              (*resolved)[0].geometry_group_offset ==
                  (*resolved)[1].geometry_group_offset,
          "multiple explicit instances can reference one geometry with "
          "different transforms");
}

void errors() {
    const auto valid = scene();
    for (std::size_t length = 0; length < valid.size(); ++length) {
        check(!Scene3DD::parse(std::span(valid).first(length)),
              "all truncated prefixes refused");
    }
    const auto refuses = [&](Data bytes, std::size_t offset,
                             std::uint32_t value, Scene3DDErrorCode code) {
        word(bytes, offset, value);
        const auto result = Scene3DD::parse(bytes);
        check(!result && result.error().code == code,
              "malformed typed reference/layout rejected explicitly");
    };
    refuses(valid, 0, 3, Scene3DDErrorCode::unsupported_version);
    refuses(valid, 8, 16, Scene3DDErrorCode::invalid_chunk);
    refuses(valid, 40, 16, Scene3DDErrorCode::invalid_chunk);
    refuses(valid, 104, 128, Scene3DDErrorCode::invalid_reference);
    refuses(valid, 684, 5U << 16, Scene3DDErrorCode::unsupported_layout);
    refuses(valid, 676, 48, Scene3DDErrorCode::invalid_reference);
    refuses(valid, 688, 112, Scene3DDErrorCode::invalid_reference);
    refuses(valid, 940, 912, Scene3DDErrorCode::invalid_hierarchy);
    refuses(valid, 908, 1024, Scene3DDErrorCode::invalid_reference);
    refuses(valid, 872, 0x789a0001, Scene3DDErrorCode::invalid_hierarchy);
    refuses(valid, 608, 0x7f800000, Scene3DDErrorCode::invalid_transform);
    refuses(valid, 620, std::bit_cast<std::uint32_t>(0.5f),
            Scene3DDErrorCode::unsupported_layout);
    refuses(valid, 1008, 128, Scene3DDErrorCode::invalid_reference);
    auto duplicate = valid;
    std::copy_n(duplicate.begin() + 800, 8, duplicate.begin() + 816);
    check(!Scene3DD::parse(duplicate), "duplicate full hierarchy keys refused");
    auto overflow = valid;
    word(overflow, 256, std::bit_cast<std::uint32_t>(1e30f));
    word(overflow, 432, std::bit_cast<std::uint32_t>(1e30f));
    const auto enormous = Scene3DD::parse(overflow);
    check(!enormous &&
              enormous.error().code == Scene3DDErrorCode::invalid_transform,
          "finite local matrices cannot overflow composed transforms");
    for (unsigned which = 0; which < 7; ++which) {
        Scene3DDLimits limits;
        if (which == 0) {
            limits.max_file_bytes = 100;
        }
        if (which == 1) {
            limits.max_nodes = 2;
        }
        if (which == 2) {
            limits.max_objects = 0;
        }
        if (which == 3) {
            limits.max_material_banks = 0;
        }
        if (which == 4) {
            limits.max_material_keys = 0;
        }
        if (which == 5) {
            limits.max_depth = 2;
        }
        if (which == 6) {
            limits.max_output_bytes = 16;
        }
        const auto result = Scene3DD::parse(valid, limits);
        check(!result &&
                  result.error().code == Scene3DDErrorCode::resource_limit,
              "configured scene budget enforced");
    }
    const auto parsed = Scene3DD::parse(valid);
    if (!parsed) {
        return;
    }
    auto wrongGeometry = geometry();
    text(wrongGeometry, 48, "LEAF");
    const auto wrongKey = parsed->resolve_geometry(wrongGeometry);
    check(!wrongKey &&
              wrongKey.error().code == Scene3DDErrorCode::geometry_mismatch,
          "matching node label cannot replace missing exact object key");
    auto unused = valid;
    word(unused, 676, 0);
    word(unused, 684, 0);
    word(unused, 688, 0);
    const auto noDrawables = Scene3DD::parse(unused);
    const auto absentUnused =
        noDrawables ? noDrawables->resolve_geometry(wrongGeometry)
                    : std::expected<std::vector<Scene3DDGeometryBinding>,
                                    Scene3DDError>(
                          std::unexpected(noDrawables.error()));
    check(!absentUnused &&
              absentUnused.error().code == Scene3DDErrorCode::geometry_mismatch,
          "uninstantiated OBJ keys also require exact geometry pairing");
    auto binaryKey = valid;
    binaryKey[101] =
        std::byte{0x9b}; // Bytes after a NUL remain part of the key.
    const auto binaryScene = Scene3DD::parse(binaryKey);
    check(binaryScene && !binaryScene->resolve_geometry(geometry()),
          "raw key bytes after NUL cannot be replaced by a display-name match");
    auto binaryGeometry = geometry();
    binaryGeometry[53] = std::byte{0x9b};
    check(binaryScene && bool(binaryScene->resolve_geometry(binaryGeometry)),
          "matching all eight binary key bytes restores the explicit binding");
    auto interior = geometry();
    word(interior, 56, 80);
    check(!parsed->resolve_geometry(interior),
          "geometry reference cannot target PRIM instead of GEOM");
    auto two = geometry();
    two.resize(176);
    word(two, 8, 176);
    word(two, 24, 160);
    word(two, 72, 112);
    chunk(two, 128, "PRIM", 48);
    const auto wrongCount = parsed->resolve_geometry(two);
    check(!wrongCount &&
              wrongCount.error().code == Scene3DDErrorCode::geometry_mismatch,
          "paired geometry primitive count must match object declaration");
}

void official(const std::filesystem::path& path) {
    const auto bank = BankArchive::read_file(path);
    check(bool(bank), "official bank validates before scene read");
    if (!bank) {
        return;
    }
    const auto sceneBytes = bank->stored_bytes(0);
    const auto geometryBytes = bank->stored_bytes(1);
    if (!sceneBytes || !geometryBytes) {
        check(false, "explicit official members0/1 exist");
        return;
    }
    const auto parsed = Scene3DD::parse(*sceneBytes);
    check(bool(parsed), "official v2 scene layout and references validate");
    if (!parsed) {
        std::cerr << "SCENE_ERROR " << parsed.error().offset << ' '
                  << parsed.error().detail << '\n';
        return;
    }
    const auto resolved = parsed->resolve_geometry(*geometryBytes);
    check(bool(resolved), "official exact object/geometry pairing validates");
    if (!resolved) {
        std::cerr << "GEOM_ERROR " << resolved.error().offset << ' '
                  << resolved.error().detail << '\n';
        return;
    }
    std::cout << "OFFICIAL " << path.filename().string()
              << " nodes=" << parsed->nodes().size()
              << " objects=" << parsed->objects().size()
              << " banks=" << parsed->material_banks().size()
              << " object_bindings=" << resolved->size() << '\n';
    const auto expected_bindings = path.filename() == "Alfa_GT.bnk" ? 58U
                                   : path.filename() == "300_C.bnk" ? 61U
                                   : path.filename() == "A3_V6.bnk" ? 64U
                                                                    : 0U;
    if (expected_bindings) {
        check(resolved->size() == expected_bindings,
              "all object-entry bindings match independent resource inventory");
    }
    if (path.filename() == "Alfa_GT.bnk") {
        check(parsed->nodes().size() == 97 && parsed->objects().size() == 34 &&
                  parsed->material_banks().size() == 33 &&
                  std::count_if(parsed->nodes().begin(), parsed->nodes().end(),
                                [](const auto& node) {
                                    return !node.object_entries.empty();
                                }) == 56 &&
                  resolved->size() >= 56,
              "official Alfa scene inventory matches structured evidence");
        const auto keyMatches = [](const Scene3DDKey& key,
                                   const char* literal) {
            Scene3DDKey wanted{};
            for (std::size_t i = 0; literal[i] && i < 8; ++i) {
                wanted[i] = std::byte(static_cast<unsigned char>(literal[i]));
            }
            return key == wanted;
        };
        const auto fork = std::find_if(
            parsed->nodes().begin(), parsed->nodes().end(),
            [&](const auto& node) { return keyMatches(node.key, "FORK_FL"); });
        check(fork != parsed->nodes().end() &&
                  near(fork->stored_transform[12], 0.76975f) &&
                  near(fork->stored_transform[13], 0.152931f) &&
                  near(fork->stored_transform[14], 1.27813f),
              "official front-left fork matrix agrees with source bytes");
        const auto body = std::find_if(
            parsed->nodes().begin(), parsed->nodes().end(),
            [&](const auto& node) { return keyMatches(node.key, "BODY_HR"); });
        check(body != parsed->nodes().end() && body->object_index &&
                  parsed->objects()[*body->object_index].primitive_count ==
                      12 &&
                  body->material_bank_index &&
                  parsed->material_banks()[*body->material_bank_index]
                          .keys.size() == 12,
              "official BODY_HR resolves its explicit twelve-part object and "
              "MBNK");
    }
}
} // namespace

int main(int argc, char** argv) {
    valid_scene();
    errors();
    for (int i = 1; i < argc; ++i) {
        official(std::filesystem::path(argv[i]));
    }
    std::cout << checks << " scene checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

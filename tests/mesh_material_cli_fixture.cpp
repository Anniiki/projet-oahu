// Source-owned regression bank. No original asset, executable or process read.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "fixtures/material_textures_fixture.hpp"
#include "fixtures/scene_mesh_fixture.hpp"
#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/geometry_3dg.hpp"
#include "tdur/assets/material_textures.hpp"
#include "tdur/assets/scene_mesh.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>

using namespace tdur::assets;
namespace mf = tdur::fixtures::material;
namespace tf = tdur::fixtures::material_textures;
namespace sf = tdur::fixtures;

namespace {
using Bytes = mf::Bytes;
constexpr Texture2DBKey Black{std::byte{'B'},
                              std::byte{'L'},
                              std::byte{'A'},
                              std::byte{'C'},
                              std::byte{'K'},
                              {},
                              {},
                              {}};
unsigned checks{};

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Bytes scene() {
    const auto original = sf::scene_mesh_scene(true);
    constexpr std::size_t OldReal = 1008;
    auto result = original;
    // Add a second whole MBNK. Relocations list both positions and targets
    // explicitly: translate each independently rather than scanning payloads.
    result.insert(result.begin() + 64, 32, std::byte{});
    mf::put32(result, 8, static_cast<std::uint32_t>(result.size()));
    mf::put32(result, 16 + 8, 80);
    for (std::size_t i = 0; i < 20; ++i) {
        const auto field = mf::get32(original, OldReal + 16 + i * 4);
        if (field == 0) {
            continue;
        }
        const auto translated_field = field >= 64 ? field + 32 : field;
        const auto target = mf::get32(original, field);
        const auto translated_target = target >= 64 ? target + 32 : target;
        mf::put32(result, translated_field, translated_target);
        mf::put32(result, OldReal + 32 + 16 + i * 4, translated_field);
    }
    mf::chunk(result, 64, "MBNK", 32);
    mf::put_key(result, 48, mf::Keys[0]);
    mf::put_key(result, 80, mf::Keys[1]);
    mf::put32(result, 88, UINT32_MAX); // a cache-looking source word, ignored
    // New INS starts are ROOT336, CHILD512 and LEAF688. Matrix starts are
    // INS+16. LEAF's parent is CHILD; make their relative placement obvious.
    sf::scene_matrix(result, 352, sf::SceneIdentity);
    sf::scene_matrix(result, 528, sf::SceneIdentity);
    auto distant = sf::SceneIdentity;
    distant[12] = 1'000'000;
    sf::scene_matrix(result, 704, distant);
    mf::put32(result, 688 + 0x54, 64);
    return result;
}

Bytes geometry() {
    auto result = sf::scene_mesh_geometry(true, true);
    mf::put32(result, 96 + 0x28, 0x0000a5a5);
    mf::put32(result, 288 + 0x28, 0x00005a5a);
    return result;
}

Bytes materials() {
    auto result = tf::fixture();
    for (std::size_t layer = 0; layer < 4; ++layer) {
        tf::set_count(result, layer, 1);
        mf::put_key(result, tf::slot_offset(layer, 0) + 8, Black);
        tf::set_sampler(result, layer, 0, {0, 0, 0, 0});
    }
    return result;
}

Bytes black_texture() {
    Bytes result(112);
    mf::put32(result, 0, 2);
    mf::put32(result, 8, 112);
    mf::put_text(result, 12, ".2DB");
    mf::put_text(result, 16, "BMAP");
    mf::put32(result, 24, 80);
    mf::put32(result, 28, 80);
    mf::put_key(result, 32, Black);
    mf::put32(result, 40, 0x00010001); // width and height: unsigned WORDs
    mf::put32(result, 44, 0x01010001); // one slice; two mip-count bytes of one
    result[48] = std::byte{0x90};
    mf::put32(result, 60, 29);   // observed BGRA8 profile
    result[83] = std::byte{255}; // B,G,R=0; opaque alpha
    mf::chunk(result, 96, "REAL", 16);
    return result;
}

// Bitwise fixture oracle, like bank_archive_test.cpp, independent of the
// production reader's table. KNAB stores this accumulator without final XOR.
std::uint32_t checksum(std::span<const std::byte> bytes) {
    std::uint32_t value = UINT32_MAX;
    for (const auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0U);
        }
    }
    return value;
}

Bytes bank() {
    const std::array members{scene(), geometry(), materials(), black_texture()};
    Bytes names;
    const auto text = [&](std::string_view value) {
        for (const auto c : value) {
            names.push_back(std::byte(static_cast<unsigned char>(c)));
        }
    };
    const auto directory = [&](std::string_view name, unsigned children) {
        names.push_back(std::byte(256U - name.size()));
        names.push_back(std::byte(children));
        text(name);
    };
    directory("C:", 4);
    constexpr std::array extensions{".3dd", ".3dg", ".2dm", ".2db"};
    constexpr std::array stems{"scene", "geometry", "materials", "black"};
    for (std::size_t i = 0; i < 4; ++i) {
        directory(extensions[i], 1);
        const std::string_view name = stems[i];
        names.push_back(std::byte(name.size()));
        text(name);
    }
    names.push_back(std::byte{});
    constexpr std::size_t Records = 72;
    constexpr std::size_t Types = Records + 8 + 5 * 16;
    constexpr std::size_t Names = Types + 8 + 4 * 4;
    const auto order = Names + 8 + names.size();
    const auto data = (order + 8 + 4 + 15) & ~std::size_t{15};
    std::size_t length = data;
    for (const auto& member : members) {
        length += member.size();
    }
    Bytes result(length);
    mf::put32(result, 0, 64);
    mf::put32(result, 8, 0x42414e4b);
    mf::put32(result, 24, static_cast<std::uint32_t>(length));
    mf::put32(result, 32, 4);
    mf::put32(result, 36, 16);
    mf::put32(result, 40, 4);
    mf::put32(result, 44, 2006);
    mf::put32(result, 48, Records);
    mf::put32(result, 52, Types);
    mf::put32(result, 56, Names);
    mf::put32(result, 60, static_cast<std::uint32_t>(order));
    mf::put32(result, 68, static_cast<std::uint32_t>(data));
    mf::put32(result, Records, 5 * 16);
    mf::put32(result, Types, 4 * 4);
    mf::put32(result, Names, static_cast<std::uint32_t>(names.size()));
    mf::put32(result, order, 4);
    std::copy(names.begin(), names.end(), result.begin() + Names + 8);
    auto cursor = data;
    for (std::size_t i = 0; i < members.size(); ++i) {
        mf::put32(result, Records + 8 + i * 16,
                  static_cast<std::uint32_t>(cursor));
        mf::put32(result, Records + 12 + i * 16,
                  static_cast<std::uint32_t>(members[i].size()));
        result[order + 8 + i] = std::byte(i);
        std::copy(members[i].begin(), members[i].end(),
                  result.begin() + cursor);
        cursor += members[i].size();
    }
    mf::put32(result, Records + 8 + 4 * 16, static_cast<std::uint32_t>(cursor));
    for (const auto section : {std::size_t{0}, Records, Types, Names, order}) {
        const auto content =
            std::span(result).subspan(section + 8, mf::get32(result, section));
        mf::put32(result, section + 4, checksum(content));
    }
    return result;
}

void validate(const Bytes& bytes) {
    const auto archive = BankArchive::parse(bytes);
    require(archive && archive->entries().size() == 4,
            "KNAB fixture must have four members");
    const auto scene_bytes = archive->stored_bytes(0);
    const auto geometry_bytes = archive->stored_bytes(1);
    const auto material_bytes = archive->stored_bytes(2);
    const auto texture_bytes = archive->stored_bytes(3);
    require(scene_bytes && geometry_bytes && material_bytes && texture_bytes,
            "all explicit physical member indices must be in bounds");
    const auto parsed_scene = Scene3DD::parse(*scene_bytes);
    const auto parsed_geometry = Geometry3DG::parse(*geometry_bytes);
    const auto parsed_materials = Material2DM::parse(*material_bytes);
    const auto texture = Texture2DBView::parse(*texture_bytes);
    require(bool(parsed_scene),
            "translated scene including every REAL field must validate");
    require(bool(parsed_geometry), "UV geometry must validate");
    require(bool(parsed_materials), "typed material chains must validate");
    require(bool(texture), "synthetic BGRA8 texture must validate");
    require(
        parsed_scene->nodes().size() == 3 &&
            parsed_scene->material_banks().size() == 2,
        "ROOT plus two drawable instances and two independent MBNKs required");
    require(parsed_scene->nodes()[1].material_bank_index == 0U &&
                parsed_scene->nodes()[2].material_bank_index == 1U,
            "A and B instances must select different MBNK records");
    require(
        parsed_scene->material_banks()[0].keys[0].key == mf::Keys[0] &&
            parsed_scene->material_banks()[1].keys[0].key == mf::Keys[1],
        "material identities differing after NUL must survive scene parsing");
    const auto bindings = parsed_scene->resolve_geometry(*geometry_bytes);
    require(bindings && bindings->size() == 2,
            "each instance has one exact paired OBJ");
    for (std::size_t i = 0; i < bindings->size(); ++i) {
        const auto& binding = (*bindings)[i];
        require(binding.node_index == i + 1 &&
                    binding.opaque_primitive_bindings.size() == 1,
                "paired fixture binds only the intended two nodes");
        const auto material = parsed_materials->bind_primitive(
            *parsed_scene, parsed_scene->nodes()[i + 1].material_bank_index,
            binding.opaque_primitive_bindings[0]);
        require(material && material->material_index == i &&
                    material->source_slot == 0,
                "high16 slot0 selects A/B by raw8, ignoring nonzero low16");
    }
    const std::array<SceneMeshSelection, 1> a{{{1, 0}}};
    const std::array<SceneMeshSelection, 2> both{{{1, 0}, {2, 0}}};
    const auto first =
        SceneMesh::build_selected(*parsed_scene, *geometry_bytes, a);
    const auto placed =
        SceneMesh::build_selected(*parsed_scene, *geometry_bytes, both);
    require(first && first->instances().size() == 1 && first->bounds(),
            "A alone must build a drawable UV quad");
    require(placed && placed->instances().size() == 2 && placed->bounds(),
            "both instances must build before material filtering");
    require(first->bounds()->minimum == std::array<float, 3>{0, 0, 0} &&
                first->bounds()->maximum == std::array<float, 3>{1, 1, 0},
            "A's exact bounds must be one unit square at the origin");
    require(placed->bounds()->minimum == std::array<float, 3>{0, 0, 0} &&
                placed->bounds()->maximum ==
                    std::array<float, 3>{1'000'001, 1, 0},
            "B must dominate unfiltered bounds by one million units");
    require(texture->resource_key() == Black && texture->width() == 1 &&
                texture->height() == 1 && texture->mip_count() == 1,
            "internal BMAP key and one-pixel mip must be exact");
    const auto image = texture->decode_rgba8();
    require(image && image->pixels == Bytes{std::byte{0}, std::byte{0},
                                            std::byte{0}, std::byte{255}},
            "the requested texture must decode to opaque RGBA black");
    auto textures = MaterialTextureBindings::from_materials(*parsed_materials);
    require(bool(textures),
            "every material must be in the supported type2 subset");
    const std::array resources{MaterialTextureResource{Black, 0}};
    const auto stores = textures->rebind(resources);
    require(stores && *stores == 4,
            "all four declared LAY stage0 keys must resolve");
    for (std::size_t material = 0; material < 4; ++material) {
        const auto plan = textures->plan(material);
        require(
            plan && plan->stage_count == 1 &&
                plan->stages[0].action == MaterialTextureAction::bind_cached &&
                plan->stages[0].texture_token == std::uint64_t{0},
            "every material must select known token0 with one active stage");
    }
}

void write_new(const std::filesystem::path& path, const Bytes& bytes) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("fixture output must be a new writable file");
    }
    DWORD written{};
    const bool success = WriteFile(handle, bytes.data(), DWORD(bytes.size()),
                                   &written, nullptr) != FALSE;
    const bool flushed = success && FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    require(success && flushed && written == bytes.size(),
            "new fixture must be completely written and flushed");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    const bool self_test =
        argc == 2 && std::wstring_view(argv[1]) == L"--self-test";
    const bool output = argc == 3 && std::wstring_view(argv[1]) == L"--output";
    if (!self_test && !output) {
        std::cerr << "usage: mesh_material_cli_fixture --self-test | --output "
                     "NEWFILE\n";
        return 2;
    }
    try {
        const auto bytes = bank();
        validate(bytes);
        if (output) {
            write_new(argv[2], bytes);
        }
        std::cout << "{\"scope\":\"source-owned-black-material-cli-fixture\","
                     "\"game_executed\":false,\"members\":4,\"bytes\":"
                  << bytes.size() << ",\"validation_checks\":" << checks
                  << ",\"node_a\":1,\"node_b\":2,"
                     "\"material_a_hex\":\"4100000000000000\","
                     "\"material_b_hex\":\"4100000000000080\","
                     "\"texture_key_hex\":\"424c41434b000000\","
                     "\"file_created\":"
                  << (output ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mesh material fixture: " << error.what() << '\n';
        return 1;
    }
}

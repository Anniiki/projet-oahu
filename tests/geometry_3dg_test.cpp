#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/geometry_3dg.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using tdur::assets::Geometry3DG;
using tdur::assets::Geometry3DGErrorCode;
using Bytes = std::vector<std::byte>;
std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view description) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 30) {
            std::cerr << "FAIL: " << description << '\n';
        }
    }
}

std::uint32_t fourcc(std::string_view text) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        result |=
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]))
            << (i * 8U);
    }
    return result;
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = std::byte((value >> (i * 8U)) & 255U);
    }
}

void putf(Bytes& bytes, std::size_t offset, float value) {
    put32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void block(Bytes& bytes, std::size_t offset, std::string_view id,
           std::size_t size, bool container = false) {
    put32(bytes, offset, fourcc(id));
    put32(bytes, offset + 8, static_cast<std::uint32_t>(size));
    put32(bytes, offset + 12,
          container ? 16U : static_cast<std::uint32_t>(size));
}

struct Fixture {
    Bytes bytes;
    std::size_t geom = 64;
    std::size_t prim = 80;
    std::size_t vba = 256;
    std::size_t vb{};
    std::size_t iba{};
    std::size_t ib{};
    std::size_t mora{};
    std::size_t uv_offset{};
};

Fixture fixture(std::vector<std::uint16_t> strip = {0, 1, 2, 3}, bool uv = true,
                bool normal = false, bool color = false) {
    Fixture f;
    f.vb = f.vba + 16;
    const std::size_t positions_size = 48;
    f.uv_offset = positions_size + (normal ? 16U : 0U) + (color ? 16U : 0U);
    const auto vb_size =
        (80 + f.uv_offset + (uv ? 32U : 0U) + 15) & ~std::size_t{15};
    f.iba = f.vb + vb_size;
    f.ib = f.iba + 16;
    const auto ib_size = (80 + strip.size() * 2 + 15) & ~std::size_t{15};
    f.mora = f.ib + ib_size;
    f.bytes.resize(f.mora + 32);
    auto& b = f.bytes;
    put32(b, 0, 0x00030003U);
    put32(b, 8, static_cast<std::uint32_t>(b.size()));
    put32(b, 12, fourcc(".3DG"));
    block(b, 16, "GEOA", f.vba - 16, true);
    block(b, 32, "HASH", 32);
    const std::string_view name = "BODY";
    for (std::size_t i = 0; i < name.size(); ++i) {
        b[48 + i] = std::byte(static_cast<unsigned char>(name[i]));
    }
    put32(b, 56, static_cast<std::uint32_t>(f.geom));
    block(b, f.geom, "GEOM", 192, true);
    block(b, f.prim, "PRIM", 176);
    put32(b, f.prim + 20, 4);
    put32(b, f.prim + 24, static_cast<std::uint32_t>(f.ib));
    put32(b, f.prim + 28, static_cast<std::uint32_t>(f.vb));
    put32(b, f.prim + 40, 0xaabbccddU);
    put32(b, f.prim + 44, 1);
    put32(b, f.prim + 52, 4);
    put32(b, f.prim + 60, static_cast<std::uint32_t>(strip.size()));
    put32(b, f.prim + 64, 1);
    put32(b, f.prim + 68, normal ? 0x901U : 0x900U);
    put32(b, f.prim + 72, color ? 0x201U : 0x200U);
    put32(b, f.prim + 80, uv ? 0xc01U : 0xc00U);
    put32(b, f.prim + 84, 0x1600);
    put32(b, f.prim + 88, 0x900);
    put32(b, f.prim + 92, 0x1100);
    std::size_t stream = 1;
    std::size_t stream_offset = positions_size;
    if (normal) {
        put32(b, f.prim + 0x88 + stream++ * 4,
              static_cast<std::uint32_t>(stream_offset));
        stream_offset += 16;
    }
    if (color) {
        put32(b, f.prim + 0x88 + stream++ * 4,
              static_cast<std::uint32_t>(stream_offset));
        stream_offset += 16;
    }
    if (uv) {
        put32(b, f.prim + 0x88 + stream * 4,
              static_cast<std::uint32_t>(stream_offset));
    }
    block(b, f.vba, "VBA.", 16 + vb_size, true);
    block(b, f.vb, "DXVB", vb_size);
    put32(b, f.vb + 16, fourcc("GUN "));
    put32(b, f.vb + 20, fourcc("TIME"));
    block(b, f.iba, "IBA.", 16 + ib_size, true);
    block(b, f.ib, "DXIB", ib_size);
    put32(b, f.ib + 16, fourcc("GUN "));
    put32(b, f.ib + 20, fourcc("TIME"));
    block(b, f.mora, "MORA", 16);
    block(b, f.mora + 16, "REAL", 16);
    const std::array<std::array<float, 3>, 4> positions{
        std::array<float, 3>{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    const std::array<std::array<float, 2>, 4> coordinates{
        std::array<float, 2>{-1, 2}, {3, 4}, {5, 6}, {7, 8}};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            putf(b, f.vb + 80 + i * 12 + axis * 4, positions[i][axis]);
        }
        if (uv) {
            for (std::size_t axis = 0; axis < 2; ++axis) {
                putf(b, f.vb + 80 + f.uv_offset + i * 8 + axis * 4,
                     coordinates[i][axis]);
            }
        }
    }
    for (std::size_t i = 0; i < strip.size(); ++i) {
        b[f.ib + 80 + i * 2] = std::byte(strip[i] & 255U);
        b[f.ib + 81 + i * 2] = std::byte(strip[i] >> 8U);
    }
    return f;
}

void valid_geometry() {
    auto f = fixture();
    const auto geometry = Geometry3DG::parse(f.bytes);
    check(geometry && geometry->groups().size() == 1,
          "structured fixture has one named group");
    if (!geometry) {
        return;
    }
    const auto& group = geometry->groups()[0];
    check(group.name_bytes == "BODY" && group.source_offset == f.geom,
          "HASH resolves exact GEOM start and name");
    check(group.primitives.size() == 1, "PRIM child count");
    const auto& mesh = group.primitives[0];
    check(mesh.positions.size() == 4 && mesh.texcoords.size() == 4,
          "positions and supported float2 UV decoded");
    check(mesh.positions[3] == std::array<float, 3>{1, 1, 0},
          "positions retain local coordinates");
    check(mesh.texcoords[0] == std::array<float, 2>{-1, 2} &&
              mesh.texcoords[3] == std::array<float, 2>{7, 8},
          "UV values are not clamped, flipped or normalized");
    check(mesh.triangle_indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3},
          "four-index strip produces independent expected triangles");
    check(mesh.bounds.minimum == std::array<float, 3>{0, 0, 0} &&
              mesh.bounds.maximum == std::array<float, 3>{1, 1, 0},
          "bounds computed from actual positions");
    check(mesh.opaque_binding_word == 0xaabbccddU,
          "binding word retained without interpreting material identity");
    putf(f.bytes, f.vb + 80, 9);
    check(mesh.positions[0][0] == 0, "decoded geometry owns its storage");
    for (const bool normal : {false, true}) {
        for (const bool color : {false, true}) {
            auto streams = fixture({0, 1, 2, 3}, true, normal, color);
            const auto parsed = Geometry3DG::parse(streams.bytes);
            check(parsed && parsed->groups()[0].primitives[0].texcoords[3] ==
                                std::array<float, 2>{7, 8},
                  "explicit UV offset follows present normal/color streams");
        }
    }
    auto without_uv = fixture({0, 1, 2}, false);
    const auto positions_only = Geometry3DG::parse(without_uv.bytes);
    check(positions_only &&
              positions_only->groups()[0].primitives[0].texcoords.empty(),
          "absent UV stream stays absent");
    auto unknown_uv = fixture();
    put32(unknown_uv.bytes, unknown_uv.prim + 80, 0x1001);
    const auto unknown = Geometry3DG::parse(unknown_uv.bytes);
    check(unknown && unknown->groups()[0].primitives[0].texcoords.empty() &&
              unknown->groups()[0].primitives[0].positions.size() == 4,
          "unproven UV declaration does not create guessed coordinates");
}

void strip_edges() {
    const std::vector<
        std::pair<std::vector<std::uint16_t>, std::vector<std::uint32_t>>>
        cases{{{}, {}},
              {{0}, {}},
              {{0, 1}, {}},
              {{0, 1, 2}, {0, 1, 2}},
              {{0, 0, 1, 2, 3}, {1, 0, 2, 1, 2, 3}},
              {{0, 1, 1, 2, 3}, {1, 2, 3}},
              {{0xffff, 0xffff, 0, 1, 2, 0xffff, 3, 2, 1}, {0, 1, 2, 3, 2, 1}},
              {{0, 1, 2, 3, 0xffff, 0, 1, 2}, {0, 1, 2, 2, 1, 3, 0, 1, 2}},
              {{0, 1, 0xffff, 2, 3}, {}}};
    for (const auto& [source, expected] : cases) {
        const auto f = fixture(source);
        const auto geometry = Geometry3DG::parse(f.bytes);
        check(geometry &&
                  geometry->groups()[0].primitives[0].triangle_indices ==
                      expected,
              "strip parity, degenerates and restart boundaries");
    }
}

void expect_error(Bytes bytes, Geometry3DGErrorCode code,
                  std::string_view description) {
    const auto decoded = Geometry3DG::parse(bytes);
    check(!decoded && decoded.error().code == code, description);
}

void malformed_geometry() {
    const auto original = fixture();
    for (std::size_t size = 0; size < original.bytes.size(); ++size) {
        auto bytes = original.bytes;
        bytes.resize(size);
        check(!Geometry3DG::parse(bytes), "every truncated file prefix fails");
    }
    auto b = original.bytes;
    put32(b, 0, 0x00020002U);
    expect_error(std::move(b), Geometry3DGErrorCode::unsupported_version,
                 "other file versions are explicit");
    b = original.bytes;
    put32(b, 8, 0xffffffffU);
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_chunk,
                 "physical file length checked");
    b = original.bytes;
    put32(b, 24, 0xfffffff0U);
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_chunk,
                 "root length cannot wrap outside file");
    b = original.bytes;
    put32(b, 28, 32);
    expect_error(std::move(b), Geometry3DGErrorCode::unsupported_layout,
                 "container header length checked");
    b = original.bytes;
    put32(b, 56, static_cast<std::uint32_t>(original.geom + 16));
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_reference,
                 "HASH must target GEOM start, not PRIM");
    b = original.bytes;
    put32(b, original.prim + 24, static_cast<std::uint32_t>(original.vb));
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_reference,
                 "index reference must target DXIB, not DXVB");
    b = original.bytes;
    put32(b, original.prim + 28, static_cast<std::uint32_t>(original.vb + 4));
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_reference,
                 "vertex reference must target a declared chunk start");
    for (const auto offset : {std::size_t{20}, std::size_t{48}, std::size_t{56},
                              std::size_t{64}, std::size_t{0x88}}) {
        b = original.bytes;
        put32(b, original.prim + offset, 3);
        expect_error(std::move(b), Geometry3DGErrorCode::unsupported_layout,
                     "unproven topology/base/position declaration rejected");
    }
    b = original.bytes;
    put32(b, original.prim + 52, 0xffffffffU);
    expect_error(std::move(b), Geometry3DGErrorCode::resource_limit,
                 "vertex count limit checked before multiplication");
    b = original.bytes;
    put32(b, original.prim + 60, 0xffffffffU);
    expect_error(std::move(b), Geometry3DGErrorCode::resource_limit,
                 "index count limit checked before multiplication");
    auto only_positions = fixture({0, 1, 2}, false);
    put32(only_positions.bytes, only_positions.prim + 52, 5);
    expect_error(std::move(only_positions.bytes),
                 Geometry3DGErrorCode::invalid_reference,
                 "vertex count cannot borrow bytes from the next chunk");
    b = original.bytes;
    put32(b, original.prim + 60, 1000);
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_reference,
                 "index count must fit declared DXIB");
    auto bad_index = fixture({0, 1, 4});
    expect_error(std::move(bad_index.bytes),
                 Geometry3DGErrorCode::invalid_indices,
                 "index cannot exceed position count");
    b = original.bytes;
    put32(b, original.vb + 80, 0x7fc00000U);
    expect_error(std::move(b), Geometry3DGErrorCode::non_finite_vertex,
                 "NaN position rejected");
    b = original.bytes;
    put32(b, original.vb + 80 + original.uv_offset, 0x7f800000U);
    expect_error(std::move(b), Geometry3DGErrorCode::non_finite_vertex,
                 "infinite UV rejected");
    b = original.bytes;
    put32(b, original.prim + 0x8c, 0xfffffff0U);
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_reference,
                 "UV offset cannot wrap outside DXVB");
    b = original.bytes;
    put32(b, original.prim + 0x8c, 4);
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_reference,
                 "UV stream cannot overlap positions");
    b = original.bytes;
    put32(b, original.vb + 16, 0);
    expect_error(std::move(b), Geometry3DGErrorCode::unsupported_layout,
                 "GPU buffer prefix must match proven80-byte layout");
    b = original.bytes;
    put32(b, original.mora, fourcc("GEOA"));
    expect_error(std::move(b), Geometry3DGErrorCode::invalid_chunk,
                 "duplicate root container rejected");
    tdur::assets::Geometry3DGLimits limits;
    limits.max_file_bytes = 1;
    check(!Geometry3DG::parse(original.bytes, limits), "file budget enforced");
    limits = {};
    limits.max_groups = 0;
    check(!Geometry3DG::parse(original.bytes, limits), "group budget enforced");
    limits = {};
    limits.max_primitives = 0;
    check(!Geometry3DG::parse(original.bytes, limits),
          "primitive/buffer budget enforced");
    limits = {};
    limits.max_vertices_per_primitive = 3;
    check(!Geometry3DG::parse(original.bytes, limits),
          "vertex budget enforced");
    limits = {};
    limits.max_strip_indices_per_primitive = 3;
    check(!Geometry3DG::parse(original.bytes, limits), "index budget enforced");
    limits = {};
    limits.max_output_bytes = 1;
    check(!Geometry3DG::parse(original.bytes, limits),
          "output budget enforced");
}

struct Corpus {
    std::size_t banks{};
    std::size_t members{};
    std::size_t accepted{};
    std::size_t groups{};
    std::size_t primitives{};
    std::size_t positions{};
    std::size_t triangles{};
    std::size_t uv_coordinates{};
    std::size_t restart_indices{};
    std::size_t max_primitive_vertices{};
    std::map<Geometry3DGErrorCode, std::size_t> rejected;
    std::map<std::string, std::size_t> reasons;
    std::size_t printed_errors{};
};

void official_bank(const std::filesystem::path& path, bool verbose,
                   Corpus& corpus) {
    const auto bank = tdur::assets::BankArchive::read_file(path);
    check(bank.has_value(), "official source bank parses");
    if (!bank) {
        return;
    }
    ++corpus.banks;
    for (std::size_t i = 0; i < bank->entries().size(); ++i) {
        auto name = bank->entries()[i].name;
        for (auto& c : name) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (!name.ends_with(".3dg")) {
            continue;
        }
        ++corpus.members;
        const auto stored = bank->stored_bytes(i);
        const auto geometry = Geometry3DG::parse(*stored);
        if (!geometry) {
            ++corpus.rejected[geometry.error().code];
            const auto count = ++corpus.reasons[geometry.error().detail];
            if (verbose || (count == 1 && corpus.printed_errors++ < 20)) {
                std::cout << "REJECTED " << path.filename().string() << '/'
                          << name
                          << " code=" << static_cast<int>(geometry.error().code)
                          << " offset=" << geometry.error().offset << ' '
                          << geometry.error().detail << '\n';
            }
            continue;
        }
        ++corpus.accepted;
        std::size_t model_primitives = 0;
        for (const auto& group : geometry->groups()) {
            ++corpus.groups;
            std::size_t positions = 0;
            std::size_t triangles = 0;
            for (const auto& mesh : group.primitives) {
                ++corpus.primitives;
                ++model_primitives;
                positions += mesh.positions.size();
                triangles += mesh.triangle_indices.size() / 3;
                corpus.uv_coordinates += mesh.texcoords.size();
                corpus.max_primitive_vertices = std::max(
                    corpus.max_primitive_vertices, mesh.positions.size());
                const auto p = static_cast<std::size_t>(mesh.source_offset);
                std::uint32_t source_indices = 0;
                for (std::size_t q = 0; q < 4; ++q) {
                    source_indices |=
                        std::to_integer<std::uint32_t>((*stored)[p + 60 + q])
                        << (q * 8U);
                }
                for (std::size_t q = 0; q < source_indices; ++q) {
                    const auto index_offset =
                        mesh.index_buffer_offset + std::size_t{80} + q * 2;
                    if ((*stored)[index_offset] == std::byte{0xff} &&
                        (*stored)[index_offset + 1] == std::byte{0xff}) {
                        ++corpus.restart_indices;
                    }
                }
                check(mesh.triangle_indices.size() % 3 == 0 &&
                          (mesh.texcoords.empty() ||
                           mesh.texcoords.size() == mesh.positions.size()),
                      "official canonical vertex/index arrays are complete");
                check(
                    std::all_of(mesh.triangle_indices.begin(),
                                mesh.triangle_indices.end(),
                                [&mesh](std::uint32_t index) {
                                    return index < mesh.positions.size();
                                }),
                    "every emitted official index addresses a decoded vertex");
            }
            corpus.positions += positions;
            corpus.triangles += triangles;
            if (verbose) {
                std::cout << "GROUP " << group.name_bytes
                          << " primitives=" << group.primitives.size()
                          << " positions=" << positions
                          << " triangles=" << triangles << '\n';
            }
            if (path.filename() == "Alfa_GT.bnk" &&
                group.name_bytes == "BODY_HR") {
                check(positions == 3781 && triangles == 4034,
                      "official Alfa BODY_HR matches observed vertex/triangle "
                      "counts");
            }
        }
        if (path.filename() == "Alfa_GT.bnk") {
            check(geometry->groups().size() == 34 && model_primitives == 66,
                  "official Alfa has34HASH groups and66PRIM children");
        }
    }
}

void official_root(const std::filesystem::path& root, Corpus& corpus) {
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            const auto extension = iterator->path().extension().string();
            if (extension == ".bnk" || extension == ".BNK") {
                official_bank(iterator->path(), false, corpus);
            }
        }
        if (!error) {
            iterator.increment(error);
        }
    }
    check(!error && corpus.banks != 0, "official corpus traversal completes");
}

} // namespace

int main(int argc, char* argv[]) {
    valid_geometry();
    strip_edges();
    malformed_geometry();
    Corpus corpus;
    if (argc == 3 && std::string_view(argv[1]) == "--official-root") {
        official_root(std::filesystem::path(argv[2]), corpus);
    } else {
        for (int i = 1; i < argc; ++i) {
            official_bank(std::filesystem::path(argv[i]), true, corpus);
        }
    }
    if (corpus.banks != 0) {
        std::cout << "banks=" << corpus.banks
                  << " 3dg_members=" << corpus.members
                  << " accepted=" << corpus.accepted
                  << " groups=" << corpus.groups
                  << " primitives=" << corpus.primitives
                  << " positions=" << corpus.positions
                  << " triangles=" << corpus.triangles
                  << " uv_coordinates=" << corpus.uv_coordinates
                  << " restart_indices=" << corpus.restart_indices
                  << " max_primitive_vertices=" << corpus.max_primitive_vertices
                  << '\n';
        for (const auto& [code, count] : corpus.rejected) {
            std::cout << "rejected_code" << static_cast<int>(code) << '='
                      << count << '\n';
        }
        for (const auto& [reason, count] : corpus.reasons) {
            std::cout << "REASON " << count << ' ' << reason << '\n';
        }
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

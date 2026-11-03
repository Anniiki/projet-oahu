#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace tdur::fixtures {
using Data = std::vector<std::byte>;
inline constexpr std::array<float, 16> SceneIdentity{1, 0, 0, 0, 0, 1, 0, 0,
                                                     0, 0, 1, 0, 0, 0, 0, 1};

inline void scene_word(Data& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        data.at(offset + i) = std::byte((value >> (i * 8)) & 255);
    }
}
inline void scene_number(Data& data, std::size_t offset, float value) {
    scene_word(data, offset, std::bit_cast<std::uint32_t>(value));
}
inline void scene_text(Data& data, std::size_t offset, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        data.at(offset + i) = std::byte(static_cast<unsigned char>(text[i]));
    }
}
inline void scene_chunk(Data& data, std::size_t offset, std::string_view tag,
                        std::uint32_t size, bool container = false) {
    scene_text(data, offset, tag);
    scene_word(data, offset + 8, size);
    scene_word(data, offset + 12, container ? 16 : size);
}
inline void scene_matrix(Data& data, std::size_t offset,
                         const std::array<float, 16>& matrix) {
    for (std::size_t i = 0; i < 16; ++i) {
        scene_number(data, offset + i * 4, matrix[i]);
    }
}

// Independently hand-laid-out 3DD. ROOT has no geometry. CHILD and LEAF share
// PART, which deliberately has a different key from either instance label.
inline Data scene_mesh_scene(bool distinct_objects = false) {
    const std::uint32_t extra = distinct_objects ? 64 : 0;
    const std::uint32_t real_size = distinct_objects ? 96 : 80;
    Data data(944 + extra + real_size);
    scene_word(data, 0, 2);
    scene_word(data, 8, static_cast<std::uint32_t>(data.size()));
    scene_text(data, 12, ".3DD");
    scene_chunk(data, 16, "MBNA", 48, true);
    scene_chunk(data, 32, "MBNK", 32);
    scene_text(data, 48, "MAT_A");
    data[56] = std::byte{0x9f};
    scene_chunk(data, 64, "OBJA", 96 + extra, true);
    scene_chunk(data, 80, "HASH", distinct_objects ? 48 : 32);
    scene_text(data, 96, "PART");
    const std::uint32_t first_object = distinct_objects ? 128 : 112;
    scene_word(data, 104, first_object);
    scene_chunk(data, first_object, "OBJ.", 48);
    scene_word(data, first_object + 28, 1);
    if (distinct_objects) {
        scene_text(data, 112, "ALT");
        scene_word(data, 120, 176);
        scene_chunk(data, 176, "OBJ.", 48);
        scene_word(data, 204, 1);
    }
    scene_chunk(data, 160 + extra, "INSA", 608, true);
    scene_chunk(data, 176 + extra, "HASH", 64);
    scene_chunk(data, 768 + extra, "HIEA", 176, true);
    scene_chunk(data, 784 + extra, "HASH", 64);
    constexpr std::string_view labels[]{"ROOT", "CHILD", "LEAF"};
    const std::uint32_t instances[]{240 + extra, 416 + extra, 592 + extra};
    const std::uint32_t hierarchy[]{848 + extra, 880 + extra, 912 + extra};
    for (std::size_t i = 0; i < 3; ++i) {
        scene_text(data, 192 + extra + i * 16, labels[i]);
        scene_word(data, 200 + extra + i * 16, instances[i]);
        scene_chunk(data, instances[i], "INS.", 176);
        scene_text(data, 800 + extra + i * 16, labels[i]);
        scene_word(data, 808 + extra + i * 16, hierarchy[i]);
        scene_chunk(data, hierarchy[i], "HIE.", 32);
        scene_word(data, hierarchy[i] + 20, instances[i]);
        scene_word(data, hierarchy[i] + 24, std::uint32_t(2 - i));
        scene_word(data, hierarchy[i] + 28, i ? hierarchy[i - 1] : 0);
        if (i) {
            scene_word(data, instances[i] + 0x5c, 1U << 16);
            scene_word(data, instances[i] + 0x54, 32);
            scene_word(data, instances[i] + 0x60,
                       distinct_objects && i == 2 ? 112 : 96);
        }
    }
    auto root = SceneIdentity;
    root[0] = 2;
    root[5] = 3;
    root[10] = 4;
    root[12] = 10;
    root[13] = 20;
    root[14] = 30;
    auto child = SceneIdentity;
    child[12] = 1;
    child[13] = 2;
    child[14] = 3;
    auto leaf = SceneIdentity;
    leaf[0] = 0;
    leaf[1] = 1;
    leaf[4] = -1;
    leaf[5] = 0;
    leaf[12] = -1;
    scene_matrix(data, 256 + extra, root);
    scene_matrix(data, 432 + extra, child);
    scene_matrix(data, 608 + extra, leaf);
    scene_chunk(data, 944 + extra, "REAL", real_size);
    const std::vector<std::uint32_t> relocations{
        104,         200 + extra, 216 + extra, 232 + extra,
        808 + extra, 824 + extra, 840 + extra, 868 + extra,
        900 + extra, 908 + extra, 932 + extra, 940 + extra,
        500 + extra, 512 + extra, 676 + extra, 688 + extra};
    for (std::size_t i = 0; i < relocations.size(); ++i) {
        scene_word(data, 960 + extra + i * 4, relocations[i]);
    }
    if (distinct_objects) {
        scene_word(data, 960 + extra + relocations.size() * 4, 120);
    }
    return data;
}

// A unit square in one known strip, one GEOM at64, one PRIM at80. Layout is
// hand-written and independent of SceneMesh or either parser's serializers.
inline Data scene_mesh_geometry(bool uv = false,
                                bool distinct_objects = false) {
    const std::uint32_t vba = distinct_objects ? 464 : 256;
    const std::uint32_t vb = vba + 16;
    const std::uint32_t vb_size = uv ? 160 : 128;
    const std::uint32_t iba = vb + vb_size;
    const std::uint32_t ib = iba + 16;
    const std::uint32_t mora = ib + 96;
    Data data(mora + 32);
    scene_word(data, 0, 0x30003);
    scene_word(data, 8, static_cast<std::uint32_t>(data.size()));
    scene_text(data, 12, ".3DG");
    scene_chunk(data, 16, "GEOA", vba - 16, true);
    scene_chunk(data, 32, "HASH", distinct_objects ? 48 : 32);
    scene_text(data, 48, "PART");
    const std::uint32_t first_group = distinct_objects ? 80 : 64;
    scene_word(data, 56, first_group);
    if (distinct_objects) {
        scene_text(data, 64, "ALT");
        scene_word(data, 72, 272);
    }
    for (std::uint32_t i = 0; i < (distinct_objects ? 2U : 1U); ++i) {
        const auto group = i ? 272 : first_group;
        const auto primitive = group + 16;
        scene_chunk(data, group, "GEOM", 192, true);
        scene_chunk(data, primitive, "PRIM", 176);
        scene_word(data, primitive + 20, 4);
        scene_word(data, primitive + 24, ib);
        scene_word(data, primitive + 28, vb);
        scene_word(data, primitive + 40, 0x12345678 + i);
        scene_word(data, primitive + 44, 1);
        scene_word(data, primitive + 52, 4);
        scene_word(data, primitive + 60, 4);
        scene_word(data, primitive + 64, 1);
        scene_word(data, primitive + 68, 0x900);
        scene_word(data, primitive + 72, 0x200);
        scene_word(data, primitive + 80, uv ? 0xc01 : 0xc00);
        scene_word(data, primitive + 84, 0x1600);
        scene_word(data, primitive + 88, 0x900);
        scene_word(data, primitive + 92, 0x1100);
        if (uv)
            scene_word(data, primitive + 140, 48);
    }
    scene_chunk(data, vba, "VBA.", 16 + vb_size, true);
    scene_chunk(data, vb, "DXVB", vb_size);
    scene_text(data, vb + 16, "GUN TIME");
    scene_chunk(data, iba, "IBA.", 112, true);
    scene_chunk(data, ib, "DXIB", 96);
    scene_text(data, ib + 16, "GUN TIME");
    scene_chunk(data, mora, "MORA", 16);
    scene_chunk(data, mora + 16, "REAL", 16);
    constexpr std::array<std::array<float, 3>, 4> points{
        std::array<float, 3>{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            scene_number(data, vb + 80 + i * 12 + axis * 4, points[i][axis]);
        }
        data[ib + 80 + i * 2] = std::byte(i);
        if (uv) {
            scene_number(data, vb + 128 + i * 8, float(i) - 1);
            scene_number(data, vb + 132 + i * 8, float(i) + 2);
        }
    }
    return data;
}
inline Data scene_mesh_variants() {
    auto data = scene_mesh_scene(true);
    scene_word(data, 480 + 0x5c, 2U << 16);
    scene_word(data, 480 + 0x64, 20);
    scene_word(data, 480 + 0x74, 112);
    scene_word(data, 480 + 0x78, 400);
    scene_word(data, 480 + 0x84, 0x11223344);
    scene_word(data, 1092, 480 + 0x74);
    return data;
}
} // namespace tdur::fixtures

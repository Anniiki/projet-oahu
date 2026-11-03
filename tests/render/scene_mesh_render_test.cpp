#include "../fixtures/scene_mesh_fixture.hpp"
#include "tdur/assets/scene_mesh.hpp"
#include "tdur/render/d3d12/mesh_viewer.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace tdur::assets;
using namespace tdur::fixtures;
using namespace tdur::render::d3d12;

namespace {
constexpr std::array<float, 16> Clip{0.25f, 0, 0, 0, 0,      0.5f,    0,    0,
                                     0,     0, 0, 0, -2.75f, -13.75f, 0.5f, 1};

std::array<float, 16> clip_matrix(const Scene3DDMatrix& matrix) {
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            for (std::size_t k = 0; k < 4; ++k) {
                result[row * 4 + col] +=
                    matrix[row * 4 + k] * Clip[k * 4 + col];
            }
        }
    }
    return result;
}

MeshReadback render_packed(MeshViewer& viewer, const SceneMesh& mesh) {
    const auto* packed = &mesh;
    std::vector<MeshVertex> vertices;
    for (const auto& vertex : packed->vertices()) {
        vertices.push_back({vertex.position, {1, 0, 0, 1}});
    }
    for (const auto& group : packed->groups()) {
        if (group.name_bytes == "ALT") {
            for (std::size_t i = group.first_vertex;
                 i < std::size_t(group.first_vertex) + group.vertex_count;
                 ++i) {
                vertices[i].color = {0, 0, 1, 1};
            }
        }
    }
    viewer.upload(vertices, packed->indices());
    std::vector<MeshDraw> draws;
    for (const auto& instance : packed->instances()) {
        const auto& group = packed->groups()[instance.group_index];
        draws.push_back({group.first_index, group.index_count,
                         clip_matrix(instance.composed_transform)});
    }
    return viewer.render_readback(128, 128, draws);
}

MeshReadback render(MeshViewer& viewer, bool distinct,
                    std::span<const std::size_t> selection) {
    const auto scene = Scene3DD::parse(scene_mesh_scene(distinct));
    if (!scene)
        throw std::runtime_error(scene.error().detail);
    const auto packed = SceneMesh::build(
        *scene, scene_mesh_geometry(false, distinct), selection);
    if (!packed)
        throw std::runtime_error(packed.error().detail);
    return render_packed(viewer, *packed);
}

bool pixel(const MeshReadback& image, uint32_t x, uint32_t y,
           std::array<unsigned, 4> expected) {
    for (std::size_t channel = 0; channel < 4; ++channel) {
        if (std::to_integer<unsigned>(
                image.pixels[(std::size_t(y) * 128 + x) * 4 + channel]) !=
            expected[channel]) {
            return false;
        }
    }
    return true;
}
std::size_t coverage(const MeshReadback& image) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        count += image.pixels[i] != std::byte{0} ||
                 image.pixels[i + 1] != std::byte{0} ||
                 image.pixels[i + 2] != std::byte{0};
    }
    return count;
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool warp = false;
        if (argc == 2 && std::string_view(argv[1]) == "--warp")
            warp = true;
        else if (argc != 1)
            throw std::invalid_argument("Expected optional --warp");
        NativeDevice native;
        native.initialize(warp, false);
        MeshViewer viewer(native);
        const std::array<std::size_t, 2> forward{1, 2};
        const std::array<std::size_t, 2> reverse{2, 1};
        const auto first = render(viewer, true, forward);
        if (!pixel(first, 32, 64, {0, 0, 255, 255}) ||
            !pixel(first, 96, 64, {255, 0, 0, 255}) ||
            !pixel(first, 64, 64, {0, 0, 0, 255}) || coverage(first) != 6144) {
            throw std::runtime_error("3DD ancestry/3DG ranges disagree with "
                                     "independent placement pixels");
        }
        if (render(viewer, true, reverse).pixels != first.pixels) {
            throw std::runtime_error("Reordering selected nodes changes "
                                     "colors, indices or placement");
        }
        const std::array<std::size_t, 1> child{1};
        const auto one = render(viewer, true, child);
        if (!pixel(one, 32, 64, {0, 0, 0, 255}) ||
            !pixel(one, 96, 64, {255, 0, 0, 255}) || coverage(one) != 3072) {
            throw std::runtime_error("Unselected drawable was rendered or "
                                     "parent transform was lost");
        }
        const auto shared = render(viewer, false, forward);
        if (!pixel(shared, 32, 64, {255, 0, 0, 255}) ||
            !pixel(shared, 96, 64, {255, 0, 0, 255}) ||
            coverage(shared) != 6144) {
            throw std::runtime_error("Two scene instances did not share "
                                     "geometry with separate matrices");
        }
        const auto variants = Scene3DD::parse(scene_mesh_variants());
        if (!variants)
            throw std::runtime_error(variants.error().detail);
        for (std::size_t entry = 0; entry < 2; ++entry) {
            const std::array<SceneMeshSelection, 1> selected{{{1, entry}}};
            const auto packed = SceneMesh::build_selected(
                *variants, scene_mesh_geometry(false, true), selected);
            if (!packed)
                throw std::runtime_error(packed.error().detail);
            const auto image = render_packed(viewer, *packed);
            const std::array<unsigned, 4> expected =
                entry == 0 ? std::array<unsigned, 4>{255, 0, 0, 255}
                           : std::array<unsigned, 4>{0, 0, 255, 255};
            if (!pixel(image, 96, 64, expected) ||
                !pixel(image, 32, 64, {0, 0, 0, 255}) ||
                coverage(image) != 3072) {
                throw std::runtime_error(
                    "Explicit object entry did not replace geometry at the "
                    "same scene node");
            }
        }
        std::cout
            << "{\"scope\":\"3dd-3dg-native-render-fixture\",\"game_executed\":"
               "false,"
               "\"api\":\"D3D12\",\"process_bits\":"
            << sizeof(void*) * 8
            << ",\"software\":" << (warp ? "true" : "false")
            << ",\"readbacks\":6,\"coverage_two_objects\":6144,\"coverage_one_"
               "object\":3072,"
               "\"shared_geometry_verified\":true,\"explicit_variant_"
               "verified\":true,\"original_materials\":false,"
               "\"debug_layer\":false,\"validation_errors\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TDU-Recompilation scene fixture: " << error.what()
                  << '\n';
        return 1;
    }
}

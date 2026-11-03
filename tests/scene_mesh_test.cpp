#include "fixtures/scene_mesh_fixture.hpp"
#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/scene_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace tdur::assets;
using namespace tdur::fixtures;

namespace {
unsigned checks = 0;
unsigned failures = 0;
void check(bool value, const char* description) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << description << '\n';
    }
}
bool close(float a, double b) { return std::abs(double(a) - b) < 0.0001; }

void fixtures() {
    auto scene_bytes = scene_mesh_scene();
    const auto scene = Scene3DD::parse(scene_bytes);
    check(bool(scene), "independent parent/shared-geometry fixture parses");
    if (!scene) {
        std::cerr << scene.error().detail << '\n';
        return;
    }
    auto geometry = scene_mesh_geometry();
    const std::array<std::size_t, 2> selection{2, 1};
    const auto mesh = SceneMesh::build(*scene, geometry, selection);
    check(bool(mesh), "explicit selection builds");
    if (!mesh) {
        std::cerr << mesh.error().detail << '\n';
        return;
    }
    check(mesh->vertices().size() == 4 && mesh->indices().size() == 6 &&
              mesh->groups().size() == 1 && mesh->instances().size() == 2,
          "two instances share one uploaded quad");
    check(std::ranges::equal(mesh->indices(),
                             std::array<std::uint32_t, 6>{0, 1, 2, 2, 1, 3}),
          "strip parity and local axes are preserved");
    check(mesh->instances()[0].source_node_index == 2 &&
              mesh->instances()[1].source_node_index == 1 &&
              mesh->instances()[0].group_index ==
                  mesh->instances()[1].group_index,
          "selection order retained with shared group identity");
    check(mesh->instances()[0].node_key[0] == std::byte{'L'} &&
              mesh->instances()[0].object_key[0] == std::byte{'P'} &&
              mesh->groups()[0].geometry_source_offset == 64,
          "OBJ binding is exact and independent of instance label");
    check(mesh->instances()[0].composed_transform ==
              scene->nodes()[2].composed_transform,
          "original composed matrix retained without another composition");
    check(mesh->bounds() &&
              mesh->bounds()->minimum == std::array<float, 3>{8, 26, 42} &&
              mesh->bounds()->maximum == std::array<float, 3>{14, 29, 42},
          "bounds include both instances and nondrawable scaled ancestor");
    check(mesh->instances()[0].bounds.minimum ==
                  std::array<float, 3>{8, 26, 42} &&
              mesh->instances()[0].bounds.maximum ==
                  std::array<float, 3>{10, 29, 42},
          "rotated leaf bounds differ from translated child");
    const auto& primitive = mesh->groups()[0].primitives[0];
    check(primitive.opaque_binding_word == 0x12345678 &&
              !primitive.has_texcoords && primitive.first_index == 0 &&
              primitive.index_count == 6 && primitive.vertex_count == 4,
          "primitive source metadata preserved without assigning material");
    const auto textured =
        SceneMesh::build(*scene, scene_mesh_geometry(true), selection);
    check(textured && textured->groups()[0].primitives[0].has_texcoords &&
              textured->vertices()[0].texcoord == std::array<float, 2>{-1, 2} &&
              textured->vertices()[3].texcoord == std::array<float, 2>{2, 5},
          "known UVs are preserved unclamped and unflipped");
    std::fill(geometry.begin(), geometry.end(), std::byte{0});
    std::fill(scene_bytes.begin(), scene_bytes.end(), std::byte{0});
    check(mesh->vertices()[3].position == std::array<float, 3>{1, 1, 0} &&
              mesh->instances()[0].composed_transform[12] == 10,
          "all output buffers and transforms own their data");
    const auto empty = SceneMesh::build(*scene, scene_mesh_geometry(), {});
    check(empty && empty->vertices().empty() && empty->instances().empty() &&
              !empty->bounds(),
          "empty explicit selection does not select all scene nodes");

    const auto distinct_scene = Scene3DD::parse(scene_mesh_scene(true));
    const std::array<std::size_t, 2> ordered{1, 2};
    const auto distinct =
        distinct_scene
            ? SceneMesh::build(*distinct_scene,
                               scene_mesh_geometry(false, true), ordered)
            : std::expected<SceneMesh, SceneMeshError>(
                  std::unexpected(SceneMeshError{}));
    check(distinct && distinct->groups().size() == 2 &&
              distinct->vertices().size() == 8 &&
              std::ranges::equal(distinct->indices(),
                                 std::array<std::uint32_t, 12>{
                                     0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7}),
          "distinct GEOM ranges rebase indices without merging equal source "
          "buffers");
    check(distinct && distinct->groups()[1].first_vertex == 4 &&
              distinct->groups()[1].first_index == 6 &&
              distinct->groups()[1].primitives[0].opaque_binding_word ==
                  0x12345679,
          "second group and primitive ranges reference their own packed "
          "vertices");
    auto unsupported_other = scene_mesh_geometry(false, true);
    scene_word(unsupported_other, 288 + 0x40, 3);
    const std::array<std::size_t, 1> child_only{1};
    const auto strict =
        distinct_scene
            ? SceneMesh::build(*distinct_scene, unsupported_other, child_only)
            : std::expected<SceneMesh, SceneMeshError>(
                  std::unexpected(SceneMeshError{}));
    check(!strict &&
              strict.error().code == SceneMeshErrorCode::unsupported_geometry,
          "complete paired file must decode even when the unsupported group is "
          "unselected");

    const auto refuses = [&](std::span<const std::size_t> indices,
                             SceneMeshLimits limits, SceneMeshErrorCode code) {
        const auto value =
            SceneMesh::build(*scene, scene_mesh_geometry(), indices, limits);
        check(!value && value.error().code == code,
              "invalid selection or budget refused");
    };
    refuses(std::array<std::size_t, 1>{3}, {},
            SceneMeshErrorCode::invalid_selection);
    refuses(std::array<std::size_t, 1>{0}, {},
            SceneMeshErrorCode::invalid_selection);
    refuses(std::array<std::size_t, 2>{1, 1}, {},
            SceneMeshErrorCode::invalid_selection);
    SceneMeshLimits limits;
    limits.max_vertices = 3;
    refuses(selection, limits, SceneMeshErrorCode::resource_limit);
    limits = {};
    limits.max_indices = 5;
    refuses(selection, limits, SceneMeshErrorCode::resource_limit);
    limits = {};
    limits.max_groups = 0;
    refuses(selection, limits, SceneMeshErrorCode::resource_limit);
    limits = {};
    limits.max_instances = 1;
    refuses(selection, limits, SceneMeshErrorCode::resource_limit);
    limits = {};
    limits.max_transformed_vertices = 7;
    refuses(selection, limits, SceneMeshErrorCode::resource_limit);
    limits = {};
    limits.max_output_bytes = 1;
    refuses(selection, limits, SceneMeshErrorCode::resource_limit);
    auto wrong_key = scene_mesh_geometry();
    wrong_key[48] = std::byte{'X'};
    const auto mismatch = SceneMesh::build(*scene, wrong_key, selection);
    check(!mismatch &&
              mismatch.error().code == SceneMeshErrorCode::geometry_binding,
          "mismatched paired geometry refused");
    auto invalid_index = scene_mesh_geometry();
    invalid_index[496] = std::byte{9};
    const auto bad = SceneMesh::build(*scene, invalid_index, selection);
    check(!bad && bad.error().code == SceneMeshErrorCode::unsupported_geometry,
          "binding success does not bypass vertex/index validation");

    auto residue = scene_mesh_scene();
    scene_number(residue, 256 + 3 * 4, 0.00005f);
    const auto residual_scene = Scene3DD::parse(residue);
    const std::array<std::size_t, 1> child{1};
    const auto residual =
        residual_scene
            ? SceneMesh::build(*residual_scene, scene_mesh_geometry(), child)
            : std::expected<SceneMesh, SceneMeshError>(
                  std::unexpected(SceneMeshError{}));
    check(
        residual && residual->bounds() &&
            close(residual->bounds()->minimum[0], 12 / 1.00005) &&
            close(residual->bounds()->maximum[0], 14 / 1.0001),
        "Cartesian bounds divide homogeneous W without erasing source residue");

    auto nonpositive = scene_mesh_scene();
    scene_number(nonpositive, 256 + 3 * 4, -0.0001f);
    auto distant = scene_mesh_geometry();
    scene_number(distant, 352, 20'000);
    const auto w_scene = Scene3DD::parse(nonpositive);
    const auto w = w_scene ? SceneMesh::build(*w_scene, distant, child)
                           : std::expected<SceneMesh, SceneMeshError>(
                                 std::unexpected(SceneMeshError{}));
    check(!w && w.error().code == SceneMeshErrorCode::invalid_transform,
          "nonpositive W refused before exposing bounds");

    const auto variants = Scene3DD::parse(scene_mesh_variants());
    check(variants && variants->nodes()[1].object_entries.size() == 2 &&
              variants->nodes()[1].object_entries[0].opaque_words[0] == 20 &&
              variants->nodes()[1].object_entries[1].opaque_words[0] == 400 &&
              variants->nodes()[1].object_entries[1].opaque_words[3] ==
                  0x11223344,
          "all20-byte object entries and four descriptors retain source order");
    if (variants) {
        const auto bound =
            variants->resolve_geometry(scene_mesh_geometry(false, true));
        check(bound && bound->size() == 3 && (*bound)[1].node_index == 1 &&
                  (*bound)[1].object_entry_index == 1 &&
                  (*bound)[1].geometry_group_offset == 272,
              "every object-entry index resolves its own exact geometry key");
        const auto unspecified = SceneMesh::build(
            *variants, scene_mesh_geometry(false, true), child_only);
        check(!unspecified && unspecified.error().code ==
                                  SceneMeshErrorCode::invalid_selection,
              "ambiguous node never silently selects its first object entry");
        const std::array<SceneMeshSelection, 1> second{{{1, 1}}};
        const auto chosen = SceneMesh::build_selected(
            *variants, scene_mesh_geometry(false, true), second);
        check(chosen && chosen->instances()[0].source_object_entry_index == 1 &&
                  chosen->instances()[0].object_key[0] == std::byte{'A'} &&
                  chosen->groups()[0].geometry_source_offset == 272,
              "explicit second object entry is selected without descriptor "
              "ranking");
        const std::array<SceneMeshSelection, 1> outside{{{1, 2}}};
        check(!SceneMesh::build_selected(
                  *variants, scene_mesh_geometry(false, true), outside),
              "object-entry index outside count is refused");
        auto missing_real = scene_mesh_variants();
        scene_word(missing_real, 1092, 0);
        check(!Scene3DD::parse(missing_real),
              "second object reference must occur in REAL");
        auto beyond_count = scene_mesh_variants();
        scene_word(beyond_count, 480 + 0x5c, 5U << 16);
        check(!Scene3DD::parse(beyond_count),
              "count cannot exceed the supported INS extent");
        Scene3DDLimits reference_limit;
        reference_limit.max_object_references = 2;
        check(!Scene3DD::parse(scene_mesh_variants(), reference_limit),
              "all object entries consume the reference count budget");
    }
    auto no_material = scene_mesh_scene();
    scene_word(no_material, 676, 0);
    const auto fallback_scene = Scene3DD::parse(no_material);
    const std::array<std::size_t, 1> leaf_only{2};
    const auto fallback =
        fallback_scene ? SceneMesh::build(*fallback_scene,
                                          scene_mesh_geometry(), leaf_only)
                       : std::expected<SceneMesh, SceneMeshError>(
                             std::unexpected(SceneMeshError{}));
    check(fallback && !fallback->instances()[0].source_material_bank_index,
          "missing material bank does not erase a valid geometry instance");
    auto inactive = scene_mesh_scene();
    scene_word(inactive, 240 + 0x60, 96);
    const auto counted = Scene3DD::parse(inactive);
    check(counted && counted->nodes()[0].object_entries.empty() &&
              !counted->nodes()[0].object_index,
          "count0 does not follow inactive object-looking tail data");
    auto four = scene_mesh_variants();
    scene_word(four, 480 + 0x5c, 4U << 16);
    scene_word(four, 480 + 0x88, 96);
    scene_word(four, 480 + 0x9c, 112);
    scene_word(four, 480 + 0xac, 0x87654321);
    scene_word(four, 1096, 480 + 0x88);
    scene_word(four, 1100, 480 + 0x9c);
    const auto maximum = Scene3DD::parse(four);
    check(
        maximum && maximum->nodes()[1].object_entries.size() == 4 &&
            maximum->nodes()[1].object_entries[3].source_offset == 480 + 0x9c &&
            maximum->nodes()[1].object_entries[3].opaque_words[3] == 0x87654321,
        "fourth20-byte entry ends exactly at the supported INS boundary");
    if (maximum) {
        const std::array<SceneMeshSelection, 1> last{{{1, 3}}};
        const auto selected = SceneMesh::build_selected(
            *maximum, scene_mesh_geometry(false, true), last);
        check(
            selected &&
                selected->instances()[0].source_object_entry_index == 3 &&
                selected->instances()[0].object_key[0] == std::byte{'A'},
            "fourth object entry resolves without losing its source identity");
    }
}

void official(const std::filesystem::path& path) {
    const auto bank = BankArchive::read_file(path);
    check(bool(bank), "official bank opens read-only");
    if (!bank)
        return;
    const auto raw_scene = bank->stored_bytes(0);
    const auto raw_geometry = bank->stored_bytes(1);
    check(raw_scene && raw_geometry, "explicit member pair available");
    if (!raw_scene || !raw_geometry)
        return;
    const auto scene = Scene3DD::parse(*raw_scene);
    check(bool(scene), "official scene parses");
    if (!scene)
        return;
    std::vector<SceneMeshSelection> selected;
    std::size_t object_entries = 0;
    for (std::size_t i = 0; i < scene->nodes().size(); ++i) {
        object_entries += scene->nodes()[i].object_entries.size();
        if (scene->nodes()[i].object_index)
            selected.push_back({i, 0});
    }
    const auto mesh =
        SceneMesh::build_selected(*scene, *raw_geometry, selected);
    check(bool(mesh), "every explicitly enumerated drawable links");
    if (!mesh) {
        std::cerr << mesh.error().detail << '\n';
        return;
    }
    check(mesh->instances().size() == selected.size() &&
              mesh->bounds().has_value(),
          "all enumerated instances have owned finite bounds");
    check(mesh->groups().size() < mesh->instances().size(),
          "official shared geometry remains shared");
    for (auto& selection : selected) {
        selection.object_entry_index =
            scene->nodes()[selection.node_index].object_entries.size() - 1;
    }
    const auto last_entries =
        SceneMesh::build_selected(*scene, *raw_geometry, selected);
    check(last_entries && last_entries->instances().size() == selected.size(),
          "a separate explicit last-entry pass covers alternative object "
          "bindings");
    std::cout << "official=" << path.filename().string()
              << " nodes=" << scene->nodes().size()
              << " instances=" << mesh->instances().size()
              << " object_entries=" << object_entries
              << " unique_groups=" << mesh->groups().size()
              << " unique_vertices=" << mesh->vertices().size()
              << " unique_indices=" << mesh->indices().size() << '\n';
}
} // namespace
int main(int argc, char** argv) {
    fixtures();
    for (int i = 1; i < argc; ++i)
        official(std::filesystem::path(argv[i]));
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

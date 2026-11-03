#pragma once

#include "tdur/assets/geometry_3dg.hpp"
#include "tdur/assets/scene_3dd.hpp"

namespace tdur::assets {

struct SceneMeshLimits {
    std::size_t max_vertices = 2'000'000;
    std::size_t max_indices = 12'000'000;
    std::size_t max_groups = 10'000;
    std::size_t max_instances = 100'000;
    std::size_t max_transformed_vertices = 20'000'000;
    // Logical owned content, excluding vector spare capacity, allocator
    // overhead and the bounded intermediate 3DD/3DG parsing allocations.
    std::size_t max_output_bytes = 256U * 1024U * 1024U;
};

enum class SceneMeshErrorCode {
    invalid_selection,
    geometry_binding,
    unsupported_geometry,
    empty_geometry,
    invalid_transform,
    resource_limit,
};

struct SceneMeshError {
    SceneMeshErrorCode code{};
    std::size_t source_offset{};
    std::string detail;
};

struct SceneMeshVertex {
    std::array<float, 3> position;
    // Zero placeholder when the owning primitive has_texcoords is false.
    std::array<float, 2> texcoord;
};

struct SceneMeshPrimitive {
    std::uint32_t source_offset{};
    std::uint32_t opaque_binding_word{};
    std::uint32_t first_vertex{};
    std::uint32_t vertex_count{};
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    bool has_texcoords{};
};

struct SceneMeshGroup {
    std::uint32_t geometry_source_offset{};
    std::string name_bytes;
    std::uint32_t first_vertex{};
    std::uint32_t vertex_count{};
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::vector<SceneMeshPrimitive> primitives;
};

struct SceneMeshInstance {
    std::size_t source_node_index{};
    std::size_t group_index{};
    Scene3DDKey node_key;
    Scene3DDKey object_key;
    // Source-scene metadata only, not a decoded per-primitive material.
    std::optional<std::size_t> source_material_bank_index;
    Scene3DDMatrix composed_transform;
    Geometry3DGBounds bounds;
    std::size_t source_object_entry_index{};
};

struct SceneMeshSelection {
    std::size_t node_index{};
    // Omit only when the node has exactly one entry. The raw order is not
    // a quality ranking, so ambiguous nodes never silently choose entry0.
    std::optional<std::size_t> object_entry_index;
};

// Backend-independent geometry for an explicit selection of 3DD instances.
// Owns the resulting buffers and matrices; no resource pointer is retained.
// No visibility, LOD, material or axis-conversion rule is inferred here.
// The complete paired 3DG must pass both readers, including unselected
// groups. Their own input/decoding limits apply before output assembly.
class SceneMesh {
  public:
    [[nodiscard]] static std::expected<SceneMesh, SceneMeshError>
    build(const Scene3DD& scene, std::span<const std::byte> paired_geometry,
          std::span<const std::size_t> selected_nodes,
          SceneMeshLimits limits = {});
    [[nodiscard]] static std::expected<SceneMesh, SceneMeshError>
    build_selected(const Scene3DD& scene,
                   std::span<const std::byte> paired_geometry,
                   std::span<const SceneMeshSelection> selection,
                   SceneMeshLimits limits = {});

    [[nodiscard]] std::span<const SceneMeshVertex> vertices() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
    [[nodiscard]] std::span<const SceneMeshGroup> groups() const noexcept;
    [[nodiscard]] std::span<const SceneMeshInstance> instances() const noexcept;
    [[nodiscard]] const std::optional<Geometry3DGBounds>&
    bounds() const noexcept;

  private:
    std::vector<SceneMeshVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<SceneMeshGroup> groups_;
    std::vector<SceneMeshInstance> instances_;
    std::optional<Geometry3DGBounds> bounds_;
};

} // namespace tdur::assets

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tdur::assets {

using Scene3DDKey = std::array<std::byte, 8>;
using Scene3DDMatrix = std::array<float, 16>;

struct Scene3DDLimits {
    std::size_t max_file_bytes = 256U * 1024U * 1024U;
    std::size_t max_nodes = 100'000;
    std::size_t max_objects = 100'000;
    std::size_t max_material_banks = 100'000;
    std::size_t max_material_keys = 1'000'000;
    std::size_t max_object_references = 1'000'000;
    std::size_t max_depth = 512;
    std::size_t max_output_bytes = 256U * 1024U * 1024U;
};

enum class Scene3DDErrorCode {
    truncated,
    unsupported_version,
    unsupported_layout,
    invalid_chunk,
    invalid_reference,
    invalid_hierarchy,
    invalid_transform,
    resource_limit,
    geometry_mismatch,
};

struct Scene3DDError {
    Scene3DDErrorCode code{};
    std::size_t offset{};
    std::string detail;
};

struct Scene3DDMaterialKey {
    Scene3DDKey key;
    std::array<std::byte, 8> opaque_tail;
};

struct Scene3DDMaterialBank {
    std::uint32_t source_offset{};
    std::vector<Scene3DDMaterialKey> keys;
};

struct Scene3DDObject {
    Scene3DDKey key;
    std::uint32_t hash_entry_offset{};
    std::uint32_t source_offset{};
    std::uint32_t primitive_count{};
    // OBJ+0x10..0x18 and +0x20..0x2c remain uninterpreted bit patterns.
    std::array<std::uint32_t, 7> opaque_words{};
};

struct Scene3DDObjectEntry {
    std::size_t object_index{};
    std::uint32_t source_offset{};
    // Four DWORDs after the OBJ HASH reference. Preserve source order and
    // bits; the dynamic visibility/LOD selection is not established here.
    std::array<std::uint32_t, 4> opaque_words{};
};

struct Scene3DDNode {
    Scene3DDKey key;
    Scene3DDKey instance_key;
    std::uint32_t hierarchy_offset{};
    std::uint32_t instance_offset{};
    Scene3DDMatrix stored_transform;
    // Explicit row-vector composition: local * parent, no source-axis flip.
    // Includes every source hierarchy ancestor, not just drawable nodes.
    Scene3DDMatrix composed_transform;
    std::optional<std::size_t> parent_index;
    // Compatibility view of object_entries.front(), never a selected LOD.
    std::optional<std::size_t> object_index;
    std::vector<Scene3DDObjectEntry> object_entries;
    std::optional<std::size_t> material_bank_index;
    std::uint32_t hierarchy_word{};
    std::uint32_t hierarchy_opaque_word{};
    std::array<std::uint32_t, 24> instance_tail{};
};

struct Scene3DDGeometryBinding {
    std::size_t node_index{};
    std::size_t object_index{};
    std::uint32_t geometry_group_offset{};
    // Preserved for further material-index proof, not interpreted as a shader
    // or texture identifier. The instance's MBNK reference stays on its node.
    std::vector<std::uint32_t> opaque_primitive_bindings;
    std::size_t object_entry_index{};
};

class Scene3DD {
  public:
    // Strict observed PC v2 chunk layout. All output is owned. References
    // must target declared records/entries and appear in the REAL table.
    [[nodiscard]] static std::expected<Scene3DD, Scene3DDError>
    parse(std::span<const std::byte> bytes, Scene3DDLimits limits = {});

    [[nodiscard]] std::span<const Scene3DDObject> objects() const noexcept;
    [[nodiscard]] std::span<const Scene3DDMaterialBank>
    material_banks() const noexcept;
    [[nodiscard]] std::span<const Scene3DDNode> nodes() const noexcept;

    // Caller supplies the paired .3DG, never guessed by a directory scan.
    // Exact eight-byte OBJ/GEOA HASH keys and primitive counts must agree.
    // Geometry3DG can then find each group by its source_offset. This does
    // not select LODs/visibility or assign unproven per-primitive materials.
    // Vertex/index data must be validated separately by Geometry3DG.
    [[nodiscard]] std::expected<std::vector<Scene3DDGeometryBinding>,
                                Scene3DDError>
    resolve_geometry(std::span<const std::byte> geometry_bytes) const;

  private:
    std::vector<Scene3DDObject> objects_;
    std::vector<Scene3DDMaterialBank> material_banks_;
    std::vector<Scene3DDNode> nodes_;
    Scene3DDLimits limits_;
};

} // namespace tdur::assets

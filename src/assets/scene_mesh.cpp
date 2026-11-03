#include "tdur/assets/scene_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>

namespace tdur::assets {
namespace {
struct Failure {
    SceneMeshError error;
};

[[noreturn]] void fail(SceneMeshErrorCode code, std::size_t offset,
                       std::string detail) {
    throw Failure{{code, offset, std::move(detail)}};
}

void bounded_add(std::size_t current, std::size_t count, std::size_t limit,
                 std::size_t offset) {
    if (current > limit || count > limit - current) {
        fail(SceneMeshErrorCode::resource_limit, offset,
             "Scene mesh exceeds configured count limit");
    }
}

struct Budget {
    std::size_t limit;
    std::size_t used{};
    void claim(std::size_t count, std::size_t item, std::size_t offset) {
        if (item && count > (limit - used) / item) {
            fail(SceneMeshErrorCode::resource_limit, offset,
                 "Scene mesh exceeds logical output byte budget");
        }
        used += count * item;
    }
};

void extend(std::optional<Geometry3DGBounds>& bounds,
            const std::array<float, 3>& point) {
    if (!bounds) {
        bounds = Geometry3DGBounds{point, point};
        return;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bounds->minimum[axis] = std::min(bounds->minimum[axis], point[axis]);
        bounds->maximum[axis] = std::max(bounds->maximum[axis], point[axis]);
    }
}

std::array<float, 3> placed(const std::array<float, 3>& point,
                            const Scene3DDMatrix& matrix, std::size_t offset) {
    std::array<double, 4> homogeneous{};
    for (std::size_t axis = 0; axis < 4; ++axis) {
        homogeneous[axis] = matrix[12 + axis];
        for (std::size_t k = 0; k < 3; ++k) {
            homogeneous[axis] += double(point[k]) * matrix[k * 4 + axis];
        }
    }
    // Preserve the small affine residues permitted by Scene3DD. Bounds use
    // Cartesian coordinates, while the GPU retains the original matrix.
    if (!std::isfinite(homogeneous[3]) || homogeneous[3] <= 0) {
        fail(SceneMeshErrorCode::invalid_transform, offset,
             "Selected geometry has nonpositive or nonfinite homogeneous W");
    }
    std::array<float, 3> result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double value = homogeneous[axis] / homogeneous[3];
        if (!std::isfinite(value) ||
            std::abs(value) > std::numeric_limits<float>::max()) {
            fail(SceneMeshErrorCode::invalid_transform, offset,
                 "Selected geometry exceeds finite Cartesian float bounds");
        }
        result[axis] = static_cast<float>(value);
    }
    return result;
}
} // namespace

std::expected<SceneMesh, SceneMeshError>
SceneMesh::build_selected(const Scene3DD& scene,
                          std::span<const std::byte> bytes,
                          std::span<const SceneMeshSelection> selected_nodes,
                          SceneMeshLimits limits) {
    try {
        bounded_add(0, selected_nodes.size(), limits.max_instances, 0);
        Budget budget{limits.max_output_bytes};
        budget.claim(selected_nodes.size(), sizeof(SceneMeshInstance), 0);
        std::set<std::size_t> selected;
        for (const auto& selection : selected_nodes) {
            const auto index = selection.node_index;
            if (index >= scene.nodes().size() ||
                !selected.insert(index).second) {
                fail(SceneMeshErrorCode::invalid_selection, 0,
                     "Selected node is out of range or repeated");
            }
            const auto& node = scene.nodes()[index];
            if (node.object_entries.empty()) {
                fail(SceneMeshErrorCode::invalid_selection,
                     node.hierarchy_offset,
                     "Selected node has no drawable object");
            }
            if ((!selection.object_entry_index &&
                 node.object_entries.size() != 1) ||
                selection.object_entry_index.value_or(0) >=
                    node.object_entries.size()) {
                fail(SceneMeshErrorCode::invalid_selection,
                     node.instance_offset + 0x5e,
                     "Choose an explicit valid object entry for an ambiguous "
                     "scene node");
            }
        }
        const auto bindings = scene.resolve_geometry(bytes);
        if (!bindings) {
            fail(SceneMeshErrorCode::geometry_binding, bindings.error().offset,
                 bindings.error().detail);
        }
        const auto geometry = Geometry3DG::parse(bytes);
        if (!geometry) {
            fail(SceneMeshErrorCode::unsupported_geometry,
                 geometry.error().offset, geometry.error().detail);
        }
        std::map<std::pair<std::size_t, std::size_t>,
                 const Scene3DDGeometryBinding*>
            binding_by_node;
        for (const auto& binding : *bindings) {
            binding_by_node.emplace(
                std::pair{binding.node_index, binding.object_entry_index},
                &binding);
        }
        std::map<std::uint32_t, const Geometry3DGGroup*> source_groups;
        for (const auto& group : geometry->groups()) {
            source_groups.emplace(group.source_offset, &group);
        }
        std::map<std::uint32_t, std::size_t> uploaded_groups;
        SceneMesh result;
        result.instances_.reserve(selected_nodes.size());
        std::size_t transformed_vertices = 0;
        for (const auto& selection : selected_nodes) {
            const auto node_index = selection.node_index;
            const auto entry_index = selection.object_entry_index.value_or(0);
            const auto& node = scene.nodes()[node_index];
            const auto found_binding =
                binding_by_node.find(std::pair{node_index, entry_index});
            if (found_binding == binding_by_node.end()) {
                fail(SceneMeshErrorCode::geometry_binding,
                     node.hierarchy_offset,
                     "Selected node has no exact geometry binding");
            }
            const auto source_offset =
                found_binding->second->geometry_group_offset;
            auto destination = uploaded_groups.find(source_offset);
            if (destination == uploaded_groups.end()) {
                const auto source = source_groups.find(source_offset);
                if (source == source_groups.end()) {
                    fail(SceneMeshErrorCode::geometry_binding, source_offset,
                         "Resolved GEOM has no decoded geometry");
                }
                bounded_add(result.groups_.size(), 1, limits.max_groups,
                            source_offset);
                budget.claim(1, sizeof(SceneMeshGroup), source_offset);
                budget.claim(source->second->name_bytes.size(), sizeof(char),
                             source_offset);
                budget.claim(source->second->primitives.size(),
                             sizeof(SceneMeshPrimitive), source_offset);
                SceneMeshGroup group{};
                group.geometry_source_offset = source_offset;
                group.name_bytes = source->second->name_bytes;
                group.first_vertex =
                    static_cast<std::uint32_t>(result.vertices_.size());
                group.first_index =
                    static_cast<std::uint32_t>(result.indices_.size());
                group.primitives.reserve(source->second->primitives.size());
                for (const auto& primitive : source->second->primitives) {
                    const auto vertex_limit = std::min<std::size_t>(
                        limits.max_vertices,
                        std::numeric_limits<std::uint32_t>::max());
                    const auto index_limit = std::min<std::size_t>(
                        limits.max_indices,
                        std::numeric_limits<std::uint32_t>::max());
                    bounded_add(result.vertices_.size(),
                                primitive.positions.size(), vertex_limit,
                                primitive.source_offset);
                    bounded_add(result.indices_.size(),
                                primitive.triangle_indices.size(), index_limit,
                                primitive.source_offset);
                    budget.claim(primitive.positions.size(),
                                 sizeof(SceneMeshVertex),
                                 primitive.source_offset);
                    budget.claim(primitive.triangle_indices.size(),
                                 sizeof(std::uint32_t),
                                 primitive.source_offset);
                    SceneMeshPrimitive range{
                        primitive.source_offset,
                        primitive.opaque_binding_word,
                        static_cast<std::uint32_t>(result.vertices_.size()),
                        static_cast<std::uint32_t>(primitive.positions.size()),
                        static_cast<std::uint32_t>(result.indices_.size()),
                        static_cast<std::uint32_t>(
                            primitive.triangle_indices.size()),
                        !primitive.texcoords.empty()};
                    for (std::size_t i = 0; i < primitive.positions.size();
                         ++i) {
                        result.vertices_.push_back(
                            {primitive.positions[i],
                             range.has_texcoords ? primitive.texcoords[i]
                                                 : std::array<float, 2>{}});
                    }
                    for (const auto index : primitive.triangle_indices) {
                        result.indices_.push_back(range.first_vertex + index);
                    }
                    group.primitives.push_back(range);
                }
                group.vertex_count =
                    static_cast<std::uint32_t>(result.vertices_.size()) -
                    group.first_vertex;
                group.index_count =
                    static_cast<std::uint32_t>(result.indices_.size()) -
                    group.first_index;
                if (!group.vertex_count || !group.index_count) {
                    fail(SceneMeshErrorCode::empty_geometry, source_offset,
                         "Selected GEOM has no supported triangles");
                }
                const auto group_index = result.groups_.size();
                result.groups_.push_back(std::move(group));
                destination =
                    uploaded_groups.emplace(source_offset, group_index).first;
            }
            const auto& group = result.groups_[destination->second];
            bounded_add(transformed_vertices, group.vertex_count,
                        limits.max_transformed_vertices, node.hierarchy_offset);
            transformed_vertices += group.vertex_count;
            std::optional<Geometry3DGBounds> instance_bounds;
            for (std::size_t i = group.first_vertex;
                 i < std::size_t(group.first_vertex) + group.vertex_count;
                 ++i) {
                const auto point =
                    placed(result.vertices_[i].position,
                           node.composed_transform, node.hierarchy_offset);
                extend(instance_bounds, point);
                extend(result.bounds_, point);
            }
            result.instances_.push_back(
                {node_index, destination->second, node.key,
                 scene.objects()[found_binding->second->object_index].key,
                 node.material_bank_index, node.composed_transform,
                 *instance_bounds, entry_index});
        }
        return result;
    } catch (const Failure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            SceneMeshError{SceneMeshErrorCode::resource_limit, 0,
                           "Scene mesh allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            SceneMeshError{SceneMeshErrorCode::resource_limit, 0,
                           "Scene mesh allocation length exceeded"});
    }
}

std::expected<SceneMesh, SceneMeshError>
SceneMesh::build(const Scene3DD& scene, std::span<const std::byte> bytes,
                 std::span<const std::size_t> nodes, SceneMeshLimits limits) {
    if (nodes.size() > limits.max_instances) {
        return std::unexpected(
            SceneMeshError{SceneMeshErrorCode::resource_limit, 0,
                           "Scene selection exceeds instance limit"});
    }
    try {
        std::vector<SceneMeshSelection> selected;
        selected.reserve(nodes.size());
        for (const auto node : nodes)
            selected.push_back({node, std::nullopt});
        return build_selected(scene, bytes, selected, limits);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            SceneMeshError{SceneMeshErrorCode::resource_limit, 0,
                           "Scene selection allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            SceneMeshError{SceneMeshErrorCode::resource_limit, 0,
                           "Scene selection allocation length exceeded"});
    }
}

std::span<const SceneMeshVertex> SceneMesh::vertices() const noexcept {
    return vertices_;
}
std::span<const std::uint32_t> SceneMesh::indices() const noexcept {
    return indices_;
}
std::span<const SceneMeshGroup> SceneMesh::groups() const noexcept {
    return groups_;
}
std::span<const SceneMeshInstance> SceneMesh::instances() const noexcept {
    return instances_;
}
const std::optional<Geometry3DGBounds>& SceneMesh::bounds() const noexcept {
    return bounds_;
}

} // namespace tdur::assets

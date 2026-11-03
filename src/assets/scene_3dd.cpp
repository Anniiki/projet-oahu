#include "tdur/assets/scene_3dd.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>

namespace tdur::assets {
namespace {
using Bytes = std::span<const std::byte>;
struct Failure {
    Scene3DDError error;
};
[[noreturn]] void fail(Scene3DDErrorCode code, std::size_t offset,
                       const char* detail) {
    throw Failure{{code, offset, detail}};
}

struct Chunk {
    std::array<char, 4> tag;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t layout;
    bool is(std::string_view value) const {
        return std::equal(tag.begin(), tag.end(), value.begin(), value.end());
    }
    std::size_t end() const { return std::size_t(offset) + size; }
};

class Reader {
  public:
    Reader(Bytes bytes, const Scene3DDLimits& limits)
        : bytes_(bytes), limits_(limits) {
        if (bytes.size() > limits.max_file_bytes || bytes.size() > UINT32_MAX) {
            fail(Scene3DDErrorCode::resource_limit, 0,
                 "Scene file exceeds byte budget");
        }
    }
    void range(std::size_t offset, std::size_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - offset) {
            fail(Scene3DDErrorCode::truncated, offset,
                 "Scene field exceeds input");
        }
    }
    std::uint32_t u32(std::size_t offset) const {
        range(offset, 4);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= std::to_integer<std::uint32_t>(bytes_[offset + i])
                     << (i * 8);
        }
        return value;
    }
    std::uint16_t u16(std::size_t offset) const {
        range(offset, 2);
        return std::uint16_t(
            std::to_integer<unsigned>(bytes_[offset]) |
            (std::to_integer<unsigned>(bytes_[offset + 1]) << 8));
    }
    Scene3DDKey key(std::size_t offset) const {
        range(offset, 8);
        Scene3DDKey result;
        std::copy_n(bytes_.begin() + offset, 8, result.begin());
        return result;
    }
    float number(std::size_t offset) const {
        const auto value = std::bit_cast<float>(u32(offset));
        if (!std::isfinite(value)) {
            fail(Scene3DDErrorCode::invalid_transform, offset,
                 "Non-finite scene matrix");
        }
        return value;
    }
    void claim(std::size_t count, std::size_t stride, std::size_t offset) {
        if (count > (limits_.max_output_bytes - used_) / stride) {
            fail(Scene3DDErrorCode::resource_limit, offset,
                 "Scene decoded-output budget exceeded");
        }
        used_ += count * stride;
    }
    void file(std::uint32_t version, std::string_view suffix) const {
        range(0, 16);
        if (u32(0) != version) {
            fail(Scene3DDErrorCode::unsupported_version, 0,
                 "Unsupported scene/geometry version");
        }
        if (u32(8) != bytes_.size() ||
            !std::equal(suffix.begin(), suffix.end(),
                        reinterpret_cast<const char*>(bytes_.data() + 12))) {
            fail(Scene3DDErrorCode::invalid_chunk, 8,
                 "File size or format marker differs");
        }
    }
    std::vector<Chunk> children(std::size_t begin, std::size_t end) {
        range(begin, end >= begin ? end - begin : bytes_.size());
        std::vector<Chunk> result;
        while (begin < end) {
            if (begin % 16 || end - begin < 16) {
                fail(Scene3DDErrorCode::invalid_chunk, begin,
                     "Unaligned or truncated chunk");
            }
            const auto size = u32(begin + 8);
            if (size < 16 || size % 16 || size > end - begin) {
                fail(Scene3DDErrorCode::invalid_chunk, begin + 8,
                     "Chunk size exceeds parent or alignment");
            }
            claim(1, sizeof(Chunk), begin);
            Chunk item{{}, std::uint32_t(begin), size, u32(begin + 12)};
            std::memcpy(item.tag.data(), bytes_.data() + begin, 4);
            result.push_back(item);
            begin += size;
        }
        return result;
    }
    std::vector<Chunk> children(const Chunk& parent) {
        if (parent.layout != 16) {
            fail(Scene3DDErrorCode::unsupported_layout, parent.offset + 12,
                 "Expected container header layout");
        }
        return children(parent.offset + 16, parent.end());
    }
    void leaf(const Chunk& item, std::string_view type,
              std::uint32_t size = 0) const {
        if (!item.is(type) || item.layout != item.size ||
            (size && item.size != size)) {
            fail(Scene3DDErrorCode::unsupported_layout, item.offset,
                 "Unsupported typed leaf layout");
        }
    }
    std::size_t size() const { return bytes_.size(); }

  private:
    Bytes bytes_;
    const Scene3DDLimits& limits_;
    std::size_t used_ = 0;
};

struct HashEntry {
    Scene3DDKey key;
    std::uint32_t entry;
    std::uint32_t target;
};

std::vector<HashEntry> hash(Reader& reader, const std::vector<Chunk>& children,
                            std::string_view type, std::size_t limit,
                            bool containers = false) {
    if (children.empty()) {
        fail(Scene3DDErrorCode::invalid_chunk, 0, "Missing HASH table");
    }
    reader.leaf(children.front(), "HASH");
    const auto& table = children.front();
    const auto count = (table.size - 16) / 16;
    if (count > limit) {
        fail(Scene3DDErrorCode::resource_limit, table.offset,
             "HASH count exceeds budget");
    }
    if (count != children.size() - 1) {
        fail(Scene3DDErrorCode::invalid_reference, table.offset,
             "HASH/record counts differ");
    }
    std::set<std::uint32_t> records;
    for (const auto& child : std::span(children).subspan(1)) {
        if (containers) {
            if (!child.is(type) || child.layout != 16) {
                fail(Scene3DDErrorCode::unsupported_layout, child.offset,
                     "Unexpected HASH container record");
            }
        } else {
            reader.leaf(child, type);
        }
        records.insert(child.offset);
    }
    reader.claim(count, sizeof(HashEntry), table.offset);
    std::vector<HashEntry> result;
    std::set<Scene3DDKey> keys;
    for (std::size_t i = table.offset + 16; i < table.end(); i += 16) {
        HashEntry entry{reader.key(i), std::uint32_t(i), reader.u32(i + 8)};
        if (!keys.insert(entry.key).second ||
            records.erase(entry.target) != 1) {
            fail(Scene3DDErrorCode::invalid_reference, i,
                 "Duplicate key or HASH target outside declared records");
        }
        result.push_back(entry);
    }
    return result;
}

std::map<std::uint32_t, const Chunk*> index(const std::vector<Chunk>& chunks) {
    std::map<std::uint32_t, const Chunk*> result;
    for (const auto& chunk : chunks) {
        result.emplace(chunk.offset, &chunk);
    }
    return result;
}

std::size_t lookup(const std::map<std::uint32_t, std::size_t>& offsets,
                   std::uint32_t target, std::size_t field) {
    const auto found = offsets.find(target);
    if (found == offsets.end()) {
        fail(Scene3DDErrorCode::invalid_reference, field,
             "Reference is not a declared record/entry");
    }
    return found->second;
}

Scene3DDMatrix multiply(const Scene3DDMatrix& left, const Scene3DDMatrix& right,
                        std::size_t offset) {
    Scene3DDMatrix result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            double value = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                value += double(left[row * 4 + k]) * right[k * 4 + column];
            }
            if (!std::isfinite(value) ||
                std::abs(value) > std::numeric_limits<float>::max()) {
                fail(Scene3DDErrorCode::invalid_transform, offset,
                     "Composed scene transform overflows");
            }
            result[row * 4 + column] = float(value);
        }
    }
    return result;
}

void compose(std::vector<Scene3DDNode>& nodes, const Scene3DDLimits& limits) {
    std::vector<unsigned char> states(nodes.size());
    std::vector<std::size_t> depths(nodes.size());
    for (std::size_t start = 0; start < nodes.size(); ++start) {
        std::vector<std::size_t> path;
        auto current = std::optional(start);
        while (current && states[*current] == 0) {
            if (path.size() >= limits.max_depth) {
                fail(Scene3DDErrorCode::resource_limit,
                     nodes[*current].hierarchy_offset,
                     "Hierarchy depth budget exceeded");
            }
            states[*current] = 1;
            path.push_back(*current);
            current = nodes[*current].parent_index;
        }
        if (current && states[*current] == 1) {
            fail(Scene3DDErrorCode::invalid_hierarchy,
                 nodes[*current].hierarchy_offset, "Hierarchy parent cycle");
        }
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            auto& node = nodes[*it];
            depths[*it] =
                (node.parent_index ? depths[*node.parent_index] : 0) + 1;
            if (depths[*it] > limits.max_depth) {
                fail(Scene3DDErrorCode::resource_limit, node.hierarchy_offset,
                     "Hierarchy depth budget exceeded");
            }
            node.composed_transform =
                node.parent_index
                    ? multiply(node.stored_transform,
                               nodes[*node.parent_index].composed_transform,
                               node.instance_offset)
                    : node.stored_transform;
            states[*it] = 2;
        }
    }
    std::vector<std::size_t> descendants(nodes.size());
    std::vector<std::size_t> childCounts(nodes.size());
    for (const auto& node : nodes) {
        if (node.parent_index) {
            ++childCounts[*node.parent_index];
        }
    }
    std::vector<std::size_t> leaves;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!childCounts[i]) {
            leaves.push_back(i);
        }
    }
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        const auto index = leaves[i];
        if (nodes[index].parent_index) {
            const auto parent = *nodes[index].parent_index;
            descendants[parent] += descendants[index] + 1;
            if (--childCounts[parent] == 0) {
                leaves.push_back(parent);
            }
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if ((nodes[i].hierarchy_word & 0xffffU) != descendants[i]) {
            fail(Scene3DDErrorCode::invalid_hierarchy,
                 nodes[i].hierarchy_offset + 0x18,
                 "Recorded hierarchy descendant count differs");
        }
    }
}
} // namespace

std::expected<Scene3DD, Scene3DDError> Scene3DD::parse(Bytes bytes,
                                                       Scene3DDLimits limits) {
    try {
        Reader reader(bytes, limits);
        reader.file(2, ".3DD");
        const auto roots = reader.children(16, reader.size());
        constexpr std::string_view tags[] = {"MBNA", "OBJA", "INSA", "HIEA",
                                             "REAL"};
        if (roots.size() != std::size(tags)) {
            fail(Scene3DDErrorCode::unsupported_layout, 16,
                 "Unsupported v2 scene root layout");
        }
        for (std::size_t i = 0; i < roots.size(); ++i) {
            if (!roots[i].is(tags[i])) {
                fail(Scene3DDErrorCode::unsupported_layout, roots[i].offset,
                     "Unexpected scene root container");
            }
        }
        reader.leaf(roots[4], "REAL");
        std::set<std::uint32_t> relocations;
        bool padding = false;
        for (std::size_t i = roots[4].offset + 16; i < roots[4].end(); i += 4) {
            const auto field = reader.u32(i);
            if (!field) {
                padding = true;
                continue;
            }
            if (padding || field < 16 || field % 4 ||
                field > reader.size() - 4 ||
                !relocations.insert(field).second) {
                fail(Scene3DDErrorCode::invalid_reference, i,
                     "Invalid or duplicate REAL relocation field");
            }
        }
        const auto reference = [&](std::size_t field) {
            const auto value = reader.u32(field);
            if (value && !relocations.contains(std::uint32_t(field))) {
                fail(Scene3DDErrorCode::invalid_reference, field,
                     "Non-null scene pointer is absent from REAL");
            }
            return value;
        };
        Scene3DD result;
        result.limits_ = limits;
        std::map<std::uint32_t, std::size_t> banks;
        std::size_t materialCount = 0;
        for (const auto& child : reader.children(roots[0])) {
            if (result.material_banks_.size() >= limits.max_material_banks) {
                fail(Scene3DDErrorCode::resource_limit, child.offset,
                     "Material-bank count exceeds budget");
            }
            reader.leaf(child, "MBNK");
            const auto count = (child.size - 16) / 16;
            if (count > limits.max_material_keys - materialCount) {
                fail(Scene3DDErrorCode::resource_limit, child.offset,
                     "Material-key budget exceeded");
            }
            materialCount += count;
            reader.claim(1, sizeof(Scene3DDMaterialBank), child.offset);
            reader.claim(count, sizeof(Scene3DDMaterialKey), child.offset);
            Scene3DDMaterialBank bank{child.offset, {}};
            for (std::size_t i = child.offset + 16; i < child.end(); i += 16) {
                bank.keys.push_back({reader.key(i), reader.key(i + 8)});
            }
            banks.emplace(child.offset, result.material_banks_.size());
            result.material_banks_.push_back(std::move(bank));
        }
        const auto objectChildren = reader.children(roots[1]);
        const auto objectHashes =
            hash(reader, objectChildren, "OBJ.", limits.max_objects);
        const auto objectRecords = index(objectChildren);
        std::map<std::uint32_t, std::size_t> objectSlots;
        reader.claim(objectHashes.size(), sizeof(Scene3DDObject),
                     roots[1].offset);
        for (const auto& item : objectHashes) {
            reference(item.entry + 8);
            reader.leaf(*objectRecords.at(item.target), "OBJ.", 0x30);
            Scene3DDObject object{item.key,
                                  item.entry,
                                  item.target,
                                  reader.u32(item.target + 0x1c),
                                  {}};
            for (std::size_t i = 0; i < 3; ++i) {
                object.opaque_words[i] = reader.u32(item.target + 0x10 + i * 4);
            }
            for (std::size_t i = 0; i < 4; ++i) {
                object.opaque_words[3 + i] =
                    reader.u32(item.target + 0x20 + i * 4);
            }
            objectSlots.emplace(item.entry, result.objects_.size());
            result.objects_.push_back(object);
        }
        const auto instanceChildren = reader.children(roots[2]);
        const auto instanceHashes =
            hash(reader, instanceChildren, "INS.", limits.max_nodes);
        const auto instanceRecords = index(instanceChildren);
        std::map<std::uint32_t, HashEntry> instances;
        for (const auto& item : instanceHashes) {
            reference(item.entry + 8);
            instances.emplace(item.target, item);
        }
        const auto hierarchyChildren = reader.children(roots[3]);
        const auto hierarchyHashes =
            hash(reader, hierarchyChildren, "HIE.", limits.max_nodes);
        const auto hierarchyRecords = index(hierarchyChildren);
        if (hierarchyHashes.size() != instances.size()) {
            fail(Scene3DDErrorCode::unsupported_layout, roots[3].offset,
                 "Hierarchy/instance counts differ");
        }
        std::map<std::uint32_t, std::size_t> nodeOffsets;
        for (std::size_t i = 0; i < hierarchyHashes.size(); ++i) {
            nodeOffsets.emplace(hierarchyHashes[i].target, i);
        }
        std::set<std::uint32_t> usedInstances;
        reader.claim(hierarchyHashes.size(), sizeof(Scene3DDNode),
                     roots[3].offset);
        std::size_t objectReferences = 0;
        for (const auto& item : hierarchyHashes) {
            reference(item.entry + 8);
            reader.leaf(*hierarchyRecords.at(item.target), "HIE.", 0x20);
            const auto instance = reference(item.target + 0x14);
            const auto found = instances.find(instance);
            if (found == instances.end() ||
                !usedInstances.insert(instance).second) {
                fail(Scene3DDErrorCode::invalid_reference, item.target + 0x14,
                     "HIE does not reference a unique INS");
            }
            reader.leaf(*instanceRecords.at(instance), "INS.", 0xb0);
            Scene3DDNode node{};
            node.key = item.key;
            node.instance_key = found->second.key;
            node.hierarchy_offset = item.target;
            node.instance_offset = instance;
            node.hierarchy_word = reader.u32(item.target + 0x18);
            node.hierarchy_opaque_word = reader.u32(item.target + 0x10);
            const auto parent = reference(item.target + 0x1c);
            if (parent) {
                node.parent_index =
                    lookup(nodeOffsets, parent, item.target + 0x1c);
            }
            for (std::size_t i = 0; i < 16; ++i) {
                node.stored_transform[i] =
                    reader.number(instance + 0x10 + i * 4);
            }
            for (const auto index : {3U, 7U, 11U}) {
                if (std::abs(node.stored_transform[index]) > 0.0001f) {
                    fail(Scene3DDErrorCode::unsupported_layout, instance + 0x10,
                         "Non-affine scene transform");
                }
            }
            if (std::abs(node.stored_transform[15] - 1) > 0.0001f) {
                fail(Scene3DDErrorCode::unsupported_layout, instance + 0x4c,
                     "Scene transform homogeneous component differs");
            }
            for (std::size_t i = 0; i < node.instance_tail.size(); ++i) {
                node.instance_tail[i] = reader.u32(instance + 0x50 + i * 4);
            }
            const auto material = reference(instance + 0x54);
            if (material) {
                node.material_bank_index =
                    lookup(banks, material, instance + 0x54);
            }
            const auto count = reader.u16(instance + 0x5e);
            if (count > 4) {
                fail(Scene3DDErrorCode::unsupported_layout, instance + 0x5e,
                     "Object-entry list exceeds the supported INS extent");
            }
            if (count > limits.max_object_references - objectReferences) {
                fail(Scene3DDErrorCode::resource_limit, instance + 0x5e,
                     "Scene object-reference count exceeds budget");
            }
            objectReferences += count;
            reader.claim(count, sizeof(Scene3DDObjectEntry), instance + 0x60);
            node.object_entries.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                const auto entry_offset =
                    instance + 0x60 + std::uint32_t(i * 20);
                const auto object = reference(entry_offset);
                Scene3DDObjectEntry entry{
                    lookup(objectSlots, object, entry_offset),
                    entry_offset,
                    {}};
                for (std::size_t word = 0; word < entry.opaque_words.size();
                     ++word) {
                    entry.opaque_words[word] =
                        reader.u32(entry_offset + 4 + word * 4);
                }
                node.object_entries.push_back(entry);
            }
            if (!node.object_entries.empty())
                node.object_index = node.object_entries[0].object_index;
            result.nodes_.push_back(std::move(node));
        }
        compose(result.nodes_, limits);
        return result;
    } catch (const Failure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(Scene3DDError{Scene3DDErrorCode::resource_limit,
                                             0, "Scene allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            Scene3DDError{Scene3DDErrorCode::resource_limit, 0,
                          "Scene allocation length exceeded"});
    }
}

std::span<const Scene3DDObject> Scene3DD::objects() const noexcept {
    return objects_;
}
std::span<const Scene3DDMaterialBank>
Scene3DD::material_banks() const noexcept {
    return material_banks_;
}
std::span<const Scene3DDNode> Scene3DD::nodes() const noexcept {
    return nodes_;
}

std::expected<std::vector<Scene3DDGeometryBinding>, Scene3DDError>
Scene3DD::resolve_geometry(Bytes bytes) const {
    try {
        Reader reader(bytes, limits_);
        reader.file(0x00030003, ".3DG");
        const auto roots = reader.children(16, reader.size());
        const auto geo =
            std::find_if(roots.begin(), roots.end(),
                         [](const auto& c) { return c.is("GEOA"); });
        if (geo == roots.end() ||
            std::count_if(roots.begin(), roots.end(),
                          [](const auto& c) { return c.is("GEOA"); }) != 1) {
            fail(Scene3DDErrorCode::geometry_mismatch, 16,
                 "Expected one paired GEOA container");
        }
        const auto groups = reader.children(*geo);
        const auto keys =
            hash(reader, groups, "GEOM", limits_.max_objects, true);
        const auto groupRecords = index(groups);
        std::map<Scene3DDKey,
                 std::pair<std::uint32_t, std::vector<std::uint32_t>>>
            geometry;
        for (const auto& item : keys) {
            const auto primitives =
                reader.children(*groupRecords.at(item.target));
            std::vector<std::uint32_t> bindings;
            for (const auto& primitive : primitives) {
                if (!primitive.is("PRIM") || primitive.size < 0x30 ||
                    primitive.layout != primitive.size) {
                    fail(Scene3DDErrorCode::unsupported_layout,
                         primitive.offset, "Unsupported paired PRIM layout");
                }
                bindings.push_back(reader.u32(primitive.offset + 0x28));
            }
            geometry.emplace(item.key,
                             std::pair{item.target, std::move(bindings)});
        }
        for (const auto& object : objects_) {
            const auto found = geometry.find(object.key);
            if (found == geometry.end() ||
                found->second.second.size() != object.primitive_count) {
                fail(Scene3DDErrorCode::geometry_mismatch, object.source_offset,
                     "Paired GEOM exact key/primitive count differs");
            }
        }
        std::vector<Scene3DDGeometryBinding> result;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            for (std::size_t entry_index = 0;
                 entry_index < nodes_[i].object_entries.size(); ++entry_index) {
                const auto object =
                    nodes_[i].object_entries[entry_index].object_index;
                const auto found = geometry.find(objects_[object].key);
                if (found == geometry.end() ||
                    found->second.second.size() !=
                        objects_[object].primitive_count) {
                    fail(Scene3DDErrorCode::geometry_mismatch,
                         objects_[object].source_offset,
                         "Paired GEOM exact key/primitive count differs");
                }
                reader.claim(1, sizeof(Scene3DDGeometryBinding),
                             nodes_[i].hierarchy_offset);
                reader.claim(found->second.second.size(), sizeof(std::uint32_t),
                             nodes_[i].hierarchy_offset);
                result.push_back({i, object, found->second.first,
                                  found->second.second, entry_index});
            }
        }
        return result;
    } catch (const Failure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            Scene3DDError{Scene3DDErrorCode::resource_limit, 0,
                          "Geometry binding allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            Scene3DDError{Scene3DDErrorCode::resource_limit, 0,
                          "Geometry binding allocation length exceeded"});
    }
}

} // namespace tdur::assets

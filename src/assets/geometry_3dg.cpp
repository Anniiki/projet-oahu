#include "tdur/assets/geometry_3dg.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <unordered_map>
#include <utility>

namespace tdur::assets {
namespace {

using Data = std::span<const std::byte>;

constexpr std::uint32_t tag(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(a) |
           (static_cast<std::uint32_t>(b) << 8U) |
           (static_cast<std::uint32_t>(c) << 16U) |
           (static_cast<std::uint32_t>(d) << 24U);
}

std::unexpected<Geometry3DGError>
fail(Geometry3DGErrorCode code, std::size_t offset, std::string message) {
    return std::unexpected(Geometry3DGError{code, offset, std::move(message)});
}

std::uint16_t u16(Data b, std::size_t p) {
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned int>(b[p]) |
        (std::to_integer<unsigned int>(b[p + 1]) << 8U));
}

std::uint32_t u32(Data b, std::size_t p) {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        result |= std::to_integer<std::uint32_t>(b[p + i]) << (i * 8U);
    }
    return result;
}

float f32(Data b, std::size_t p) { return std::bit_cast<float>(u32(b, p)); }

struct Chunk {
    std::size_t offset{};
    std::size_t size{};
    std::uint32_t kind{};
    std::size_t end() const { return offset + size; }
};

std::expected<Chunk, Geometry3DGError> chunk(Data bytes, std::size_t offset,
                                             std::size_t end) {
    if (end > bytes.size() || offset > end || end - offset < 16) {
        return fail(Geometry3DGErrorCode::truncated, offset,
                    "Chunk header is outside its parent");
    }
    const auto size = u32(bytes, offset + 8);
    if (size < 16 || (size % 16U) != 0 || size > end - offset) {
        return fail(Geometry3DGErrorCode::invalid_chunk, offset + 8,
                    "Chunk length is unaligned or outside its parent");
    }
    return Chunk{offset, size, u32(bytes, offset)};
}

std::expected<std::vector<Chunk>, Geometry3DGError>
children(Data bytes, const Chunk& parent, std::size_t maximum) {
    if (u32(bytes, parent.offset + 12) != 16) {
        return fail(Geometry3DGErrorCode::unsupported_layout,
                    parent.offset + 12, "Expected a 16-byte container header");
    }
    std::vector<Chunk> result;
    for (auto cursor = parent.offset + 16; cursor < parent.end();) {
        if (result.size() >= maximum) {
            return fail(Geometry3DGErrorCode::resource_limit, cursor,
                        "Child chunk count exceeds the configured limit");
        }
        auto next = chunk(bytes, cursor, parent.end());
        if (!next) {
            return std::unexpected(next.error());
        }
        result.push_back(*next);
        cursor = next->end();
    }
    return result;
}

using ChunkMap = std::unordered_map<std::size_t, Chunk>;

std::expected<ChunkMap, Geometry3DGError> gpu_buffers(Data bytes,
                                                      const Chunk& parent,
                                                      std::uint32_t expected,
                                                      std::size_t maximum) {
    const auto list = children(bytes, parent, maximum);
    if (!list) {
        return std::unexpected(list.error());
    }
    ChunkMap result;
    for (const auto& entry : *list) {
        if (entry.kind != expected || entry.size < 80 ||
            u32(bytes, entry.offset + 12) != entry.size ||
            u32(bytes, entry.offset + 16) != tag('G', 'U', 'N', ' ') ||
            u32(bytes, entry.offset + 20) != tag('T', 'I', 'M', 'E')) {
            return fail(Geometry3DGErrorCode::unsupported_layout, entry.offset,
                        "Expected a DX buffer with its 80-byte prefix");
        }
        result.emplace(entry.offset, entry);
    }
    return result;
}

template <class Emit>
std::expected<std::size_t, Geometry3DGError>
visit_triangles(Data bytes, std::size_t start, std::size_t count,
                std::size_t vertices, Emit emit) {
    std::uint32_t previous0 = 0;
    std::uint32_t previous1 = 0;
    unsigned int history = 0;
    bool odd = false;
    std::size_t triangles = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = u16(bytes, start + i * 2);
        if (value == 0xffffU) {
            history = 0;
            odd = false;
            continue;
        }
        if (value >= vertices) {
            return fail(Geometry3DGErrorCode::invalid_indices, start + i * 2,
                        "Strip index exceeds the primitive vertex count");
        }
        if (history == 0) {
            previous0 = value;
            history = 1;
        } else if (history == 1) {
            previous1 = value;
            history = 2;
        } else {
            if (previous0 != previous1 && previous0 != value &&
                previous1 != value) {
                emit(odd ? previous1 : previous0, odd ? previous0 : previous1,
                     static_cast<std::uint32_t>(value));
                ++triangles;
            }
            odd = !odd;
            previous0 = previous1;
            previous1 = value;
        }
    }
    return triangles;
}

std::expected<Geometry3DGPrimitive, Geometry3DGError>
primitive(Data bytes, const Chunk& source, const ChunkMap& vertex_buffers,
          const ChunkMap& index_buffers, const Geometry3DGLimits& limits,
          std::size_t& output_bytes) {
    if (source.kind != tag('P', 'R', 'I', 'M') || source.size < 0x90 ||
        u32(bytes, source.offset + 12) != source.size) {
        return fail(Geometry3DGErrorCode::unsupported_layout, source.offset,
                    "Expected a sufficiently large PRIM leaf");
    }
    const auto p = source.offset;
    const auto vertex_ref = u32(bytes, p + 28);
    const auto index_ref = u32(bytes, p + 24);
    const auto vertex = vertex_buffers.find(vertex_ref);
    const auto index = index_buffers.find(index_ref);
    if (vertex == vertex_buffers.end() || index == index_buffers.end()) {
        return fail(Geometry3DGErrorCode::invalid_reference, p + 24,
                    "PRIM does not reference declared DXVB/DXIB chunks");
    }
    if (u32(bytes, p + 20) != 4 || u32(bytes, p + 48) != 0 ||
        u32(bytes, p + 56) != 0 || u32(bytes, p + 64) != 1 ||
        u32(bytes, p + 0x88) != 0) {
        return fail(Geometry3DGErrorCode::unsupported_layout, p + 20,
                    "Only zero-based strips with a leading float3 stream are "
                    "supported");
    }
    const std::size_t vertices = u32(bytes, p + 52);
    const std::size_t indices = u32(bytes, p + 60);
    if (vertices > limits.max_vertices_per_primitive ||
        indices > limits.max_strip_indices_per_primitive) {
        return fail(
            Geometry3DGErrorCode::resource_limit, p + 52,
            "Primitive vertex/index count exceeds the configured limit");
    }
    const auto vertex_start = vertex->second.offset + 80;
    const auto index_start = index->second.offset + 80;
    if (vertices > (vertex->second.size - 80) / 12 ||
        indices > (index->second.size - 80) / 2) {
        return fail(Geometry3DGErrorCode::invalid_reference, p + 52,
                    "Primitive counts exceed referenced buffer lengths");
    }
    auto triangles = visit_triangles(bytes, index_start, indices, vertices,
                                     [](auto, auto, auto) {});
    if (!triangles) {
        return std::unexpected(triangles.error());
    }
    Geometry3DGPrimitive result;
    result.source_offset = static_cast<std::uint32_t>(p);
    result.vertex_buffer_offset = vertex_ref;
    result.index_buffer_offset = index_ref;
    result.opaque_binding_word = u32(bytes, p + 40);
    for (std::size_t i = 0; i < result.vertex_declaration_words.size(); ++i) {
        result.vertex_declaration_words[i] = u32(bytes, p + 64 + i * 4);
    }
    const auto normal = result.vertex_declaration_words[1];
    const auto color = result.vertex_declaration_words[2];
    const auto uv = result.vertex_declaration_words[4];
    const bool known_uv = (normal == 0x900U || normal == 0x901U) &&
                          (color == 0x200U || color == 0x201U) &&
                          result.vertex_declaration_words[3] == 0 &&
                          (uv == 0xc01U || uv == 0xc02U);
    std::size_t uv_start = 0;
    if (known_uv) {
        const std::size_t uv_stream = 1 + (normal & 1U) + (color & 1U);
        const auto offset_field = p + 0x88 + uv_stream * 4;
        if (offset_field + 4 > source.end()) {
            return fail(Geometry3DGErrorCode::invalid_reference, offset_field,
                        "Float2 stream offset is outside PRIM");
        }
        const std::size_t uv_offset = u32(bytes, offset_field);
        const auto capacity = vertex->second.size - 80;
        if (uv_offset < vertices * 12 || uv_offset > capacity ||
            vertices > (capacity - uv_offset) / 8) {
            return fail(Geometry3DGErrorCode::invalid_reference, offset_field,
                        "Float2 stream is outside the declared vertex buffer");
        }
        uv_start = vertex_start + uv_offset;
    }
    const auto required =
        static_cast<std::uint64_t>(vertices) * (known_uv ? 20U : 12U) +
        static_cast<std::uint64_t>(*triangles) * 12U;
    if (output_bytes > limits.max_output_bytes ||
        required > limits.max_output_bytes - output_bytes) {
        return fail(Geometry3DGErrorCode::resource_limit, p,
                    "Decoded geometry exceeds the configured output limit");
    }
    output_bytes += static_cast<std::size_t>(required);
    result.positions.resize(vertices);
    if (known_uv) {
        result.texcoords.resize(vertices);
    }
    for (std::size_t i = 0; i < vertices; ++i) {
        auto& position = result.positions[i];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto field = vertex_start + i * 12 + axis * 4;
            position[axis] = f32(bytes, field);
            if (!std::isfinite(position[axis])) {
                return fail(Geometry3DGErrorCode::non_finite_vertex, field,
                            "Position contains NaN or infinity");
            }
            if (i == 0) {
                result.bounds.minimum[axis] = position[axis];
                result.bounds.maximum[axis] = position[axis];
            } else {
                result.bounds.minimum[axis] =
                    std::min(result.bounds.minimum[axis], position[axis]);
                result.bounds.maximum[axis] =
                    std::max(result.bounds.maximum[axis], position[axis]);
            }
        }
        if (known_uv) {
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const auto field = uv_start + i * 8 + axis * 4;
                result.texcoords[i][axis] = f32(bytes, field);
                if (!std::isfinite(result.texcoords[i][axis])) {
                    return fail(Geometry3DGErrorCode::non_finite_vertex, field,
                                "UV coordinate contains NaN or infinity");
                }
            }
        }
    }
    result.triangle_indices.reserve(*triangles * 3);
    const auto emitted = visit_triangles(
        bytes, index_start, indices, vertices,
        [&result](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            result.triangle_indices.insert(result.triangle_indices.end(),
                                           {a, b, c});
        });
    if (!emitted) {
        return std::unexpected(emitted.error());
    }
    return result;
}

} // namespace

std::expected<Geometry3DG, Geometry3DGError>
Geometry3DG::parse(Data bytes, Geometry3DGLimits limits) {
    try {
        if (bytes.size() > limits.max_file_bytes ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return fail(Geometry3DGErrorCode::resource_limit, 0,
                        "Geometry file exceeds the configured byte limit");
        }
        if (bytes.size() < 16) {
            return fail(Geometry3DGErrorCode::truncated, 0,
                        "A 3DG file header requires16 bytes");
        }
        if (u32(bytes, 0) != 0x00030003U ||
            u32(bytes, 12) != tag('.', '3', 'D', 'G')) {
            return fail(Geometry3DGErrorCode::unsupported_version, 0,
                        "Only PC little-endian 3DG version3.3 is supported");
        }
        if (u32(bytes, 8) != bytes.size()) {
            return fail(Geometry3DGErrorCode::invalid_chunk, 8,
                        "Declared geometry length differs from stored data");
        }
        std::unordered_map<std::uint32_t, Chunk> roots;
        for (std::size_t cursor = 16; cursor < bytes.size();) {
            const auto next = chunk(bytes, cursor, bytes.size());
            if (!next) {
                return std::unexpected(next.error());
            }
            const auto kind = next->kind;
            if (kind != tag('G', 'E', 'O', 'A') &&
                kind != tag('V', 'B', 'A', '.') &&
                kind != tag('I', 'B', 'A', '.') &&
                kind != tag('M', 'O', 'R', 'A') &&
                kind != tag('R', 'E', 'A', 'L')) {
                return fail(Geometry3DGErrorCode::unsupported_layout, cursor,
                            "Unknown root chunk in the supported PC subset");
            }
            if (!roots.emplace(kind, *next).second) {
                return fail(Geometry3DGErrorCode::invalid_chunk, cursor,
                            "A root chunk is duplicated");
            }
            cursor = next->end();
        }
        if (!roots.contains(tag('G', 'E', 'O', 'A')) ||
            !roots.contains(tag('V', 'B', 'A', '.')) ||
            !roots.contains(tag('I', 'B', 'A', '.'))) {
            return fail(Geometry3DGErrorCode::invalid_chunk, 16,
                        "Missing geometry, vertex or index container");
        }
        const auto vertex_buffers =
            gpu_buffers(bytes, roots.at(tag('V', 'B', 'A', '.')),
                        tag('D', 'X', 'V', 'B'), limits.max_primitives);
        if (!vertex_buffers) {
            return std::unexpected(vertex_buffers.error());
        }
        const auto index_buffers =
            gpu_buffers(bytes, roots.at(tag('I', 'B', 'A', '.')),
                        tag('D', 'X', 'I', 'B'), limits.max_primitives);
        if (!index_buffers) {
            return std::unexpected(index_buffers.error());
        }
        const auto geometry_children = children(
            bytes, roots.at(tag('G', 'E', 'O', 'A')), limits.max_groups + 1);
        if (!geometry_children) {
            return std::unexpected(geometry_children.error());
        }
        std::unordered_map<std::size_t, std::string> names;
        bool have_hash = false;
        for (const auto& item : *geometry_children) {
            if (item.kind != tag('H', 'A', 'S', 'H')) {
                continue;
            }
            if (have_hash || (item.size - 16) / 16 > limits.max_groups ||
                u32(bytes, item.offset + 12) != item.size) {
                return fail(Geometry3DGErrorCode::invalid_chunk, item.offset,
                            "Invalid or duplicated HASH table");
            }
            have_hash = true;
            for (auto p = item.offset + 16; p < item.end(); p += 16) {
                std::string name;
                for (std::size_t i = 0; i < 8 && bytes[p + i] != std::byte{0};
                     ++i) {
                    name += static_cast<char>(
                        std::to_integer<unsigned char>(bytes[p + i]));
                }
                if (!names.emplace(u32(bytes, p + 8), std::move(name)).second) {
                    return fail(Geometry3DGErrorCode::invalid_reference, p + 8,
                                "Two HASH records reference the same GEOM");
                }
            }
        }
        if (!have_hash) {
            return fail(Geometry3DGErrorCode::invalid_chunk, 16,
                        "The supported subset requires a HASH name table");
        }
        Geometry3DG result;
        std::size_t primitive_count = 0;
        std::size_t output_bytes = 0;
        for (const auto& item : *geometry_children) {
            if (item.kind == tag('H', 'A', 'S', 'H')) {
                continue;
            }
            if (item.kind != tag('G', 'E', 'O', 'M') ||
                result.groups_.size() >= limits.max_groups) {
                return fail(Geometry3DGErrorCode::unsupported_layout,
                            item.offset,
                            "Expected a bounded list of GEOM containers");
            }
            const auto name = names.find(item.offset);
            if (name == names.end()) {
                return fail(Geometry3DGErrorCode::invalid_reference,
                            item.offset, "GEOM has no matching HASH record");
            }
            Geometry3DGGroup group{
                name->second, static_cast<std::uint32_t>(item.offset), {}};
            const auto primitives =
                children(bytes, item, limits.max_primitives);
            if (!primitives) {
                return std::unexpected(primitives.error());
            }
            for (const auto& source : *primitives) {
                if (primitive_count >= limits.max_primitives) {
                    return fail(Geometry3DGErrorCode::resource_limit,
                                source.offset, "Too many primitives");
                }
                ++primitive_count;
                auto decoded = primitive(bytes, source, *vertex_buffers,
                                         *index_buffers, limits, output_bytes);
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                group.primitives.push_back(std::move(*decoded));
            }
            result.groups_.push_back(std::move(group));
        }
        if (result.groups_.size() != names.size()) {
            return fail(Geometry3DGErrorCode::invalid_reference, 16,
                        "HASH records do not match the GEOM containers");
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(Geometry3DGErrorCode::resource_limit, 0,
                    "Insufficient memory for decoded geometry");
    }
}

std::span<const Geometry3DGGroup> Geometry3DG::groups() const noexcept {
    return groups_;
}

} // namespace tdur::assets

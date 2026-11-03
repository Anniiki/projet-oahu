#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace tdur::assets {

struct Geometry3DGLimits {
    std::size_t max_file_bytes = 256U * 1024U * 1024U;
    std::size_t max_groups = 10'000;
    std::size_t max_primitives = 100'000;
    std::size_t max_vertices_per_primitive = 1'000'000;
    std::size_t max_strip_indices_per_primitive = 3'000'000;
    std::size_t max_output_bytes = 256U * 1024U * 1024U;
};

enum class Geometry3DGErrorCode {
    truncated,
    unsupported_version,
    unsupported_layout,
    invalid_chunk,
    invalid_reference,
    invalid_indices,
    non_finite_vertex,
    resource_limit,
};

struct Geometry3DGError {
    Geometry3DGErrorCode code{};
    std::size_t offset{};
    std::string detail;
};

struct Geometry3DGBounds {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

struct Geometry3DGPrimitive {
    std::uint32_t source_offset{};
    std::uint32_t vertex_buffer_offset{};
    std::uint32_t index_buffer_offset{};
    // Source PRIM+0x28, retained without assigning an unproven material ID.
    std::uint32_t opaque_binding_word{};
    std::array<std::uint32_t, 8> vertex_declaration_words{};
    std::vector<std::array<float, 3>> positions;
    // First float2 UV set for supported declarations; empty when unavailable.
    std::vector<std::array<float, 2>> texcoords;
    // Triangle list local to positions. Degenerates are omitted, strip
    // parity is preserved, and 0xffff restarts a source strip.
    std::vector<std::uint32_t> triangle_indices;
    Geometry3DGBounds bounds;
};

struct Geometry3DGGroup {
    // Original eight-byte HASH key, trimmed at its first NUL; not guaranteed
    // UTF-8 and not necessarily a complete human-readable object name.
    std::string name_bytes;
    std::uint32_t source_offset{};
    std::vector<Geometry3DGPrimitive> primitives;
};

class Geometry3DG {
  public:
    // Structured PC 3.3 subset: HASH -> GEOM -> PRIM and referenced DXVB/DXIB.
    // Owns all decoded output; input may be released after success.
    // Coordinates stay in source mesh-local space. No .3DD transforms,
    // material assignment, axis conversion or normal decoding is invented.
    [[nodiscard]] static std::expected<Geometry3DG, Geometry3DGError>
    parse(std::span<const std::byte> bytes, Geometry3DGLimits limits = {});

    [[nodiscard]] std::span<const Geometry3DGGroup> groups() const noexcept;

  private:
    std::vector<Geometry3DGGroup> groups_;
};

} // namespace tdur::assets

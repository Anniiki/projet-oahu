#pragma once

#include "tdur/assets/scene_3dd.hpp"

namespace tdur::assets {

using Material2DMKey = std::array<std::byte, 8>;

struct Material2DMLimits {
    std::size_t max_file_bytes = 256U * 1024U * 1024U;
    std::size_t max_materials = 100'000;
    std::size_t max_parameters = 100'000;
    std::size_t max_layers = 100'000;
    std::size_t max_relocations = 1'000'000;
    // Logical owned output, excluding spare capacity/allocator overhead and
    // temporary validation indexes (bounded by the file and count limits).
    std::size_t max_output_bytes = 256U * 1024U * 1024U;
};

enum class Material2DMErrorCode {
    truncated,
    unsupported_version,
    unsupported_layout,
    invalid_chunk,
    invalid_reference,
    invalid_hash,
    resource_limit,
    invalid_material_bank,
    invalid_material_slot,
    invalid_fallback,
    fallback_required,
};

struct Material2DMError {
    Material2DMErrorCode code{};
    std::size_t offset{};
    std::string detail;
};

struct Material2DMLayer {
    std::uint32_t source_offset{};
    // Complete LAY payload, without inventing texture/sampler semantics.
    std::array<std::byte, 0x110> opaque_payload;
};

struct Material2DMParameters {
    std::uint32_t source_offset{};
    std::optional<std::size_t> layer_index;
    // Includes the original 32-bit link word, never a host pointer.
    std::vector<std::byte> opaque_payload;
};

struct Material2DMEntry {
    Material2DMKey key;
    std::uint32_t hash_entry_offset{};
    std::uint32_t source_offset{};
    std::optional<std::size_t> parameters_index;
    // MAT payload+0 is a classification word, not a proven shader ID.
    std::uint16_t classification_word{};
    std::uint32_t render_flags{};
    std::array<std::byte, 0xe0> opaque_payload;
};

enum class MaterialFallbackReason {
    none,
    absent_bank,
    sentinel,
    unresolved_key
};

struct Material2DMBinding {
    std::size_t material_index{};
    std::optional<std::uint16_t> source_slot;
    MaterialFallbackReason fallback_reason{MaterialFallbackReason::none};
};

// Strict observed PC v2 subset. All decoded storage is owned and offsets
// stay32-bit even in the native x64 client. Parsing does not establish that
// a material's opaque layers can be rendered, nor choose a shader or LOD.
class Material2DM {
  public:
    [[nodiscard]] static std::expected<Material2DM, Material2DMError>
    parse(std::span<const std::byte> bytes, Material2DMLimits limits = {});

    [[nodiscard]] std::span<const Material2DMEntry> materials() const noexcept;
    [[nodiscard]] std::span<const Material2DMParameters>
    parameters() const noexcept;
    [[nodiscard]] std::span<const Material2DMLayer> layers() const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    find(const Material2DMKey& key) const noexcept;

    // Select PRIM bits16..31 through the instance's MBNK. Missing/sentinel/
    // unresolved entries need a caller-supplied known fallback. Raw pointer
    // cache words from the 32-bit file are never used as host addresses.
    [[nodiscard]] std::expected<Material2DMBinding, Material2DMError>
    bind_primitive(const Scene3DD& scene,
                   std::optional<std::size_t> material_bank_index,
                   std::uint32_t primitive_binding_word,
                   std::optional<std::size_t> fallback_material = {}) const;

  private:
    std::vector<Material2DMEntry> materials_;
    std::vector<Material2DMParameters> parameters_;
    std::vector<Material2DMLayer> layers_;
    // Sorted indexes into materials_, using the native unsigned high/low
    // DWORD key comparison. The source HASH order is retained separately.
    std::vector<std::size_t> key_order_;
};

} // namespace tdur::assets

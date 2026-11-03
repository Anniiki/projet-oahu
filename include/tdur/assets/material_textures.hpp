#pragma once

#include "tdur/assets/material_2dm.hpp"
#include "tdur/assets/texture_2db.hpp"

namespace tdur::assets {

enum class MaterialMinMagFilter { point, linear };
enum class MaterialMipFilter { none, linear };
enum class MaterialAddressMode { wrap, clamp, mirror, border };

struct MaterialTextureSampler {
    MaterialMinMagFilter min_mag{MaterialMinMagFilter::linear};
    MaterialMipFilter mip{MaterialMipFilter::linear};
    MaterialAddressMode address_u{MaterialAddressMode::wrap};
    MaterialAddressMode address_v{MaterialAddressMode::wrap};
    // W is fixed CLAMP by the observed native path. Border color, sRGB and
    // anisotropy are not established by these slot fields.
};

struct MaterialTextureSlot {
    std::uint32_t source_offset{};
    Texture2DBKey key{};
    MaterialTextureSampler sampler;
    // MIN/MAG, MIP, U, V indices, before the established table conversion.
    std::array<std::uint8_t, 4> sampler_indices{};
    // Includes the original cache/auxiliary words; neither is a host pointer.
    std::array<std::byte, 32> source_bytes{};
};

struct MaterialTextureLayer {
    std::uint32_t source_offset{};
    std::uint8_t slot_count{};
    std::array<MaterialTextureSlot, 8> slots;
};

struct MaterialTextureLayout {
    std::size_t material_index{};
    std::size_t parameter_index{};
    std::size_t layer_index{};
};

struct MaterialTextureLimits {
    std::size_t max_materials = 100'000;
    std::size_t max_layers = 100'000;
    std::size_t max_loaded_resources = 100'000;
    std::size_t max_rebind_comparisons = 50'000'000;
    // Logical owned layouts/layers/caches; excludes allocator spare capacity.
    std::size_t max_output_bytes = 256U * 1024U * 1024U;
};

enum class MaterialTextureErrorCode {
    invalid_material,
    unsupported_parameter_count,
    missing_parameters,
    unsupported_parameter_type,
    missing_layer,
    invalid_reference,
    invalid_slot_count,
    unsupported_sampler,
    resource_limit,
};
struct MaterialTextureError {
    MaterialTextureErrorCode code{};
    std::size_t source_offset{};
    std::string detail;
};

// Caller-defined resource identity, never an original pointer or GPU address.
// Token 0 is valid. Resources must remain alive while a cache can refer to
// them.
struct MaterialTextureResource {
    Texture2DBKey key{};
    std::uint64_t token{};
};

enum class MaterialTextureAction {
    bind_cached,
    require_fallback,
    keep_previous
};
struct MaterialTextureStage {
    MaterialTextureAction action{MaterialTextureAction::keep_previous};
    std::optional<std::uint64_t> texture_token;
    MaterialTextureSampler sampler;
};
struct MaterialTexturePlan {
    std::uint8_t stage_count{};
    bool suppressed{};
    std::array<MaterialTextureStage, 8> stages;
};

// Type-2 path from 601F90/505060, present in official trial23. One owner per
// material collection: caches are shared by all MAT aliases of the same LAY.
// All declared layers are retained, including unreferenced ones. Starts with
// empty typed caches, ignoring disk pointer words. No constants, shaders, GPU
// upload or draw calls. The host serializes uses and pins token resources.
class MaterialTextureBindings {
  public:
    [[nodiscard]] static std::expected<MaterialTextureBindings,
                                       MaterialTextureError>
    from_materials(const Material2DM& materials,
                   MaterialTextureLimits limits = {});

    // Same material and physical LAY order as Material2DM.
    [[nodiscard]] std::span<const MaterialTextureLayout>
    layouts() const noexcept;
    [[nodiscard]] std::span<const MaterialTextureLayer> layers() const noexcept;

    // Explicit loaded collection, in caller-supplied order. Each matching
    // raw8 key overwrites its cache, including zero keys; later matches win.
    // Missing matches preserve old caches. Count refusal precedes all writes.
    // The returned count includes every matching store, even repeated ones.
    [[nodiscard]] std::expected<std::size_t, MaterialTextureError>
    rebind(std::span<const MaterialTextureResource> loaded);

    // 601F90 skips textures AND samplers when global_flags & 0x42 != 0.
    // Otherwise each active ordinal requests its sampler, even if its zero
    // key/empty cache preserves the previous texture. The unknown native
    // fallback FAD8F0 is requested explicitly, never synthesized as white.
    [[nodiscard]] std::expected<MaterialTexturePlan, MaterialTextureError>
    plan(std::size_t material_index, std::uint32_t global_flags = 0) const;

  private:
    MaterialTextureBindings() = default;
    MaterialTextureLimits limits_;
    std::vector<MaterialTextureLayout> layouts_;
    std::vector<MaterialTextureLayer> layers_;
    std::vector<std::array<std::optional<std::uint64_t>, 8>> cached_;
    std::size_t active_slots_{};
};

} // namespace tdur::assets

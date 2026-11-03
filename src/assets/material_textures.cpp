#include "tdur/assets/material_textures.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace tdur::assets {
namespace {
struct Failure {
    MaterialTextureError error;
};
[[noreturn]] void fail(MaterialTextureErrorCode code, std::size_t offset,
                       const char* detail) {
    throw Failure{{code, offset, detail}};
}
std::uint16_t u16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned>(bytes[offset]) |
        (std::to_integer<unsigned>(bytes[offset + 1]) << 8));
}
std::uint32_t u32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t result{};
    for (unsigned i = 0; i < 4; ++i)
        result |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8);
    return result;
}
MaterialAddressMode address_mode(std::uint8_t index, std::size_t offset) {
    switch (index) {
    case 0:
    case 1:
        return MaterialAddressMode::wrap;
    case 2:
        return MaterialAddressMode::clamp;
    case 3:
        return MaterialAddressMode::mirror;
    case 4:
        return MaterialAddressMode::border;
    default:
        fail(MaterialTextureErrorCode::unsupported_sampler, offset,
             "Unknown native texture address index");
    }
}
MaterialTextureSampler sampler(const std::array<std::uint8_t, 4>& indices,
                               std::uint32_t slot_offset) {
    MaterialTextureSampler result;
    switch (indices[0]) {
    case 0:
    case 2:
        result.min_mag = MaterialMinMagFilter::linear;
        break;
    case 1:
        result.min_mag = MaterialMinMagFilter::point;
        break;
    default:
        fail(MaterialTextureErrorCode::unsupported_sampler, slot_offset + 0x16,
             "Unknown native MIN/MAG filter index");
    }
    switch (indices[1]) {
    case 0:
    case 2:
        result.mip = MaterialMipFilter::linear;
        break;
    case 1:
        result.mip = MaterialMipFilter::none;
        break;
    default:
        // Native table index3 yields ANISOTROPIC, which is not a valid MIP
        // filter. Neither that value nor unknown indices become LINEAR.
        fail(MaterialTextureErrorCode::unsupported_sampler, slot_offset + 0x17,
             "Native MIP filter is unknown or unsupported for mip sampling");
    }
    result.address_u = address_mode(indices[2], slot_offset + 0x18);
    result.address_v = address_mode(indices[3], slot_offset + 0x19);
    return result;
}
} // namespace

std::expected<MaterialTextureBindings, MaterialTextureError>
MaterialTextureBindings::from_materials(const Material2DM& materials,
                                        MaterialTextureLimits limits) {
    try {
        if (materials.materials().size() > limits.max_materials ||
            materials.layers().size() > limits.max_layers)
            fail(MaterialTextureErrorCode::resource_limit, 0,
                 "Material or LAY count exceeds the collection budget");
        MaterialTextureBindings result;
        result.limits_ = limits;
        std::size_t used{};
        const auto claim = [&](std::size_t count, std::size_t stride) {
            if (count > (limits.max_output_bytes - used) / stride)
                fail(MaterialTextureErrorCode::resource_limit, 0,
                     "Material texture logical output exceeds byte budget");
            used += count * stride;
        };
        claim(materials.materials().size(), sizeof(MaterialTextureLayout));
        claim(materials.layers().size(), sizeof(MaterialTextureLayer));
        claim(materials.layers().size(),
              sizeof(std::array<std::optional<std::uint64_t>, 8>));
        result.layouts_.reserve(materials.materials().size());
        result.layers_.reserve(materials.layers().size());
        result.cached_.resize(materials.layers().size());
        for (const auto& layer : materials.layers()) {
            const auto count =
                std::to_integer<std::uint8_t>(layer.opaque_payload[0xc]);
            if (count > 8)
                fail(MaterialTextureErrorCode::invalid_slot_count,
                     layer.source_offset + 0x1c,
                     "LAY byte count exceeds its eight physical slots");
            MaterialTextureLayer decoded{layer.source_offset, count, {}};
            for (std::size_t i = 0; i < count; ++i) {
                auto& slot = decoded.slots[i];
                slot.source_offset = layer.source_offset + 0x20 +
                                     static_cast<std::uint32_t>(i) * 32;
                std::copy_n(layer.opaque_payload.begin() + 0x10 + i * 32, 32,
                            slot.source_bytes.begin());
                std::copy_n(slot.source_bytes.begin() + 8, slot.key.size(),
                            slot.key.begin());
                for (std::size_t j = 0; j < slot.sampler_indices.size(); ++j)
                    slot.sampler_indices[j] = std::to_integer<std::uint8_t>(
                        slot.source_bytes[0x16 + j]);
                slot.sampler =
                    sampler(slot.sampler_indices, slot.source_offset);
            }
            result.layers_.push_back(std::move(decoded));
            result.active_slots_ += count;
        }
        for (std::size_t i = 0; i < materials.materials().size(); ++i) {
            const auto& material = materials.materials()[i];
            if (u16(material.opaque_payload, 2) != 1)
                fail(MaterialTextureErrorCode::unsupported_parameter_count,
                     material.source_offset + 0x12,
                     "Only one parameter per material is established here");
            if (!material.parameters_index)
                fail(MaterialTextureErrorCode::missing_parameters,
                     material.source_offset + 0xe0,
                     "Material has no declared parameter reference");
            if (*material.parameters_index >= materials.parameters().size())
                fail(MaterialTextureErrorCode::invalid_reference,
                     material.source_offset + 0xe0,
                     "Material parameter index is outside the parsed library");
            const auto& parameter =
                materials.parameters()[*material.parameters_index];
            if (parameter.opaque_payload.size() < 4)
                fail(MaterialTextureErrorCode::invalid_reference,
                     parameter.source_offset + 0x10,
                     "Parameter has no complete type word");
            if (u32(parameter.opaque_payload, 0) != 2)
                fail(MaterialTextureErrorCode::unsupported_parameter_type,
                     parameter.source_offset + 0x10,
                     "Only the observed type-2 texture parameter is supported");
            if (!parameter.layer_index)
                fail(MaterialTextureErrorCode::missing_layer,
                     parameter.source_offset + 0x54,
                     "Texture parameter has no declared LAY reference");
            if (*parameter.layer_index >= result.layers_.size())
                fail(MaterialTextureErrorCode::invalid_reference,
                     parameter.source_offset + 0x54,
                     "Texture layer index is outside the parsed library");
            result.layouts_.push_back(
                {i, *material.parameters_index, *parameter.layer_index});
        }
        return result;
    } catch (const Failure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            MaterialTextureError{MaterialTextureErrorCode::resource_limit, 0,
                                 "Texture collection allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            MaterialTextureError{MaterialTextureErrorCode::resource_limit, 0,
                                 "Texture collection allocation is too large"});
    }
}

std::span<const MaterialTextureLayout>
MaterialTextureBindings::layouts() const noexcept {
    return layouts_;
}
std::span<const MaterialTextureLayer>
MaterialTextureBindings::layers() const noexcept {
    return layers_;
}

std::expected<std::size_t, MaterialTextureError>
MaterialTextureBindings::rebind(
    std::span<const MaterialTextureResource> loaded) {
    if (loaded.size() > limits_.max_loaded_resources ||
        (active_slots_ &&
         loaded.size() > limits_.max_rebind_comparisons / active_slots_))
        return std::unexpected(MaterialTextureError{
            MaterialTextureErrorCode::resource_limit, 0,
            "Explicit loaded texture collection exceeds the resource budget"});
    // Preserve the native ordered, all-matches overwrite behavior. A search
    // miss does not reset an earlier binding. The typed cache is separate
    // from all 32-bit source words and is never treated as a process address.
    std::size_t stores{};
    for (const auto& resource : loaded) {
        for (std::size_t layer = 0; layer < layers_.size(); ++layer) {
            for (std::size_t i = 0; i < layers_[layer].slot_count; ++i) {
                if (layers_[layer].slots[i].key == resource.key) {
                    cached_[layer][i] = resource.token;
                    ++stores;
                }
            }
        }
    }
    return stores;
}

std::expected<MaterialTexturePlan, MaterialTextureError>
MaterialTextureBindings::plan(std::size_t material_index,
                              std::uint32_t global_flags) const {
    if (material_index >= layouts_.size())
        return std::unexpected(MaterialTextureError{
            MaterialTextureErrorCode::invalid_material, material_index,
            "Material index is outside the decoded texture collection"});
    MaterialTexturePlan result{};
    result.suppressed = (global_flags & 0x42) != 0;
    if (result.suppressed)
        return result;
    const auto layer_index = layouts_[material_index].layer_index;
    const auto& layer = layers_[layer_index];
    result.stage_count = layer.slot_count;
    for (std::size_t i = 0; i < layer.slot_count; ++i) {
        auto& stage = result.stages[i];
        stage.sampler = layer.slots[i].sampler;
        if (cached_[layer_index][i]) {
            stage.action = MaterialTextureAction::bind_cached;
            stage.texture_token = cached_[layer_index][i];
        } else if (std::ranges::any_of(layer.slots[i].key, [](std::byte byte) {
                       return byte != std::byte{0};
                   })) {
            stage.action = MaterialTextureAction::require_fallback;
        }
        // The default action keeps a prior texture on a zero-key/empty-cache
        // slot, while still carrying this slot's sampler request.
    }
    return result;
}
} // namespace tdur::assets

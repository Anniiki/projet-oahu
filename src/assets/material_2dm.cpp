#include "tdur/assets/material_2dm.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <new>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>

namespace tdur::assets {
namespace {
using Bytes = std::span<const std::byte>;
struct Failure {
    Material2DMError error;
};
[[noreturn]] void fail(Material2DMErrorCode code, std::size_t offset,
                       const char* detail) {
    throw Failure{{code, offset, detail}};
}
struct Chunk {
    std::array<char, 4> tag{};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t layout{};
    bool is(std::string_view value) const {
        return std::equal(tag.begin(), tag.end(), value.begin(), value.end());
    }
    std::size_t end() const { return std::size_t(offset) + size; }
};
class Reader {
  public:
    Reader(Bytes bytes, const Material2DMLimits& limits)
        : bytes_(bytes), limits_(limits) {
        if (bytes.size() > limits.max_file_bytes || bytes.size() > UINT32_MAX)
            fail(Material2DMErrorCode::resource_limit, 0,
                 "Material file exceeds byte budget");
    }
    void range(std::size_t offset, std::size_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - offset)
            fail(Material2DMErrorCode::truncated, offset,
                 "Material field exceeds input");
    }
    std::uint32_t u32(std::size_t offset) const {
        range(offset, 4);
        std::uint32_t result{};
        for (unsigned i = 0; i < 4; ++i)
            result |= std::to_integer<std::uint32_t>(bytes_[offset + i])
                      << (i * 8);
        return result;
    }
    std::uint16_t u16(std::size_t offset) const {
        range(offset, 2);
        return static_cast<std::uint16_t>(
            std::to_integer<unsigned>(bytes_[offset]) |
            (std::to_integer<unsigned>(bytes_[offset + 1]) << 8));
    }
    Bytes payload(const Chunk& chunk) const {
        return bytes_.subspan(std::size_t(chunk.offset) + 16, chunk.size - 16);
    }
    Material2DMKey key(std::size_t offset) const {
        range(offset, 8);
        Material2DMKey result{};
        std::copy_n(bytes_.begin() + offset, 8, result.begin());
        return result;
    }
    void claim(std::size_t count, std::size_t stride, std::size_t offset) {
        if (count > (limits_.max_output_bytes - output_bytes_) / stride)
            fail(Material2DMErrorCode::resource_limit, offset,
                 "Material logical output exceeds byte budget");
        output_bytes_ += count * stride;
    }
    std::vector<Chunk> children(std::size_t begin, std::size_t end,
                                std::size_t limit) const {
        range(begin, end >= begin ? end - begin : bytes_.size());
        std::vector<Chunk> result;
        while (begin < end) {
            if (begin % 16 || end - begin < 16)
                fail(Material2DMErrorCode::invalid_chunk, begin,
                     "Unaligned or truncated material chunk");
            if (result.size() >= limit)
                fail(Material2DMErrorCode::resource_limit, begin,
                     "Material chunk count exceeds budget");
            const auto size = u32(begin + 8);
            if (size < 16 || size % 16 || size > end - begin)
                fail(Material2DMErrorCode::invalid_chunk, begin + 8,
                     "Material chunk exceeds parent or alignment");
            if (u32(begin + 4) != 0)
                fail(Material2DMErrorCode::unsupported_layout, begin + 4,
                     "Nonzero opaque chunk header is unsupported");
            Chunk chunk{
                {}, static_cast<std::uint32_t>(begin), size, u32(begin + 12)};
            std::memcpy(chunk.tag.data(), bytes_.data() + begin, 4);
            result.push_back(chunk);
            begin += size;
        }
        return result;
    }
    std::vector<Chunk> children(const Chunk& parent, std::size_t limit) const {
        if (parent.layout != 16)
            fail(Material2DMErrorCode::unsupported_layout, parent.offset + 12,
                 "Expected material container layout");
        return children(parent.offset + 16, parent.end(), limit);
    }
    void leaf(const Chunk& chunk, std::string_view type,
              std::uint32_t size = 0) const {
        if (!chunk.is(type) || chunk.layout != chunk.size ||
            (size && chunk.size != size))
            fail(Material2DMErrorCode::unsupported_layout, chunk.offset,
                 "Unsupported typed material leaf");
    }

  private:
    Bytes bytes_;
    const Material2DMLimits& limits_;
    std::size_t output_bytes_{};
};

std::uint32_t key_word(const Material2DMKey& key, std::size_t offset) noexcept {
    std::uint32_t word{};
    for (unsigned i = 0; i < 4; ++i)
        word |= std::to_integer<std::uint32_t>(key[offset + i]) << (8 * i);
    return word;
}
bool key_less(const Material2DMKey& a, const Material2DMKey& b) noexcept {
    const auto high_a = key_word(a, 4), high_b = key_word(b, 4);
    return high_a != high_b ? high_a < high_b : key_word(a, 0) < key_word(b, 0);
}
std::optional<std::size_t>
resolve(const std::map<std::uint32_t, std::size_t>& records,
        std::uint32_t target, std::size_t field) {
    if (target == 0)
        return {};
    const auto found = records.find(target);
    if (found == records.end())
        fail(Material2DMErrorCode::invalid_reference, field,
             "Reference does not target a declared record of the right type");
    return found->second;
}
} // namespace

std::expected<Material2DM, Material2DMError>
Material2DM::parse(Bytes bytes, Material2DMLimits limits) {
    try {
        Reader reader(bytes, limits);
        reader.range(0, 16);
        if (reader.u32(0) != 2)
            fail(Material2DMErrorCode::unsupported_version, 0,
                 "Only the observed PC v2 material format is supported");
        if (reader.u32(4) != 0)
            fail(Material2DMErrorCode::unsupported_layout, 4,
                 "Nonzero opaque file header is unsupported");
        if (reader.u32(8) != bytes.size() ||
            std::memcmp(bytes.data() + 12, ".2DM", 4) != 0)
            fail(Material2DMErrorCode::invalid_chunk, 8,
                 "Material file size or marker differs");
        const auto roots = reader.children(16, bytes.size(), 6);
        constexpr std::string_view tags[]{"LAYA", "PARA", "MATA",
                                          "MTPA", "UVAA", "REAL"};
        if (roots.size() != std::size(tags))
            fail(Material2DMErrorCode::unsupported_layout, 16,
                 "Unsupported material root count");
        for (std::size_t i = 0; i < roots.size(); ++i)
            if (!roots[i].is(tags[i]))
                fail(Material2DMErrorCode::unsupported_layout, roots[i].offset,
                     "Unexpected material root order");
        for (std::size_t i : {3U, 4U})
            if (roots[i].layout != 16 || roots[i].size != 16)
                fail(Material2DMErrorCode::unsupported_layout, roots[i].offset,
                     "Nonempty MTPA/UVAA is not yet supported");
        const auto layers = reader.children(roots[0], limits.max_layers);
        const auto parameters =
            reader.children(roots[1], limits.max_parameters);
        const auto maximum_material_children = limits.max_materials == SIZE_MAX
                                                   ? SIZE_MAX
                                                   : limits.max_materials + 1;
        const auto material_children =
            reader.children(roots[2], maximum_material_children);
        if (material_children.empty())
            fail(Material2DMErrorCode::invalid_hash, roots[2].offset,
                 "MATA must begin with HASH");
        const auto& hash = material_children.front();
        reader.leaf(hash, "HASH");
        const auto material_count = (hash.size - 16) / 16;
        if (material_count > limits.max_materials)
            fail(Material2DMErrorCode::resource_limit, hash.offset,
                 "Material HASH count exceeds budget");
        if (material_count != material_children.size() - 1)
            fail(Material2DMErrorCode::invalid_hash, hash.offset,
                 "Material HASH does not cover the declared MAT records");
        reader.leaf(roots[5], "REAL");
        std::set<std::uint32_t> relocations;
        bool padding{};
        std::size_t relocation_words{};
        for (std::size_t i = roots[5].offset + 16; i < roots[5].end(); i += 4) {
            if (relocation_words++ >= limits.max_relocations)
                fail(Material2DMErrorCode::resource_limit, i,
                     "REAL word count exceeds budget, including padding");
            const auto field = reader.u32(i);
            if (!field) {
                padding = true;
                continue;
            }
            if (padding || field < 16 || field % 4 ||
                field > bytes.size() - 4 || !relocations.insert(field).second)
                fail(Material2DMErrorCode::invalid_reference, i,
                     "Invalid, duplicate or nonterminal-padding REAL field");
        }
        std::set<std::uint32_t> known_fields;
        const auto reference = [&](std::uint32_t field) {
            known_fields.insert(field);
            const auto value = reader.u32(field);
            if (value && !relocations.contains(field))
                fail(Material2DMErrorCode::invalid_reference, field,
                     "Nonzero material reference is missing from REAL");
            return value;
        };
        Material2DM result;
        std::map<std::uint32_t, std::size_t> layer_indexes, parameter_indexes;
        reader.claim(layers.size(), sizeof(Material2DMLayer), roots[0].offset);
        for (const auto& chunk : layers) {
            reader.leaf(chunk, "LAY.", 0x120);
            Material2DMLayer layer{chunk.offset, {}};
            std::ranges::copy(reader.payload(chunk),
                              layer.opaque_payload.begin());
            layer_indexes.emplace(chunk.offset, result.layers_.size());
            result.layers_.push_back(std::move(layer));
        }
        reader.claim(parameters.size(), sizeof(Material2DMParameters),
                     roots[1].offset);
        for (const auto& chunk : parameters) {
            reader.leaf(chunk, "PAR.");
            switch (chunk.size) {
            case 0x60:
            case 0x80:
            case 0xa0:
            case 0x100:
            case 0x180:
            case 0x1a0:
                break;
            default:
                fail(Material2DMErrorCode::unsupported_layout, chunk.offset,
                     "PAR size is outside the observed supported subset");
            }
            const auto field = chunk.offset + 0x54;
            const auto layer = resolve(layer_indexes, reference(field), field);
            const auto payload = reader.payload(chunk);
            reader.claim(payload.size(), 1, chunk.offset);
            parameter_indexes.emplace(chunk.offset, result.parameters_.size());
            result.parameters_.push_back(
                {chunk.offset, layer, {payload.begin(), payload.end()}});
        }
        std::map<std::uint32_t, const Chunk*> material_records;
        for (const auto& chunk : std::span(material_children).subspan(1)) {
            reader.leaf(chunk, "MAT.", 0xf0);
            material_records.emplace(chunk.offset, &chunk);
        }
        reader.claim(material_count, sizeof(Material2DMEntry), hash.offset);
        reader.claim(material_count, sizeof(std::size_t), hash.offset);
        for (std::uint32_t i = 0; i < material_count; ++i) {
            const auto entry = hash.offset + 16 + i * 16;
            if (reader.u32(entry + 12) != 0)
                fail(Material2DMErrorCode::unsupported_layout, entry + 12,
                     "Nonzero HASH tail is unsupported");
            const auto target = reference(entry + 8);
            const auto found = material_records.find(target);
            if (found == material_records.end())
                fail(Material2DMErrorCode::invalid_hash, entry + 8,
                     "HASH target is not a unique declared MAT start");
            const auto& chunk = *found->second;
            const auto field = chunk.offset + 0xe0;
            Material2DMEntry material{
                reader.key(entry),
                entry,
                target,
                resolve(parameter_indexes, reference(field), field),
                reader.u16(chunk.offset + 0x10),
                reader.u32(chunk.offset + 0x14),
                {}};
            std::ranges::copy(reader.payload(chunk),
                              material.opaque_payload.begin());
            result.materials_.push_back(std::move(material));
            material_records.erase(found);
        }
        if (!std::ranges::includes(known_fields, relocations))
            fail(Material2DMErrorCode::unsupported_layout, roots[5].offset,
                 "REAL includes an unsupported pointer field");
        result.key_order_.resize(material_count);
        std::iota(result.key_order_.begin(), result.key_order_.end(),
                  std::size_t{});
        std::ranges::sort(result.key_order_, [&](std::size_t a, std::size_t b) {
            return key_less(result.materials_[a].key, result.materials_[b].key);
        });
        for (std::size_t i = 1; i < result.key_order_.size(); ++i)
            if (result.materials_[result.key_order_[i - 1]].key ==
                result.materials_[result.key_order_[i]].key)
                fail(Material2DMErrorCode::invalid_hash, hash.offset,
                     "Duplicate eight-byte material key is ambiguous");
        return result;
    } catch (const Failure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            Material2DMError{Material2DMErrorCode::resource_limit, 0,
                             "Material allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(
            Material2DMError{Material2DMErrorCode::resource_limit, 0,
                             "Material allocation is too large"});
    }
}

std::span<const Material2DMEntry> Material2DM::materials() const noexcept {
    return materials_;
}
std::span<const Material2DMParameters>
Material2DM::parameters() const noexcept {
    return parameters_;
}
std::span<const Material2DMLayer> Material2DM::layers() const noexcept {
    return layers_;
}
std::optional<std::size_t>
Material2DM::find(const Material2DMKey& key) const noexcept {
    const auto found =
        std::lower_bound(key_order_.begin(), key_order_.end(), key,
                         [&](std::size_t index, const Material2DMKey& wanted) {
                             return key_less(materials_[index].key, wanted);
                         });
    if (found == key_order_.end() || materials_[*found].key != key)
        return {};
    return *found;
}
std::expected<Material2DMBinding, Material2DMError> Material2DM::bind_primitive(
    const Scene3DD& scene, std::optional<std::size_t> material_bank_index,
    std::uint32_t primitive_binding_word,
    std::optional<std::size_t> fallback_material) const {
    const auto error = [](Material2DMErrorCode code, std::size_t offset,
                          const char* detail) {
        return std::unexpected(Material2DMError{code, offset, detail});
    };
    if (fallback_material && *fallback_material >= materials_.size())
        return error(Material2DMErrorCode::invalid_fallback, *fallback_material,
                     "Fallback must be a known material in this library");
    const auto fallback = [&](MaterialFallbackReason reason,
                              std::optional<std::uint16_t> slot)
        -> std::expected<Material2DMBinding, Material2DMError> {
        if (!fallback_material)
            return error(Material2DMErrorCode::fallback_required,
                         slot.value_or(0),
                         "Native fallback identity is unresolved; supply an "
                         "explicit material");
        return Material2DMBinding{*fallback_material, slot, reason};
    };
    if (!material_bank_index)
        return fallback(MaterialFallbackReason::absent_bank, {});
    if (*material_bank_index >= scene.material_banks().size())
        return error(Material2DMErrorCode::invalid_material_bank,
                     *material_bank_index,
                     "Instance material bank is outside the parsed scene");
    const auto slot = static_cast<std::uint16_t>(primitive_binding_word >> 16);
    if (slot == UINT16_MAX)
        return fallback(MaterialFallbackReason::sentinel, slot);
    const auto& bank = scene.material_banks()[*material_bank_index];
    if (slot >= bank.keys.size())
        return error(Material2DMErrorCode::invalid_material_slot,
                     bank.source_offset,
                     "PRIM material slot exceeds the selected instance bank");
    const auto material = find(bank.keys[slot].key);
    if (!material)
        return fallback(MaterialFallbackReason::unresolved_key, slot);
    return Material2DMBinding{*material, slot, MaterialFallbackReason::none};
}
} // namespace tdur::assets

#include "tdur/assets/vmf_text_data.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace tdur::assets {
namespace {
auto reject(VmfErrorCode code, std::size_t at,
            std::string_view detail) noexcept {
    return std::unexpected(VmfError{code, at, detail});
}
std::uint32_t integer(std::span<const std::byte> bytes, std::size_t at,
                      std::size_t size, bool big) noexcept {
    std::uint32_t result = 0;
    for (std::size_t i = 0; i != size; ++i) {
        const auto shift = static_cast<unsigned>(big ? size - i - 1 : i) * 8U;
        result |= std::to_integer<std::uint32_t>(bytes[at + i]) << shift;
    }
    return result;
}
} // namespace

VmfTextData::VmfTextData(VmfProfile profile) noexcept : profile_(profile) {}

std::expected<VmfTextData, VmfError>
VmfTextData::decode(const VmfMovie &movie, VmfTextLimits limits) noexcept {
    const auto profile = movie.header().profile;
    if (profile != VmfProfile::pc_2_8 && profile != VmfProfile::xbox_2_5)
        return reject(VmfErrorCode::unsupported_profile, 0,
                      "VMF text profile is not established");
    const bool big = profile == VmfProfile::xbox_2_5;
    const std::size_t name_at = big ? 0x30 : 0x32;
    const std::size_t cache_at = big ? 0x6c : 0x70;
    const std::size_t text_at = big ? 0x74 : 0x78;
    VmfTextData result(profile);
    auto remaining = limits.max_output_bytes;
    const auto charge = [&remaining](std::size_t amount) {
        if (amount > remaining)
            return false;
        remaining -= amount;
        return true;
    };
    if (!charge(sizeof(VmfTextData)))
        return reject(VmfErrorCode::resource_limit, 0,
                      "VMF text storage limit");
    try {
        std::size_t count = 0;
        for (const auto &record : movie.records())
            if (record.type == 37)
                ++count;
        if (count > limits.max_fields ||
            count > remaining / sizeof(VmfTextField) ||
            !charge(count * sizeof(VmfTextField)))
            return reject(VmfErrorCode::resource_limit, 0,
                          "VMF text field/storage limit");
        result.fields_.reserve(count);
        const auto records = movie.records();
        for (std::size_t i = 0; i != records.size(); ++i) {
            const auto &record = records[i];
            if (record.type != 37)
                continue;
            const auto base = static_cast<std::size_t>(record.source_offset);
            const auto region =
                movie.bytes({record.source_offset, record.declared_size});
            if (!region)
                return std::unexpected(region.error());
            const auto bytes = *region;
            if (bytes.size() < text_at + 4)
                return reject(VmfErrorCode::truncated, base + bytes.size(),
                              "VMF text fixed header/terminator truncated");
            if (!record.definition_id ||
                integer(bytes, 8, 4, big) != *record.definition_id)
                return reject(VmfErrorCode::invalid_definition, base + 8,
                              "VMF text definition identity");
            const std::string_view name(
                reinterpret_cast<const char *>(bytes.data() + name_at), 60);
            const auto name_end = name.find('\0');
            if (name_end == std::string_view::npos)
                return reject(VmfErrorCode::invalid_range, base + name_at,
                              "VMF text variable name lacks terminator");
            if (integer(bytes, cache_at, 4, big) != 0 ||
                integer(bytes, cache_at + 4, 4, big) != 0)
                return reject(VmfErrorCode::unsupported_layout, base + cache_at,
                              "VMF on-disk text caches must be zero");
            const std::string_view text(
                reinterpret_cast<const char *>(bytes.data() + text_at),
                bytes.size() - text_at);
            const auto inspected =
                std::min(text.size(), limits.max_initial_text_bytes);
            const auto text_end = text.substr(0, inspected).find('\0');
            if (text_end == std::string_view::npos) {
                if (text.size() > inspected)
                    return reject(VmfErrorCode::resource_limit, base + text_at,
                                  "VMF initial text exceeds byte limit");
                return reject(VmfErrorCode::truncated, base + text_at,
                              "VMF initial text lacks terminator");
            }
            const auto padding = text.substr(text_end + 1);
            if (padding.size() > 3 ||
                padding.find_first_not_of('\0') != std::string_view::npos)
                return reject(VmfErrorCode::unsupported_layout,
                              base + text_at + text_end + 1,
                              "VMF initial text alignment/extra data");
            if (!charge(text_end + 1))
                return reject(VmfErrorCode::resource_limit, base + text_at,
                              "VMF text storage limit");

            VmfTextField field;
            field.character_id = *record.definition_id;
            field.record_index = i;
            field.font_id =
                static_cast<std::uint16_t>(integer(bytes, 0x1c, 2, big));
            field.raw_flags_2e = std::to_integer<std::uint8_t>(bytes[0x2e]);
            field.raw_2f = std::to_integer<std::uint8_t>(bytes[0x2f]);
            if (!big)
                field.raw_30 =
                    static_cast<std::uint16_t>(integer(bytes, 0x30, 2, false));
            std::copy_n(name.data(), name_end, field.variable_name.data());
            field.initial_text.assign(text.substr(0, text_end));
            if (field.font_id >= movie.definitions().size())
                return reject(VmfErrorCode::invalid_reference, base + 0x1c,
                              "VMF text font ID outside definition table");
            field.font_record_index = movie.definition(field.font_id);
            if (field.font_record_index) {
                const auto &font = records[*field.font_record_index];
                if (font.type != 10)
                    return reject(VmfErrorCode::unsupported_layout,
                                  font.source_offset,
                                  "VMF text font reference is not type10");
                const auto data =
                    movie.bytes({font.source_offset, font.declared_size});
                if (!data)
                    return std::unexpected(data.error());
                if (data->size() < 16)
                    return reject(VmfErrorCode::truncated,
                                  font.source_offset + data->size(),
                                  "VMF font companion ID truncated");
                const auto companion = integer(*data, 12, 4, big);
                if (companion >= movie.definitions().size())
                    return reject(VmfErrorCode::invalid_reference,
                                  font.source_offset + 12,
                                  "VMF font companion ID outside table");
                field.companion_character_id = companion;
                field.companion_record_index = movie.definition(companion);
            }
            result.fields_.push_back(std::move(field));
        }
        std::sort(result.fields_.begin(), result.fields_.end(),
                  [](const auto &a, const auto &b) {
                      return a.character_id < b.character_id;
                  });
        result.logical_output_bytes_ = limits.max_output_bytes - remaining;
        return result;
    } catch (const std::bad_alloc &) {
        return reject(VmfErrorCode::resource_limit, 0,
                      "VMF text allocation failed");
    } catch (const std::length_error &) {
        return reject(VmfErrorCode::resource_limit, 0,
                      "VMF text container limit");
    }
}

std::span<const VmfTextField> VmfTextData::fields() const & noexcept {
    return fields_;
}
std::optional<std::size_t>
VmfTextData::field_index(std::uint32_t character_id) const noexcept {
    const auto at = std::lower_bound(
        fields_.begin(), fields_.end(), character_id,
        [](const auto &field, auto id) { return field.character_id < id; });
    if (at == fields_.end() || at->character_id != character_id)
        return std::nullopt;
    return static_cast<std::size_t>(at - fields_.begin());
}
VmfProfile VmfTextData::profile() const noexcept { return profile_; }
std::size_t VmfTextData::logical_output_bytes() const noexcept {
    return logical_output_bytes_;
}

} // namespace tdur::assets

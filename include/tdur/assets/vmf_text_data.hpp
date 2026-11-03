#pragma once

#include "tdur/assets/vmf_movie.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tdur::assets {

struct VmfTextLimits {
    std::size_t max_fields = 65'535;
    // Per field, including the first NUL. View/storage alignment is excluded.
    std::size_t max_initial_text_bytes = 64U * 1024U;
    // sizeof(VmfTextData) + sizeof(field) per field + initial_text.size()+1.
    // Allocator overhead, capacity and temporary parsing storage are excluded.
    std::size_t max_output_bytes = 64U * 1024U * 1024U;
};

struct VmfTextField {
    std::uint32_t character_id{};
    std::size_t record_index{};
    std::uint16_t font_id{};
    std::uint8_t raw_flags_2e{};
    // Raw file byte; native44B030 rewrites it from context+2C and flag2E&8.
    std::uint8_t raw_2f{};
    // PC-only word. No maximum-length interpretation is asserted here.
    std::optional<std::uint16_t> raw_30;
    // Owns the prefix through its first NUL, with a zero-filled unused tail.
    std::array<char, 60> variable_name{};
    std::string initial_text;
    // Indices into this source movie, never native addresses or live caches.
    std::optional<std::size_t> font_record_index;
    std::optional<std::uint32_t> companion_character_id;
    std::optional<std::size_t> companion_record_index;
};

// Decodes immutable on-disk type37 records. The native PC loader4418B0 changes
// their first DWORD into a vtable in place; it is not a payload base offset.
// PC2.8 and Xbox2.5 have distinct name/cache/text offsets. No file bytes are
// changed; nonzero on-disk caches are refused, never treated as pointers.
// Strings are owned. No variable seeding, focus, glyph layout or drawing
// occurs.
class VmfTextData {
  public:
    [[nodiscard]] static std::expected<VmfTextData, VmfError>
    decode(const VmfMovie &movie, VmfTextLimits limits = {}) noexcept;
    [[nodiscard]] std::span<const VmfTextField> fields() const & noexcept;
    std::span<const VmfTextField> fields() const && = delete;
    // fields() is sorted by character ID; lookup returns an owning index value.
    [[nodiscard]] std::optional<std::size_t>
    field_index(std::uint32_t character_id) const noexcept;
    [[nodiscard]] VmfProfile profile() const noexcept;
    [[nodiscard]] std::size_t logical_output_bytes() const noexcept;

  private:
    explicit VmfTextData(VmfProfile profile) noexcept;
    VmfProfile profile_{};
    std::vector<VmfTextField> fields_;
    std::size_t logical_output_bytes_{};
};

} // namespace tdur::assets

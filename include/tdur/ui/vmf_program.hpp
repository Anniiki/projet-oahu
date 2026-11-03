#pragma once

#include "tdur/assets/vmf_display_data.hpp"
#include "tdur/assets/vmf_text_data.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

namespace tdur::ui {

// Owns a validated movie and the display data decoded FROM that same movie.
// No public constructor accepts an arbitrary movie/display pair. Resource
// IDs, sprite indices and script indices all belong to this one program.
class VmfProgram {
  public:
    VmfProgram(const VmfProgram&) = delete;
    VmfProgram& operator=(const VmfProgram&) = delete;
    VmfProgram(VmfProgram&&) = delete;
    VmfProgram& operator=(VmfProgram&&) = delete;
    // Logical bytes: sizeof(VmfProgram), movie/display logical bytes and one
    // size_t per definition. Allocator overhead/capacity are excluded. Caller
    // keeps movie stable for build(); all copies/decoding allocations guarded.
    // Optional text decoding adds its logical bytes to this same total; it
    // does not enable display37, variable seeding, focus or rendering services.
    [[nodiscard]] static std::expected<std::shared_ptr<const VmfProgram>,
                                       assets::VmfError>
    build(const assets::VmfMovie &movie,
          assets::VmfDisplayLimits display_limits = {},
          std::size_t max_output_bytes = 512U * 1024U * 1024U,
          std::optional<assets::VmfTextLimits> text_limits = {}) noexcept;

    [[nodiscard]] const assets::VmfMovie& movie() const& noexcept;
    const assets::VmfMovie& movie() const&& = delete;
    [[nodiscard]] const assets::VmfDisplayData& display() const& noexcept;
    const assets::VmfDisplayData& display() const&& = delete;
    // Borrowed until program destruction; null if decoding was not requested.
    [[nodiscard]] const assets::VmfTextData *text_data() const & noexcept;
    const assets::VmfTextData *text_data() const && = delete;
    [[nodiscard]] std::optional<std::size_t>
    sprite_for_character(std::uint32_t character_id) const noexcept;
    [[nodiscard]] std::size_t logical_output_bytes() const noexcept;

  private:
    VmfProgram(assets::VmfMovie movie, assets::VmfDisplayData display,
               std::optional<assets::VmfTextData> text,
               std::vector<std::size_t> sprites, std::size_t bytes);
    assets::VmfMovie movie_;
    assets::VmfDisplayData display_;
    std::optional<assets::VmfTextData> text_;
    std::vector<std::size_t> sprites_;
    std::size_t logical_output_bytes_{};
};

using VmfProgramPtr = std::shared_ptr<const VmfProgram>;

} // namespace tdur::ui

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

struct VmfDisplayLimits {
    std::size_t max_input_bytes = 64U * 1024U * 1024U;
    std::size_t max_placements = 500'000;
    std::size_t max_removals = 100'000;
    std::size_t max_frames = 250'000;
    std::size_t max_events = 600'000;
    std::size_t max_name_bytes = 4096;
    std::size_t max_total_name_bytes = 16U * 1024U * 1024U;
    // Logical result = sizeof(T) per element in all four collections below,
    // plus name lengths. Excludes allocator/container/capacity overhead and
    // string NULs. The source movie is not copied into this result.
    std::size_t max_output_bytes = 128U * 1024U * 1024U;
};

struct VmfDisplayLocation {
    std::size_t record_index{};
    std::size_t sprite_index{};
    std::size_t global_frame_index{};
    std::uint16_t frame_index{};
    std::uint32_t source_offset{};
};

struct VmfPlacement {
    VmfDisplayLocation location;
    std::uint16_t depth{};
    std::uint16_t flags{};
    // These optionals describe active fields, selected by flags 2/4/8/10/40.
    std::optional<std::uint16_t> character_id;
    // Native PC order: pair100, pair80, pair200. Missing PC pairs default to
    // bits(+1,+1,+0,+0,+0,+0). No renderer row/column convention is implied.
    std::optional<std::array<std::uint32_t, 6>> matrix_bits;
    // Raw words: signedness/8.8 arithmetic are not imposed here.
    std::optional<std::array<std::uint16_t, 8>> color_words;
    std::optional<std::uint16_t> ratio;
    std::optional<std::uint16_t> clip_depth;
    // Full byte name, not truncated31. The instance constructor owns that
    // separate truncation. Flag20 describes whether a name update is active.
    std::string name;
    // Xbox physically stores a fixed52-byte prefix at tag+8 even when fields
    // are inactive. Preserve those bytes without interpreting reserved data.
    std::optional<std::array<std::byte, 52>> xbox_fixed_payload;
};

struct VmfRemoval {
    VmfDisplayLocation location;
    std::uint16_t depth{};
    std::uint16_t raw_0a{};
};

enum class VmfDisplayEventKind { place, remove };
struct VmfDisplayEvent {
    VmfDisplayEventKind kind{};
    // Index in placements() or removals(), selected by kind.
    std::size_t item_index{};
};
struct VmfDisplayFrame {
    std::size_t first_event{};
    std::size_t event_count{};
};

// Typed immutable data for tags26/28, not a live display list or renderer.
// See docs/evidence/vmf_display_data.md for native/differential evidence.
// Records come only from the validated movie walk. Frames use its global
// frame indices; each event slice preserves the original record order.
//
// decode() borrows movie only for the call. Results own all decoded values,
// names and Xbox fixed bytes; source indices/ranges are provenance metadata.
// Copy assignment has the strong guarantee. Views must not outlive this
// result or its move/assignment. Moved-from objects allow destruction or
// assignment only. The caller keeps movie stable for the whole call.
class VmfDisplayData {
  public:
    VmfDisplayData(const VmfDisplayData&) = default;
    VmfDisplayData& operator=(const VmfDisplayData&);
    VmfDisplayData(VmfDisplayData&&) noexcept = default;
    VmfDisplayData& operator=(VmfDisplayData&&) noexcept = default;
    ~VmfDisplayData() = default;

    [[nodiscard]] static std::expected<VmfDisplayData, VmfError>
    decode(const VmfMovie& movie, VmfDisplayLimits limits = {});
    [[nodiscard]] VmfProfile profile() const noexcept;
    [[nodiscard]] std::span<const VmfPlacement> placements() const& noexcept;
    std::span<const VmfPlacement> placements() const&& = delete;
    [[nodiscard]] std::span<const VmfRemoval> removals() const& noexcept;
    std::span<const VmfRemoval> removals() const&& = delete;
    [[nodiscard]] std::span<const VmfDisplayFrame> frames() const& noexcept;
    std::span<const VmfDisplayFrame> frames() const&& = delete;
    [[nodiscard]] std::span<const VmfDisplayEvent> events() const& noexcept;
    std::span<const VmfDisplayEvent> events() const&& = delete;
    [[nodiscard]] std::expected<std::span<const VmfDisplayEvent>, VmfError>
    events_for_frame(std::size_t global_frame_index) const& noexcept;
    std::expected<std::span<const VmfDisplayEvent>, VmfError>
    events_for_frame(std::size_t) const&& = delete;
    [[nodiscard]] std::size_t logical_output_bytes() const noexcept;

  private:
    VmfDisplayData() = default;
    VmfProfile profile_{};
    std::vector<VmfPlacement> placements_;
    std::vector<VmfRemoval> removals_;
    std::vector<VmfDisplayFrame> frames_;
    std::vector<VmfDisplayEvent> events_;
    std::size_t logical_output_bytes_{};
};

} // namespace tdur::assets

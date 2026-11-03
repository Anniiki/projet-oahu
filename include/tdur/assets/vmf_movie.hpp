#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tdur::assets {

// These are separate, explicit observed resource profiles. Neither profile
// establishes execution of ActionScript or compatibility with another player.
enum class VmfProfile { pc_2_8, xbox_2_5 };

struct VmfLimits {
    std::size_t max_input_bytes = 64U * 1024U * 1024U;
    // Logical output = owned input bytes + sizeof(VmfHeader) + sizeof(T)
    // for every element of each public decoded collection below. Vector
    // capacity, allocator/container overhead and temporary validation/work
    // indexes are excluded; their counts are bounded by the other quotas.
    std::size_t max_output_bytes = 256U * 1024U * 1024U;
    std::size_t max_records = 250'000;
    std::size_t max_sprites = 16'384;
    std::size_t max_frames = 250'000;
    std::size_t max_definitions = 65'535;
    std::size_t max_scripts = 250'000;
    std::size_t max_actions = 2'000'000;
    std::size_t max_branches = 500'000;
    // Root counts as depth 1. This is a structural bound, not a native limit.
    std::size_t max_depth = 64;
    std::size_t max_button_visuals = 65'535;
    std::size_t max_labels = 250'000;
    std::size_t max_images = 65'535;
    std::size_t max_atlas_entries = 65'535;
};

enum class VmfErrorCode {
    truncated,
    unsupported_profile,
    unsupported_version,
    unsupported_layout,
    unsupported_record,
    unsupported_action,
    invalid_header,
    invalid_crc,
    invalid_hierarchy,
    invalid_definition,
    invalid_frame,
    invalid_action,
    invalid_branch,
    invalid_reference,
    invalid_numeric,
    invalid_range,
    resource_limit,
};

struct VmfError {
    VmfErrorCode code{};
    std::size_t offset{};
    // Static diagnostic literal; does not borrow from caller input or require
    // allocation when reporting a quota/allocation failure.
    std::string_view detail;
};

struct VmfByteRange {
    std::uint32_t offset{};
    std::uint32_t size{};
};

struct VmfHeader {
    VmfProfile profile{};
    std::byte raw_04{};
    std::uint8_t major_version{};
    std::uint16_t minor_version{};
    // Byte +08 agrees with profile endianness in the observed files. The
    // remaining meaning of +08/+09 and the u16 +0a stays uninterpreted.
    std::array<std::byte, 2> raw_08{};
    std::uint16_t raw_0a{};
    std::array<std::byte, 32> author{};
    std::array<float, 4> rectangle{};
    std::uint16_t raw_3c{};
    std::uint16_t definition_count{};
    std::uint16_t raw_40{};
    std::uint16_t raw_42{};
    std::uint32_t crc32{};
};

struct VmfRecord {
    std::uint32_t source_offset{};
    std::uint32_t type{};
    // Actual u32 from the record: root End=0, nested End=8. For type39 this
    // is metadata size, NOT the complete sprite extent.
    std::uint32_t declared_size{};
    // Complete sprite extent for type39; 8 for either End; otherwise size.
    std::uint32_t full_size{};
    VmfByteRange payload{};
    std::optional<std::size_t> parent_sprite_index;
    std::optional<std::uint32_t> definition_id;
};

struct VmfSprite {
    std::size_t record_index{};
    std::uint32_t character_id{};
    std::uint16_t raw_0e{};
    std::size_t first_frame{};
    std::uint16_t frame_count{};
};

struct VmfFrame {
    std::size_t sprite_index{};
    std::uint16_t index{};
    std::uint32_t begin_offset{};
    // Exclusive end, equal to the direct ShowFrame record offset + 8.
    std::uint32_t end_offset{};
    std::size_t show_frame_record_index{};
};

struct VmfDefinition {
    std::uint32_t character_id{};
    std::size_t record_index{};
};

enum class VmfScriptKind { timeline, button_condition };

struct VmfButtonCondition {
    std::uint32_t source_offset{};
    std::uint16_t declared_size{};
    std::uint16_t raw_flags{};
    std::size_t ordinal{};
};

struct VmfScript {
    VmfScriptKind kind{};
    // Type12 for timeline; type7 for button conditions.
    std::size_t owner_record_index{};
    std::size_t parent_sprite_index{};
    // Present only for a timeline. No active/focused state is inferred.
    std::optional<std::uint16_t> frame_index;
    std::optional<VmfButtonCondition> button_condition;
    VmfByteRange encoded_region{};
    std::size_t first_action{};
    std::size_t action_count{};
    // Includes End, excludes physical alignment padding.
    std::uint32_t compact_size{};
};

struct VmfBranch {
    std::int16_t delta{};
    // Index into the whole movie's actions(), never a native address.
    std::size_t target_action_index{};
};

struct VmfAction {
    std::uint32_t source_offset{};
    std::uint16_t opcode{};
    std::uint32_t encoded_size{};
    VmfByteRange payload{};
    // Offset in this script's compact SWF byte space, not the VMF buffer.
    std::uint32_t compact_offset{};
    std::optional<VmfBranch> branch;
    // Strings exclude their NUL and remain bytes, not assumed UTF-8. Push,
    // SetTarget and GotoLabel have one; GetURL has two. They are operands,
    // not proof of a reachable/executed command.
    std::array<std::optional<VmfByteRange>, 2> strings{};
};

struct VmfButtonVisual {
    std::size_t record_index{};
    std::uint32_t source_offset{};
    std::optional<std::uint32_t> pc_extra_word;
    std::uint32_t states{};
    std::uint16_t character_id{};
    std::uint16_t raw_06{};
    std::array<float, 6> raw_matrix{};
    std::array<std::uint16_t, 8> raw_words{};
};

struct VmfLabel {
    std::size_t record_index{};
    std::size_t parent_sprite_index{};
    std::uint16_t frame_index{};
    VmfByteRange text{};
};

struct VmfAtlasEntry {
    std::uint32_t source_offset{};
    std::uint32_t character_id{};
    std::uint32_t page_number{};
    std::array<std::uint32_t, 2> raw_words{};
    std::array<float, 4> raw_floats{};
};

enum class VmfImageSource { direct_fla, atlas_page };

struct VmfImageReference {
    std::uint32_t character_id{};
    std::size_t record_index{};
    VmfImageSource source{};
    // Inventory naming: fla_%04u.2db for direct IDs, page_%03u.2db for
    // atlas numbers. This is NOT a BNK physical index or resolved resource.
    std::uint32_t source_number{};
    std::optional<std::size_t> atlas_entry_index;
};

namespace detail {
class VmfMovieParser;
}

// Strict observed subset: tags 0,1,2,7,9,10,11,12,14,15,20,26,28,37,39,43,46
// and the 36 corpus action opcodes documented in vmf_movie_reader.md. Other
// payloads stay opaque. Type7 with more than one visual is unsupported.
// No interpretation, rendering, native pointer relocation or command calls.
//
// parse() copies the input. Collections store values/ranges/indices, never
// views into another object. Copying makes an independent owning copy. Views
// returned by an object must not outlive it or any move/assignment involving
// it. All operations on a moved-from object except destruction/assignment are
// outside this contract. The caller keeps input stable for the parse call.
class VmfMovie {
  public:
    VmfMovie(const VmfMovie&) = default;
    // An allocation failure preserves the entire validated destination.
    VmfMovie& operator=(const VmfMovie&);
    VmfMovie(VmfMovie&&) noexcept = default;
    VmfMovie& operator=(VmfMovie&&) noexcept = default;
    ~VmfMovie() = default;

    [[nodiscard]] static std::expected<VmfMovie, VmfError>
    parse(std::span<const std::byte> bytes, VmfProfile profile,
          VmfLimits limits = {});

    [[nodiscard]] const VmfHeader& header() const& noexcept;
    const VmfHeader& header() const&& = delete;
    [[nodiscard]] std::span<const std::byte> source_bytes() const& noexcept;
    std::span<const std::byte> source_bytes() const&& = delete;
    [[nodiscard]] std::expected<std::span<const std::byte>, VmfError>
    bytes(VmfByteRange range) const& noexcept;
    std::expected<std::span<const std::byte>, VmfError>
    bytes(VmfByteRange range) const&& = delete;

    [[nodiscard]] std::span<const VmfRecord> records() const& noexcept;
    std::span<const VmfRecord> records() const&& = delete;
    [[nodiscard]] std::span<const VmfSprite> sprites() const& noexcept;
    std::span<const VmfSprite> sprites() const&& = delete;
    [[nodiscard]] std::span<const VmfFrame> frames() const& noexcept;
    std::span<const VmfFrame> frames() const&& = delete;
    // Dense ascending character IDs, including root ID0, not physical order.
    [[nodiscard]] std::span<const VmfDefinition> definitions() const& noexcept;
    std::span<const VmfDefinition> definitions() const&& = delete;
    [[nodiscard]] std::span<const VmfScript> scripts() const& noexcept;
    std::span<const VmfScript> scripts() const&& = delete;
    [[nodiscard]] std::span<const VmfAction> actions() const& noexcept;
    std::span<const VmfAction> actions() const&& = delete;
    [[nodiscard]] std::span<const VmfButtonVisual>
    button_visuals() const& noexcept;
    std::span<const VmfButtonVisual> button_visuals() const&& = delete;
    [[nodiscard]] std::span<const VmfLabel> labels() const& noexcept;
    std::span<const VmfLabel> labels() const&& = delete;
    [[nodiscard]] std::span<const VmfImageReference> images() const& noexcept;
    std::span<const VmfImageReference> images() const&& = delete;
    [[nodiscard]] std::span<const VmfAtlasEntry>
    atlas_entries() const& noexcept;
    std::span<const VmfAtlasEntry> atlas_entries() const&& = delete;

    // Returns the target RECORD index, not the index in definitions().
    // With the dense ID table, equivalent to definitions()[id].record_index.
    [[nodiscard]] std::optional<std::size_t>
    definition(std::uint32_t character_id) const noexcept;
    [[nodiscard]] std::size_t branch_count() const noexcept;
    [[nodiscard]] std::size_t logical_output_bytes() const noexcept;

  private:
    VmfMovie() = default;
    friend class detail::VmfMovieParser;

    VmfHeader header_;
    std::vector<std::byte> source_;
    std::vector<VmfRecord> records_;
    std::vector<VmfSprite> sprites_;
    std::vector<VmfFrame> frames_;
    std::vector<VmfDefinition> definitions_;
    std::vector<VmfScript> scripts_;
    std::vector<VmfAction> actions_;
    std::vector<VmfButtonVisual> button_visuals_;
    std::vector<VmfLabel> labels_;
    std::vector<VmfImageReference> images_;
    std::vector<VmfAtlasEntry> atlas_entries_;
    std::size_t branch_count_{};
    std::size_t logical_output_bytes_{};
};

} // namespace tdur::assets

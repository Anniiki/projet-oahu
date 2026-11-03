#pragma once

#include "tdur/assets/vmf_movie.hpp"

#include <string>

namespace tdur::assets {

// Borrowed only for resolve(). Supply exact bank leaf names, with extension.
// member_index is a caller-defined index/token; no physical order is inferred.
struct VmfImageAsset {
    std::string_view exact_name;
    std::size_t member_index{};
};

struct VmfImageResource {
    std::string exact_name;
    std::size_t member_index{};
};

struct VmfBoundImageAtlas {
    VmfAtlasEntry entry;
    // Original byte order and all 32 source bytes, including opaque fields.
    // No UV orientation, sampling or Xbox texture layout is interpreted.
    std::array<std::byte, 32> source_bytes{};
};

struct VmfImageBinding {
    VmfImageReference image;
    VmfRecord definition_record;
    // The only rebased link here: index into resources() of this result.
    std::size_t resource_index{};
    std::optional<VmfBoundImageAtlas> atlas;
    // image.record_index, image.atlas_entry_index, definition_record.payload
    // and parent indices are SOURCE metadata, not output indices or views.
};

struct VmfImageBindingLimits {
    std::size_t max_input_bytes = 64U * 1024U * 1024U;
    std::size_t max_catalogue_entries = 100'000;
    std::size_t max_catalogue_name_bytes = 16U * 1024U * 1024U;
    std::size_t max_name_bytes = 4096;
    std::size_t max_images = 65'535;
    std::size_t max_resources = 65'535;
    std::size_t max_owned_name_bytes = 16U * 1024U * 1024U;
    // Logical result: bindings + resources + owned name bytes + ID lookup
    // indices. Excludes object/container overhead, spare capacity and NULs.
    std::size_t max_output_bytes = 64U * 1024U * 1024U;
    // Counts image-source key and catalogue-name equality operations in the
    // two resolution passes; excludes validation and the bounded ID sort.
    // Half is reserved for each pass, before any result allocation.
    std::size_t max_resolution_comparisons = 50'000'000;
};

enum class VmfImageBindingErrorCode {
    resource_limit,
    invalid_reference,
    missing_asset,
    ambiguous_asset,
    allocation_failure,
};

struct VmfImageBindingError {
    VmfImageBindingErrorCode code{};
    std::size_t source_offset{};
    std::optional<std::uint32_t> character_id;
    // Offending catalogue ordinal, when available; not member_index.
    std::optional<std::size_t> catalogue_index;
    // Static literal, never caller data or an allocation-dependent message.
    std::string_view detail;
};

// Pure bank-explicit association, no texture decoding, I/O or GPU ownership.
// PC and Xbox profiles use their already validated VmfMovie interpretation.
// Exact direct names: fla_%04u.2db using character_id. Exact atlas names:
// page_%03u.2db using source_number. Width is a MINIMUM, never truncation.
// Duplicate catalogue names used by an image are ambiguous, even if their
// member_index values are equal. Different names may alias the same token.
// Resources and bindings follow first occurrence in movie.images().
//
// Input movie and catalogue must remain stable for the call only. Returned
// collections own all values/names. Views expire on destruction, move or
// assignment; moved-from objects support destruction/assignment only.
class VmfImageBindings {
  public:
    VmfImageBindings(const VmfImageBindings&) = default;
    VmfImageBindings& operator=(const VmfImageBindings&);
    VmfImageBindings(VmfImageBindings&&) noexcept = default;
    VmfImageBindings& operator=(VmfImageBindings&&) noexcept = default;
    ~VmfImageBindings() = default;

    [[nodiscard]] static std::expected<VmfImageBindings, VmfImageBindingError>
    resolve(const VmfMovie& movie, std::span<const VmfImageAsset> catalogue,
            VmfImageBindingLimits limits = {});

    [[nodiscard]] std::span<const VmfImageResource> resources() const& noexcept;
    std::span<const VmfImageResource> resources() const&& = delete;
    [[nodiscard]] std::span<const VmfImageBinding> bindings() const& noexcept;
    std::span<const VmfImageBinding> bindings() const&& = delete;
    [[nodiscard]] std::optional<std::size_t>
    binding_index(std::uint32_t character_id) const& noexcept;
    std::optional<std::size_t>
    binding_index(std::uint32_t character_id) const&& = delete;
    [[nodiscard]] VmfProfile profile() const& noexcept;
    VmfProfile profile() const&& = delete;
    [[nodiscard]] std::size_t logical_output_bytes() const& noexcept;
    std::size_t logical_output_bytes() const&& = delete;

  private:
    VmfImageBindings() = default;
    VmfProfile profile_{};
    std::vector<VmfImageResource> resources_;
    std::vector<VmfImageBinding> bindings_;
    std::vector<std::size_t> id_lookup_;
    std::size_t logical_output_bytes_{};
};

} // namespace tdur::assets

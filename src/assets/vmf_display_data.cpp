#include "tdur/assets/vmf_display_data.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace tdur::assets {
namespace {
VmfError error(VmfErrorCode code, std::size_t at,
               std::string_view detail) noexcept {
    return {code, at, detail};
}
VmfError resource_error(std::size_t at = 0) noexcept {
    return error(VmfErrorCode::resource_limit, at,
                 "VMF display resource limit");
}

struct Reader {
    std::span<const std::byte> bytes;
    VmfProfile profile;
    std::size_t base;
    std::size_t position = 12;
    std::optional<VmfError> failure;

    void reject(VmfErrorCode code, std::size_t at,
                std::string_view detail) noexcept {
        if (!failure)
            failure = error(code, base + std::min(at, bytes.size()), detail);
    }
    bool need(std::size_t at, std::size_t count) noexcept {
        if (failure)
            return false;
        if (at > bytes.size() || count > bytes.size() - at) {
            reject(VmfErrorCode::truncated, at, "VMF display field truncated");
            return false;
        }
        return true;
    }
    std::uint32_t integer(std::size_t at, std::size_t width) noexcept {
        if (!need(at, width))
            return 0;
        std::uint32_t result = 0;
        for (std::size_t i = 0; i != width; ++i) {
            const auto shift =
                profile == VmfProfile::pc_2_8 ? i : width - i - 1;
            result |= std::to_integer<std::uint32_t>(bytes[at + i])
                      << (8U * shift);
        }
        return result;
    }
    std::uint16_t word(std::size_t at) noexcept {
        return static_cast<std::uint16_t>(integer(at, 2));
    }
    std::uint16_t take_word() noexcept {
        const auto value = word(position);
        if (!failure)
            position += 2;
        return value;
    }
    std::uint32_t take_float_bits() noexcept {
        const auto at = position;
        const auto value = integer(at, 4);
        if (!failure)
            position += 4;
        if ((value & 0x7f800000U) == 0x7f800000U)
            reject(VmfErrorCode::invalid_numeric, at,
                   "VMF display matrix is not finite");
        return value;
    }
    void align() noexcept {
        while (!failure && (position & 3U) != 0) {
            if (!need(position, 1))
                return;
            if (bytes[position] != std::byte{}) {
                reject(VmfErrorCode::unsupported_layout, position,
                       "VMF display alignment is not zero");
                return;
            }
            ++position;
        }
    }
    std::string_view name(std::size_t maximum) noexcept {
        if (!need(position, 1))
            return {};
        const auto available = bytes.size() - position;
        const auto inspected =
            maximum == std::numeric_limits<std::size_t>::max()
                ? available
                : std::min(available, maximum + 1);
        const std::string_view region(
            reinterpret_cast<const char*>(bytes.data() + position), inspected);
        const auto end = region.find('\0');
        if (end == std::string_view::npos) {
            if (available > maximum)
                reject(VmfErrorCode::resource_limit, position,
                       "VMF display name exceeds limit");
            else
                reject(VmfErrorCode::truncated, position,
                       "VMF display name has no terminator");
            return {};
        }
        position += end + 1;
        return region.substr(0, end);
    }
    void finish() noexcept {
        align();
        if (!failure && position != bytes.size())
            reject(VmfErrorCode::unsupported_layout, position,
                   "VMF display record has extra data");
    }
};

struct ParsedPlacement {
    std::uint16_t depth{}, flags{};
    std::optional<std::uint16_t> character, ratio, clip;
    std::optional<std::array<std::uint32_t, 6>> matrix;
    std::optional<std::array<std::uint16_t, 8>> color;
    std::string_view name;
    std::optional<std::array<std::byte, 52>> xbox;
};

std::expected<ParsedPlacement, VmfError>
placement(std::span<const std::byte> bytes, VmfProfile profile,
          std::size_t base, std::size_t max_name) noexcept {
    Reader reader{bytes, profile, base};
    ParsedPlacement value;
    value.depth = reader.word(8);
    value.flags = reader.word(profile == VmfProfile::pc_2_8 ? 10 : 52);
    const auto mask = profile == VmfProfile::pc_2_8 ? 0x3ffU : 0xffU;
    if ((value.flags & ~mask) != 0 ||
        (profile == VmfProfile::pc_2_8 && (value.flags & 0x380U) != 0 &&
         (value.flags & 4U) == 0))
        reader.reject(VmfErrorCode::unsupported_layout,
                      profile == VmfProfile::pc_2_8 ? 10 : 52,
                      "VMF display flags outside observed layout");
    if (profile == VmfProfile::pc_2_8) {
        if (value.flags & 2U)
            value.character = reader.take_word();
        if (value.flags & 4U) {
            reader.align();
            value.matrix = std::array<std::uint32_t, 6>{
                0x3f800000U, 0x3f800000U, 0, 0, 0, 0};
            constexpr std::array<unsigned, 3> pair_flags{0x100, 0x80, 0x200};
            for (std::size_t pair = 0; pair != pair_flags.size(); ++pair)
                if (value.flags & pair_flags[pair]) {
                    (*value.matrix)[pair * 2] = reader.take_float_bits();
                    (*value.matrix)[pair * 2 + 1] = reader.take_float_bits();
                }
        }
        if (value.flags & 8U) {
            reader.align();
            value.color.emplace();
            for (auto& word : *value.color)
                word = reader.take_word();
        }
        if (value.flags & 0x10U)
            value.ratio = reader.take_word();
        if (value.flags & 0x40U)
            value.clip = reader.take_word();
        if (value.flags & 0x20U)
            value.name = reader.name(max_name);
    } else {
        if (reader.need(8, 52)) {
            value.xbox.emplace();
            std::copy_n(bytes.data() + 8, 52, value.xbox->data());
        }
        if (value.flags & 2U)
            value.character = reader.word(10);
        if (value.flags & 4U) {
            reader.position = 12;
            value.matrix.emplace();
            for (auto& component : *value.matrix)
                component = reader.take_float_bits();
        }
        if (value.flags & 8U) {
            reader.position = 36;
            value.color.emplace();
            for (auto& word : *value.color)
                word = reader.take_word();
        }
        if (value.flags & 0x10U)
            value.ratio = reader.word(54);
        if (value.flags & 0x40U)
            value.clip = reader.word(58);
        reader.position = 60;
        value.name = reader.name(max_name);
    }
    reader.finish();
    if (reader.failure)
        return std::unexpected(*reader.failure);
    return value;
}

std::expected<VmfDisplayLocation, VmfError>
location(const VmfMovie& movie, const VmfRecord& record,
         std::size_t record_index) noexcept {
    const auto owner = record.parent_sprite_index;
    if (!owner || *owner >= movie.sprites().size())
        return std::unexpected(error(VmfErrorCode::invalid_reference,
                                     record.source_offset,
                                     "VMF display record has no sprite owner"));
    const auto& sprite = movie.sprites()[*owner];
    const auto frames = movie.frames();
    if (sprite.first_frame > frames.size() ||
        sprite.frame_count > frames.size() - sprite.first_frame)
        return std::unexpected(error(VmfErrorCode::invalid_frame,
                                     record.source_offset,
                                     "VMF display sprite frame range invalid"));
    const auto end = sprite.first_frame + sprite.frame_count;
    auto low = sprite.first_frame;
    auto high = end;
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (frames[middle].end_offset <= record.source_offset)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == end || frames[low].sprite_index != *owner ||
        record.source_offset < frames[low].begin_offset ||
        record.full_size > frames[low].end_offset - record.source_offset)
        return std::unexpected(error(VmfErrorCode::invalid_frame,
                                     record.source_offset,
                                     "VMF display record outside its frame"));
    return VmfDisplayLocation{record_index, *owner, low, frames[low].index,
                              record.source_offset};
}

struct ParsedRecord {
    VmfDisplayLocation where;
    std::optional<ParsedPlacement> placed;
    std::uint16_t removed_depth{}, removed_raw{};
};
std::expected<ParsedRecord, VmfError> record(const VmfMovie& movie,
                                             const VmfRecord& source,
                                             std::size_t index,
                                             VmfDisplayLimits limits) noexcept {
    auto where = location(movie, source, index);
    if (!where)
        return std::unexpected(where.error());
    auto bytes = movie.bytes({source.source_offset, source.full_size});
    if (!bytes)
        return std::unexpected(bytes.error());
    ParsedRecord result;
    result.where = *where;
    std::uint16_t depth = 0;
    if (source.type == 26) {
        auto value = placement(*bytes, movie.header().profile,
                               source.source_offset, limits.max_name_bytes);
        if (!value)
            return std::unexpected(value.error());
        depth = value->depth;
        if (value->character && !movie.definition(*value->character))
            return std::unexpected(error(VmfErrorCode::invalid_reference,
                                         source.source_offset,
                                         "VMF display character is undefined"));
        result.placed = *value;
    } else {
        Reader reader{*bytes, movie.header().profile, source.source_offset};
        result.removed_depth = reader.word(8);
        result.removed_raw = reader.word(10);
        if (reader.failure)
            return std::unexpected(*reader.failure);
        if (bytes->size() != 12)
            return std::unexpected(error(VmfErrorCode::unsupported_layout,
                                         source.source_offset,
                                         "VMF removal record size is not12"));
        depth = result.removed_depth;
    }
    if (depth >= movie.sprites()[where->sprite_index].raw_0e)
        return std::unexpected(
            error(VmfErrorCode::invalid_reference, source.source_offset,
                  "VMF display depth exceeds sprite capacity"));
    return result;
}

struct Counts {
    std::size_t placements{}, removals{}, events{}, names{}, logical{};
};
std::expected<Counts, VmfError> preflight(const VmfMovie& movie,
                                          VmfDisplayLimits limits) noexcept {
    Counts counts;
    if (movie.source_bytes().size() > limits.max_input_bytes ||
        movie.frames().size() > limits.max_frames ||
        movie.frames().size() >
            limits.max_output_bytes / sizeof(VmfDisplayFrame))
        return std::unexpected(resource_error());
    counts.logical = movie.frames().size() * sizeof(VmfDisplayFrame);
    for (std::size_t i = 0; i != movie.records().size(); ++i) {
        const auto& source = movie.records()[i];
        if (source.type != 26 && source.type != 28)
            continue;
        auto& counter = source.type == 26 ? counts.placements : counts.removals;
        const auto maximum =
            source.type == 26 ? limits.max_placements : limits.max_removals;
        if (counter == maximum || counts.events == limits.max_events)
            return std::unexpected(resource_error(source.source_offset));
        const auto parsed = record(movie, source, i, limits);
        if (!parsed)
            return std::unexpected(parsed.error());
        const auto name_bytes =
            parsed->placed ? parsed->placed->name.size() : 0;
        if (name_bytes > limits.max_total_name_bytes - counts.names)
            return std::unexpected(resource_error(source.source_offset));
        const auto item_bytes =
            source.type == 26 ? sizeof(VmfPlacement) : sizeof(VmfRemoval);
        if (sizeof(VmfDisplayEvent) > limits.max_output_bytes - counts.logical)
            return std::unexpected(resource_error(source.source_offset));
        counts.logical += sizeof(VmfDisplayEvent);
        if (item_bytes > limits.max_output_bytes - counts.logical)
            return std::unexpected(resource_error(source.source_offset));
        counts.logical += item_bytes;
        if (name_bytes > limits.max_output_bytes - counts.logical)
            return std::unexpected(resource_error(source.source_offset));
        counts.logical += name_bytes;
        counts.names += name_bytes;
        ++counter;
        ++counts.events;
    }
    return counts;
}
} // namespace

std::expected<VmfDisplayData, VmfError>
VmfDisplayData::decode(const VmfMovie& movie, VmfDisplayLimits limits) {
    const auto profile = movie.header().profile;
    if (profile != VmfProfile::pc_2_8 && profile != VmfProfile::xbox_2_5)
        return std::unexpected(error(VmfErrorCode::unsupported_profile, 0,
                                     "VMF display profile not covered"));
    const auto counts = preflight(movie, limits);
    if (!counts)
        return std::unexpected(counts.error());
    try {
        VmfDisplayData result;
        result.profile_ = profile;
        result.placements_.reserve(counts->placements);
        result.removals_.reserve(counts->removals);
        result.frames_.resize(movie.frames().size());
        result.events_.resize(counts->events);
        result.logical_output_bytes_ = counts->logical;
        for (std::size_t i = 0; i != movie.records().size(); ++i) {
            const auto& source = movie.records()[i];
            if (source.type != 26 && source.type != 28)
                continue;
            const auto parsed = record(movie, source, i, limits);
            if (!parsed)
                return std::unexpected(parsed.error());
            if (parsed->placed) {
                const auto& value = *parsed->placed;
                VmfPlacement placed{parsed->where,   value.depth,  value.flags,
                                    value.character, value.matrix, value.color,
                                    value.ratio,     value.clip,   {},
                                    value.xbox};
                if (!value.name.empty())
                    placed.name.assign(value.name.data(), value.name.size());
                result.placements_.push_back(std::move(placed));
            } else {
                result.removals_.push_back({parsed->where,
                                            parsed->removed_depth,
                                            parsed->removed_raw});
            }
            ++result.frames_[parsed->where.global_frame_index].event_count;
        }
        std::size_t first = 0;
        for (auto& frame : result.frames_) {
            frame.first_event = first;
            first += frame.event_count;
        }
        // Merge the two source-ordered collections. Temporarily advance each
        // frame's write cursor, then restore its published slice start.
        std::size_t p = 0, r = 0;
        while (p != result.placements_.size() || r != result.removals_.size()) {
            const auto place = p != result.placements_.size() &&
                               (r == result.removals_.size() ||
                                result.placements_[p].location.record_index <
                                    result.removals_[r].location.record_index);
            const auto index = place ? p++ : r++;
            const auto& where = place ? result.placements_[index].location
                                      : result.removals_[index].location;
            auto& frame = result.frames_[where.global_frame_index];
            result.events_[frame.first_event++] = {
                place ? VmfDisplayEventKind::place
                      : VmfDisplayEventKind::remove,
                index};
        }
        for (auto& frame : result.frames_)
            frame.first_event -= frame.event_count;
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(VmfErrorCode::resource_limit, 0,
                                     "VMF display allocation failed"));
    } catch (const std::length_error&) {
        return std::unexpected(resource_error());
    }
}

VmfDisplayData& VmfDisplayData::operator=(const VmfDisplayData& other) {
    if (this != &other) {
        VmfDisplayData prepared(other);
        *this = std::move(prepared);
    }
    return *this;
}
VmfProfile VmfDisplayData::profile() const noexcept { return profile_; }
std::span<const VmfPlacement> VmfDisplayData::placements() const& noexcept {
    return placements_;
}
std::span<const VmfRemoval> VmfDisplayData::removals() const& noexcept {
    return removals_;
}
std::span<const VmfDisplayFrame> VmfDisplayData::frames() const& noexcept {
    return frames_;
}
std::span<const VmfDisplayEvent> VmfDisplayData::events() const& noexcept {
    return events_;
}
std::expected<std::span<const VmfDisplayEvent>, VmfError>
VmfDisplayData::events_for_frame(std::size_t index) const& noexcept {
    if (index >= frames_.size())
        return std::unexpected(error(VmfErrorCode::invalid_frame, 0,
                                     "VMF display frame index invalid"));
    const auto& frame = frames_[index];
    return std::span(events_).subspan(frame.first_event, frame.event_count);
}
std::size_t VmfDisplayData::logical_output_bytes() const noexcept {
    return logical_output_bytes_;
}

} // namespace tdur::assets

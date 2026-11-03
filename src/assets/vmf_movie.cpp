#include "tdur/assets/vmf_movie.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace tdur::assets {
namespace {

constexpr auto no_index = std::numeric_limits<std::size_t>::max();

struct ParseFailure {
    VmfError error;
};

[[noreturn]] void reject(VmfErrorCode code, std::size_t offset,
                         std::string_view detail) {
    throw ParseFailure{{code, offset, detail}};
}

bool known_record(std::uint32_t value) noexcept {
    switch (value) {
    case 0:
    case 1:
    case 2:
    case 7:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
    case 15:
    case 20:
    case 26:
    case 28:
    case 37:
    case 39:
    case 43:
    case 46:
        return true;
    default:
        return false;
    }
}

bool definition_record(std::uint32_t value) noexcept {
    switch (value) {
    case 2:
    case 7:
    case 10:
    case 11:
    case 14:
    case 20:
    case 37:
    case 39:
    case 46:
        return true;
    default:
        return false;
    }
}

bool known_action(std::uint16_t value) noexcept {
    switch (value) {
    case 0x00:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x17:
    case 0x18:
    case 0x1c:
    case 0x1d:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x26:
    case 0x81:
    case 0x83:
    case 0x8b:
    case 0x8c:
    case 0x96:
    case 0x99:
    case 0x9a:
    case 0x9d:
    case 0x9e:
    case 0x9f:
        return true;
    default:
        return false;
    }
}

std::uint32_t header_crc(std::array<std::byte, 68> canonical,
                         bool big_endian) noexcept {
    if (big_endian) {
        for (const auto offset : {6U, 10U, 60U, 62U, 64U, 66U}) {
            std::swap(canonical[offset], canonical[offset + 1]);
        }
        for (const auto offset : {44U, 48U, 52U, 56U}) {
            std::swap(canonical[offset], canonical[offset + 3]);
            std::swap(canonical[offset + 1], canonical[offset + 2]);
        }
    }
    std::uint32_t crc = 0xffffffffU;
    for (const auto value : canonical) {
        crc ^= std::to_integer<std::uint32_t>(value);
        for (unsigned bit = 0; bit != 8; ++bit) {
            const auto polynomial = (crc & 1U) != 0 ? 0xedb88320U : 0U;
            crc = (crc >> 1) ^ polynomial;
        }
    }
    return crc ^ 0xffffffffU;
}

} // namespace

namespace detail {

class VmfMovieParser {
  public:
    VmfMovieParser(std::span<const std::byte> source, VmfProfile profile,
                   VmfLimits limits)
        : data_(source), profile_(profile), limits_(limits) {}

    VmfMovie parse() {
        if (profile_ != VmfProfile::pc_2_8 &&
            profile_ != VmfProfile::xbox_2_5) {
            reject(VmfErrorCode::unsupported_profile, 0,
                   "unknown explicit VMF profile");
        }
        big_ = profile_ == VmfProfile::xbox_2_5;
        if (data_.size() > limits_.max_input_bytes ||
            data_.size() > std::numeric_limits<std::uint32_t>::max()) {
            reject(VmfErrorCode::resource_limit, 0, "input byte limit");
        }
        parse_header();
        charge(data_.size(), 0);
        charge(sizeof(VmfHeader), 0);
        const auto count =
            static_cast<std::size_t>(movie_.header_.definition_count);
        if (count == 0) {
            reject(VmfErrorCode::invalid_definition, 62,
                   "root requires a definition ID");
        }
        count_growth(0, count, limits_.max_definitions, 62);
        charge(product(count, sizeof(VmfDefinition), 62), 62);
        movie_.source_.assign(data_.begin(), data_.end());
        data_ = movie_.source_;
        movie_.definitions_.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            movie_.definitions_[i] = {narrow(i, 62), no_index};
        }

        push_sprite(72, data_.size(), {}, true);
        std::size_t root_end = 0;
        while (!work_.empty()) {
            const auto state = work_.back();
            const auto pos = state.cursor;
            require(pos, 8, state.end);
            const auto type = u32(pos, state.end);
            const auto declared = u32(pos + 4, state.end);
            if (type == 0) {
                const auto expected = work_.size() == 1 ? 0U : 8U;
                if (declared != expected || state.end - pos != 8 ||
                    state.frame != movie_.sprites_[state.sprite].frame_count) {
                    reject(VmfErrorCode::invalid_hierarchy, pos,
                           "incorrect terminal sprite End");
                }
                append(movie_.records_,
                       VmfRecord{narrow(pos, pos),
                                 0,
                                 declared,
                                 8,
                                 range(pos + 8, 0),
                                 state.sprite,
                                 {}},
                       limits_.max_records, pos);
                if (work_.size() == 1) {
                    root_end = state.end;
                }
                work_.pop_back();
                continue;
            }
            if (!known_record(type)) {
                reject(VmfErrorCode::unsupported_record, pos,
                       "record type outside observed subset");
            }
            if (declared < 8 || declared % 4 != 0 || pos % 4 != 0) {
                reject(VmfErrorCode::invalid_hierarchy, pos,
                       "invalid record metadata size or alignment");
            }
            const auto record_end = bounded_end(pos, declared, state.end);
            const auto& sprite = movie_.sprites_[state.sprite];
            if (state.frame >= sprite.frame_count) {
                reject(VmfErrorCode::invalid_frame, pos,
                       "record after final direct ShowFrame");
            }
            const auto frame_end = static_cast<std::size_t>(
                movie_.frames_[sprite.first_frame + state.frame].end_offset);
            if (type == 39) {
                require(pos, 28, record_end);
                const auto child_end =
                    bounded_end(pos, u32(pos + 16, record_end), state.end);
                if (child_end > frame_end - 8) {
                    reject(VmfErrorCode::invalid_frame, pos,
                           "child overlaps its parent's ShowFrame");
                }
                work_.back().cursor = child_end;
                push_sprite(pos, state.end, state.sprite, false);
                continue;
            }
            if (record_end > (type == 1 ? frame_end : frame_end - 8)) {
                reject(VmfErrorCode::invalid_frame, pos,
                       "record crosses its direct frame");
            }
            work_.back().cursor = record_end;
            const auto ri = append(movie_.records_,
                                   VmfRecord{narrow(pos, pos),
                                             type,
                                             declared,
                                             declared,
                                             range(pos + 8, declared - 8),
                                             state.sprite,
                                             {}},
                                   limits_.max_records, pos);
            register_definition(ri);
            const auto frame = static_cast<std::uint16_t>(state.frame);
            if (type == 1) {
                if (declared != 8 || record_end != frame_end) {
                    reject(VmfErrorCode::invalid_frame, pos,
                           "ShowFrame disagrees with frame end table");
                }
                movie_.frames_[sprite.first_frame + state.frame]
                    .show_frame_record_index = ri;
                ++work_.back().frame;
            } else if (type == 12) {
                parse_script(pos + 8, record_end, ri, state.sprite, frame, {});
            } else if (type == 7) {
                parse_button(ri, state.sprite, pos, record_end);
            } else if (type == 43) {
                const auto text = string_at(pos + 8, record_end);
                zero_tail(static_cast<std::size_t>(text.offset) + text.size + 1,
                          record_end);
                append(movie_.labels_, VmfLabel{ri, state.sprite, frame, text},
                       limits_.max_labels, pos);
            } else if (type == 20) {
                const auto id = *movie_.records_[ri].definition_id;
                append(movie_.images_,
                       VmfImageReference{
                           id, ri, VmfImageSource::direct_fla, id, {}},
                       limits_.max_images, pos);
            }
        }
        validate_references();
        parse_atlas(root_end);
        return std::move(movie_);
    }

  private:
    struct Work {
        std::size_t sprite{};
        std::size_t cursor{};
        std::size_t end{};
        std::size_t frame{};
    };

    std::span<const std::byte> data_;
    VmfProfile profile_;
    VmfLimits limits_;
    bool big_{};
    VmfMovie movie_;
    std::vector<Work> work_;

    static std::uint32_t narrow(std::size_t value, std::size_t at) {
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            reject(VmfErrorCode::resource_limit, at, "32-bit source range");
        }
        return static_cast<std::uint32_t>(value);
    }

    static VmfByteRange range(std::size_t at, std::size_t size) {
        return {narrow(at, at), narrow(size, at)};
    }

    static std::size_t product(std::size_t count, std::size_t size,
                               std::size_t at) {
        if (count > std::numeric_limits<std::size_t>::max() / size) {
            reject(VmfErrorCode::resource_limit, at, "byte product overflow");
        }
        return count * size;
    }

    static void count_growth(std::size_t used, std::size_t extra,
                             std::size_t limit, std::size_t at) {
        if (used > limit || extra > limit - used) {
            reject(VmfErrorCode::resource_limit, at, "collection count limit");
        }
    }

    void charge(std::size_t bytes, std::size_t at) {
        const auto used = movie_.logical_output_bytes_;
        if (used > limits_.max_output_bytes ||
            bytes > limits_.max_output_bytes - used) {
            reject(VmfErrorCode::resource_limit, at,
                   "logical output byte limit");
        }
        movie_.logical_output_bytes_ += bytes;
    }

    template <class T>
    std::size_t append(std::vector<T>& target, T value, std::size_t limit,
                       std::size_t at) {
        count_growth(target.size(), 1, limit, at);
        charge(sizeof(T), at);
        const auto index = target.size();
        target.push_back(std::move(value));
        return index;
    }

    void require(std::size_t at, std::size_t count, std::size_t end) const {
        if (end > data_.size() || at > end || count > end - at) {
            reject(VmfErrorCode::truncated, at, "field exceeds its own region");
        }
    }

    std::size_t bounded_end(std::size_t at, std::size_t count,
                            std::size_t end) const {
        require(at, count, end);
        return at + count;
    }

    std::uint16_t u16(std::size_t at, std::size_t end) const {
        require(at, 2, end);
        const auto a = std::to_integer<std::uint32_t>(data_[at]);
        const auto b = std::to_integer<std::uint32_t>(data_[at + 1]);
        return static_cast<std::uint16_t>(big_ ? (a << 8) | b : a | (b << 8));
    }

    std::uint32_t u32(std::size_t at, std::size_t end) const {
        require(at, 4, end);
        std::uint32_t value = 0;
        for (unsigned i = 0; i != 4; ++i) {
            const auto shift = 8U * (big_ ? 3U - i : i);
            value |= std::to_integer<std::uint32_t>(data_[at + i]) << shift;
        }
        return value;
    }

    float finite_float(std::size_t at, std::size_t end) const {
        const auto value = std::bit_cast<float>(u32(at, end));
        if (!std::isfinite(value)) {
            reject(VmfErrorCode::invalid_numeric, at, "nonfinite float");
        }
        return value;
    }

    void parse_header() {
        require(0, 72, data_.size());
        constexpr std::array signature{std::byte{0x56}, std::byte{0x4d},
                                       std::byte{0x46}, std::byte{0}};
        for (std::size_t i = 0; i < signature.size(); ++i) {
            if (data_[i] != signature[i]) {
                reject(VmfErrorCode::invalid_header, i, "VMF magic mismatch");
            }
        }
        auto& h = movie_.header_;
        h.profile = profile_;
        h.raw_04 = data_[4];
        h.major_version = std::to_integer<std::uint8_t>(data_[5]);
        h.minor_version = u16(6, 72);
        if (h.major_version != 2 || h.minor_version != (big_ ? 5U : 8U)) {
            reject(VmfErrorCode::unsupported_version, 5,
                   "resource version does not match explicit profile");
        }
        h.raw_08 = {data_[8], data_[9]};
        if (data_[8] != (big_ ? std::byte{1} : std::byte{2})) {
            reject(VmfErrorCode::invalid_header, 8,
                   "profile byte order marker mismatch");
        }
        h.raw_0a = u16(10, 72);
        for (std::size_t i = 0; i < h.author.size(); ++i) {
            h.author[i] = data_[12 + i];
        }
        h.raw_3c = u16(60, 72);
        h.definition_count = u16(62, 72);
        h.raw_40 = u16(64, 72);
        h.raw_42 = u16(66, 72);
        h.crc32 = u32(68, 72);
        std::array<std::byte, 68> canonical{};
        for (std::size_t i = 0; i < canonical.size(); ++i) {
            canonical[i] = data_[i];
        }
        if (header_crc(canonical, big_) != h.crc32) {
            reject(VmfErrorCode::invalid_crc, 68,
                   "normalized header CRC32 mismatch");
        }
        for (std::size_t i = 0; i < h.rectangle.size(); ++i) {
            h.rectangle[i] = finite_float(44 + 4 * i, 72);
        }
    }

    void register_definition(std::size_t ri) {
        auto& r = movie_.records_[ri];
        if (!definition_record(r.type)) {
            return;
        }
        const auto at = static_cast<std::size_t>(r.source_offset);
        const auto end = bounded_end(at, r.declared_size, data_.size());
        const auto id = r.type == 10
                            ? static_cast<std::uint32_t>(u16(at + 8, end))
                            : u32(at + 8, end);
        if (id >= movie_.definitions_.size() ||
            movie_.definitions_[id].record_index != no_index) {
            reject(VmfErrorCode::invalid_definition, at + 8,
                   "duplicate or out-of-range definition ID");
        }
        r.definition_id = id;
        movie_.definitions_[id].record_index = ri;
    }

    void push_sprite(std::size_t pos, std::size_t parent_end,
                     std::optional<std::size_t> parent, bool root) {
        count_growth(work_.size(), 1, limits_.max_depth, pos);
        require(pos, 8, parent_end);
        if (pos % 4 != 0 || u32(pos, parent_end) != 39) {
            reject(VmfErrorCode::invalid_hierarchy, pos,
                   "expected aligned sprite record");
        }
        const auto metadata = u32(pos + 4, parent_end);
        if (metadata < 28 || metadata % 4 != 0) {
            reject(VmfErrorCode::unsupported_layout, pos + 4,
                   "sprite requires observed nonempty metadata");
        }
        const auto meta_end = bounded_end(pos, metadata, parent_end);
        const auto id = u32(pos + 8, meta_end);
        const auto frames = u16(pos + 12, meta_end);
        const auto raw_0e = u16(pos + 14, meta_end);
        const auto extent = u32(pos + 16, meta_end);
        const auto expected_metadata =
            24U + 4U * static_cast<std::uint32_t>(frames);
        if (frames == 0 || metadata != expected_metadata ||
            u32(pos + 20, meta_end) != metadata || extent < metadata ||
            extent - metadata < 8) {
            reject(VmfErrorCode::invalid_hierarchy, pos,
                   "sprite metadata or extent mismatch");
        }
        const auto end = bounded_end(pos, extent, parent_end);
        if (root && (id != 0 || raw_0e != movie_.header_.raw_40)) {
            reject(VmfErrorCode::invalid_header, pos + 8,
                   "root ID or raw header relationship mismatch");
        }
        std::size_t previous = meta_end;
        for (std::size_t i = 0; i < frames; ++i) {
            const auto frame_end =
                bounded_end(pos, u32(pos + 24 + 4 * i, meta_end), end - 8);
            if (frame_end < previous || frame_end - previous < 8) {
                reject(VmfErrorCode::invalid_frame, pos + 24 + 4 * i,
                       "frame end is not strictly increasing");
            }
            previous = frame_end;
        }
        if (previous != end - 8) {
            reject(VmfErrorCode::invalid_frame, pos + 24,
                   "last frame does not end at terminal End");
        }
        count_growth(movie_.frames_.size(), frames, limits_.max_frames, pos);
        const auto ri = append(movie_.records_,
                               VmfRecord{narrow(pos, pos),
                                         39,
                                         metadata,
                                         extent,
                                         range(pos + 8, metadata - 8),
                                         parent,
                                         {}},
                               limits_.max_records, pos);
        register_definition(ri);
        const auto first = movie_.frames_.size();
        const auto si =
            append(movie_.sprites_, VmfSprite{ri, id, raw_0e, first, frames},
                   limits_.max_sprites, pos);
        charge(product(frames, sizeof(VmfFrame), pos), pos);
        movie_.frames_.resize(first + frames);
        previous = meta_end;
        for (std::size_t i = 0; i < frames; ++i) {
            const auto frame_end =
                bounded_end(pos, u32(pos + 24 + 4 * i, meta_end), end - 8);
            movie_.frames_[first + i] = VmfFrame{
                si, static_cast<std::uint16_t>(i), narrow(previous, pos),
                narrow(frame_end, pos), no_index};
            previous = frame_end;
        }
        work_.push_back(Work{si, meta_end, end, 0});
    }

    VmfByteRange string_at(std::size_t begin, std::size_t end) const {
        require(begin, 0, end);
        for (auto pos = begin; pos < end; ++pos) {
            if (data_[pos] == std::byte{}) {
                return range(begin, pos - begin);
            }
        }
        reject(VmfErrorCode::invalid_action, begin,
               "string has no NUL in its own region");
    }

    void zero_tail(std::size_t begin, std::size_t end) const {
        require(begin, 0, end);
        if (end - begin > 3) {
            reject(VmfErrorCode::invalid_action, begin,
                   "excessive trailing padding");
        }
        for (auto pos = begin; pos < end; ++pos) {
            if (data_[pos] != std::byte{}) {
                reject(VmfErrorCode::invalid_action, pos,
                       "nonzero trailing padding");
            }
        }
    }

    void operands(VmfAction& action, std::size_t end) {
        const auto p = static_cast<std::size_t>(action.payload.offset);
        const auto n = action.payload.size;
        const auto payload_end = bounded_end(p, n, end);
        const auto exact_string = [&]() {
            const auto text = string_at(p, payload_end);
            if (static_cast<std::size_t>(text.offset) + text.size + 1 !=
                payload_end) {
                reject(VmfErrorCode::invalid_action, p,
                       "extra bytes in string operand");
            }
            action.strings[0] = text;
        };
        switch (action.opcode) {
        case 0x81:
            if (n != 2) {
                reject(VmfErrorCode::invalid_action, p, "GotoFrame size");
            }
            break;
        case 0x83: {
            const auto first = string_at(p, payload_end);
            const auto second = string_at(
                static_cast<std::size_t>(first.offset) + first.size + 1,
                payload_end);
            if (static_cast<std::size_t>(second.offset) + second.size + 1 !=
                payload_end) {
                reject(VmfErrorCode::invalid_action, p, "GetURL string count");
            }
            action.strings = {first, second};
            break;
        }
        case 0x8b:
        case 0x8c:
            exact_string();
            break;
        case 0x96: {
            if (n < 2 || data_[p] != std::byte{}) {
                reject(VmfErrorCode::unsupported_layout, p,
                       "only single-string Push is covered");
            }
            const auto text = string_at(p + 1, payload_end);
            if (static_cast<std::size_t>(text.offset) + text.size + 1 !=
                payload_end) {
                reject(VmfErrorCode::unsupported_layout, p,
                       "multiple Push values are not covered");
            }
            action.strings[0] = text;
            break;
        }
        case 0x99:
        case 0x9d:
            if (n != 2) {
                reject(VmfErrorCode::invalid_action, p, "branch operand size");
            }
            count_growth(movie_.branch_count_, 1, limits_.max_branches, p);
            ++movie_.branch_count_;
            action.branch = VmfBranch{
                std::bit_cast<std::int16_t>(u16(p, payload_end)), no_index};
            break;
        case 0x9a:
            if (n != 1) {
                reject(VmfErrorCode::invalid_action, p, "GetURL2 operand size");
            }
            break;
        case 0x9e:
            if (n != 0) {
                reject(VmfErrorCode::invalid_action, p, "Call operand size");
            }
            break;
        case 0x9f:
            if (n != 1) {
                reject(VmfErrorCode::unsupported_layout, p,
                       "GotoFrame2 variant outside observed subset");
            }
            break;
        default:
            break;
        }
    }

    void parse_script(std::size_t begin, std::size_t end, std::size_t owner,
                      std::size_t parent, std::optional<std::uint16_t> frame,
                      std::optional<VmfButtonCondition> condition) {
        count_growth(movie_.scripts_.size(), 1, limits_.max_scripts, begin);
        const auto first = movie_.actions_.size();
        auto pos = begin;
        std::size_t compact = 0;
        bool ended = false;
        while (pos < end) {
            const auto op = u16(pos, end);
            if (!known_action(op)) {
                reject(VmfErrorCode::unsupported_action, pos,
                       "action opcode outside observed subset");
            }
            std::size_t length = 2;
            std::size_t payload_size = 0;
            std::size_t payload_start = pos + 2;
            if (op >= 0x80) {
                require(pos, 4, end);
                payload_size = u16(pos + 2, end);
                length = 4 + payload_size + payload_size % 2;
                payload_start = pos + 4;
            }
            const auto next = bounded_end(pos, length, end);
            if (payload_size % 2 != 0 &&
                data_[payload_start + payload_size] != std::byte{}) {
                reject(VmfErrorCode::invalid_action,
                       payload_start + payload_size,
                       "nonzero action alignment byte");
            }
            VmfAction action{narrow(pos, pos),
                             op,
                             narrow(length, pos),
                             range(payload_start, payload_size),
                             narrow(compact, pos),
                             {},
                             {}};
            operands(action, next);
            append(movie_.actions_, std::move(action), limits_.max_actions,
                   pos);
            const auto advance = op < 0x80 ? 1 : 3 + payload_size;
            if (advance > std::numeric_limits<std::uint32_t>::max() - compact) {
                reject(VmfErrorCode::resource_limit, pos,
                       "compact script offset overflow");
            }
            compact += advance;
            pos = next;
            if (op == 0) {
                zero_tail(pos, end);
                ended = true;
                break;
            }
        }
        if (!ended) {
            reject(VmfErrorCode::invalid_action, end, "missing script End");
        }
        const auto count = movie_.actions_.size() - first;
        auto* const action_begin = movie_.actions_.data() + first;
        auto* const action_end = action_begin + count;
        for (std::size_t i = first; i < first + count; ++i) {
            auto& action = movie_.actions_[i];
            if (!action.branch) {
                continue;
            }
            const auto target =
                static_cast<std::int64_t>(action.compact_offset) + 5 +
                action.branch->delta;
            if (target < 0 || target >= static_cast<std::int64_t>(compact)) {
                reject(VmfErrorCode::invalid_branch, action.source_offset,
                       "branch outside its own compact stream");
            }
            const auto found = std::lower_bound(
                action_begin, action_end, static_cast<std::uint32_t>(target),
                [](const VmfAction& candidate, std::uint32_t value) {
                    return candidate.compact_offset < value;
                });
            if (found == action_end || found->compact_offset != target) {
                reject(VmfErrorCode::invalid_branch, action.source_offset,
                       "branch target is not an exact instruction boundary");
            }
            action.branch->target_action_index =
                first + static_cast<std::size_t>(found - action_begin);
        }
        append(movie_.scripts_,
               VmfScript{condition ? VmfScriptKind::button_condition
                                   : VmfScriptKind::timeline,
                         owner, parent, frame, condition,
                         range(begin, end - begin), first, count,
                         narrow(compact, begin)},
               limits_.max_scripts, begin);
    }

    void parse_button(std::size_t ri, std::size_t parent, std::size_t begin,
                      std::size_t end) {
        const auto fixed = big_ ? 16U : 20U;
        require(begin, fixed, end);
        const auto off =
            static_cast<std::size_t>(u32(begin + (big_ ? 12 : 16), end));
        const auto terminator = bounded_end(begin, off, end);
        require(terminator, 10, end);
        if (off < fixed || (off - fixed) % 48 != 0) {
            reject(VmfErrorCode::invalid_hierarchy, begin,
                   "button visual array placement mismatch");
        }
        if ((off - fixed) / 48 != 1) {
            reject(VmfErrorCode::unsupported_layout, begin,
                   "only singleton button visual is covered");
        }
        const auto p = begin + fixed;
        require(p, 48, terminator);
        if (u32(terminator, end) != 0) {
            reject(VmfErrorCode::invalid_action, terminator,
                   "button visual terminator is not zero");
        }
        VmfButtonVisual visual{};
        visual.record_index = ri;
        visual.source_offset = narrow(p, p);
        if (!big_) {
            visual.pc_extra_word = u32(begin + 12, end);
        }
        visual.states = u32(p, terminator);
        visual.character_id = u16(p + 4, terminator);
        visual.raw_06 = u16(p + 6, terminator);
        for (std::size_t i = 0; i < visual.raw_matrix.size(); ++i) {
            visual.raw_matrix[i] = finite_float(p + 8 + 4 * i, terminator);
        }
        for (std::size_t i = 0; i < visual.raw_words.size(); ++i) {
            visual.raw_words[i] = u16(p + 32 + 2 * i, terminator);
        }
        append(movie_.button_visuals_, visual, limits_.max_button_visuals, p);
        auto pos = terminator + 4;
        std::size_t ordinal = 0;
        bool last = false;
        while (pos < end) {
            require(pos, 4, end);
            const auto size = u16(pos, end);
            const auto flags = u16(pos + 2, end);
            const auto next = size != 0 ? bounded_end(pos, size, end) : end;
            if (pos % 4 != 0 || next % 4 != 0 || next - pos < 6) {
                reject(VmfErrorCode::invalid_action, pos,
                       "invalid physical button condition extent");
            }
            parse_script(
                pos + 4, next, ri, parent, {},
                VmfButtonCondition{narrow(pos, pos), size, flags, ordinal});
            pos = next;
            if (size == 0) {
                last = true;
                break;
            }
            count_growth(ordinal, 1, std::numeric_limits<std::size_t>::max(),
                         pos);
            ++ordinal;
        }
        if (!last || pos != end) {
            reject(VmfErrorCode::invalid_action, pos,
                   "button chain has no terminal condition");
        }
    }

    void validate_references() const {
        for (const auto& definition : movie_.definitions_) {
            if (definition.record_index == no_index) {
                reject(VmfErrorCode::invalid_definition, 62,
                       "definition IDs are not dense");
            }
        }
        for (const auto& visual : movie_.button_visuals_) {
            if (visual.character_id >= movie_.definitions_.size()) {
                reject(VmfErrorCode::invalid_reference,
                       visual.source_offset + 4,
                       "button character ID is absent");
            }
        }
    }

    void parse_atlas(std::size_t begin) {
        require(begin, 8, data_.size());
        if (u32(begin, data_.size()) != 0x54584958U) {
            reject(VmfErrorCode::invalid_header, begin, "TXIX magic mismatch");
        }
        const auto count =
            static_cast<std::size_t>(u32(begin + 4, data_.size()));
        count_growth(0, count, limits_.max_atlas_entries, begin + 4);
        const auto entries = begin + 8;
        const auto end =
            bounded_end(entries, product(count, 32, begin), data_.size());
        if (end != data_.size()) {
            reject(VmfErrorCode::invalid_hierarchy, end,
                   "bytes after declared TXIX records");
        }
        if (!big_ && count != 0) {
            reject(VmfErrorCode::unsupported_layout, begin,
                   "nonempty PC TXIX outside observed profile");
        }
        std::vector<std::size_t> image_by_id(movie_.definitions_.size(),
                                             no_index);
        for (std::size_t i = 0; i < movie_.images_.size(); ++i) {
            image_by_id[movie_.images_[i].character_id] = i;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto p = entries + 32 * i;
            const auto own_end = p + 32;
            require(p, 32, end);
            VmfAtlasEntry entry{};
            entry.source_offset = narrow(p, p);
            entry.character_id = u32(p, own_end);
            if (entry.character_id >= image_by_id.size() ||
                image_by_id[entry.character_id] == no_index) {
                reject(VmfErrorCode::invalid_reference, p,
                       "TXIX ID must name a type20 image");
            }
            auto& image = movie_.images_[image_by_id[entry.character_id]];
            if (image.atlas_entry_index) {
                reject(VmfErrorCode::invalid_reference, p,
                       "duplicate TXIX image ID");
            }
            entry.page_number = u32(p + 4, own_end);
            entry.raw_words = {u32(p + 8, own_end), u32(p + 12, own_end)};
            for (std::size_t j = 0; j < entry.raw_floats.size(); ++j) {
                const auto value = finite_float(p + 16 + 4 * j, own_end);
                if (value < 0.0F || value > 1.0F) {
                    reject(VmfErrorCode::invalid_numeric, p + 16 + 4 * j,
                           "TXIX normalized value outside [0,1]");
                }
                entry.raw_floats[j] = value;
            }
            const auto ai = append(movie_.atlas_entries_, entry,
                                   limits_.max_atlas_entries, p);
            image.source = VmfImageSource::atlas_page;
            image.source_number = entry.page_number;
            image.atlas_entry_index = ai;
        }
    }
};

} // namespace detail

VmfMovie& VmfMovie::operator=(const VmfMovie& other) {
    if (this != &other) {
        VmfMovie replacement(other);
        *this = std::move(replacement);
    }
    return *this;
}

std::expected<VmfMovie, VmfError>
VmfMovie::parse(std::span<const std::byte> bytes, VmfProfile profile,
                VmfLimits limits) {
    try {
        return detail::VmfMovieParser(bytes, profile, limits).parse();
    } catch (const ParseFailure& failure) {
        return std::unexpected(failure.error);
    } catch (const std::bad_alloc&) {
        return std::unexpected(
            VmfError{VmfErrorCode::resource_limit, 0, "VMF allocation failed"});
    } catch (const std::length_error&) {
        return std::unexpected(VmfError{VmfErrorCode::resource_limit, 0,
                                        "VMF container length exceeded"});
    }
}

const VmfHeader& VmfMovie::header() const& noexcept { return header_; }

std::span<const std::byte> VmfMovie::source_bytes() const& noexcept {
    return source_;
}

std::expected<std::span<const std::byte>, VmfError>
VmfMovie::bytes(VmfByteRange range) const& noexcept {
    if (range.offset > source_.size() ||
        range.size > source_.size() - range.offset) {
        return std::unexpected(VmfError{VmfErrorCode::invalid_range,
                                        range.offset,
                                        "range exceeds owned source"});
    }
    return std::span<const std::byte>(source_).subspan(range.offset,
                                                       range.size);
}

std::span<const VmfRecord> VmfMovie::records() const& noexcept {
    return records_;
}
std::span<const VmfSprite> VmfMovie::sprites() const& noexcept {
    return sprites_;
}
std::span<const VmfFrame> VmfMovie::frames() const& noexcept { return frames_; }
std::span<const VmfDefinition> VmfMovie::definitions() const& noexcept {
    return definitions_;
}
std::span<const VmfScript> VmfMovie::scripts() const& noexcept {
    return scripts_;
}
std::span<const VmfAction> VmfMovie::actions() const& noexcept {
    return actions_;
}
std::span<const VmfButtonVisual> VmfMovie::button_visuals() const& noexcept {
    return button_visuals_;
}
std::span<const VmfLabel> VmfMovie::labels() const& noexcept { return labels_; }
std::span<const VmfImageReference> VmfMovie::images() const& noexcept {
    return images_;
}
std::span<const VmfAtlasEntry> VmfMovie::atlas_entries() const& noexcept {
    return atlas_entries_;
}
std::optional<std::size_t>
VmfMovie::definition(std::uint32_t character_id) const noexcept {
    if (character_id >= definitions_.size()) {
        return {};
    }
    return definitions_[character_id].record_index;
}
std::size_t VmfMovie::branch_count() const noexcept { return branch_count_; }
std::size_t VmfMovie::logical_output_bytes() const noexcept {
    return logical_output_bytes_;
}

} // namespace tdur::assets

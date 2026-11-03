#include "tdur/assets/vmf_display_data.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Observes/fails ordinary operator-new allocations of this single-threaded
// fixture, not direct malloc or arbitrary aligned allocations.
namespace display_allocation_fixture {
thread_local bool enabled{};
thread_local std::size_t calls{};
thread_local std::size_t fail_at = std::numeric_limits<std::size_t>::max();
void* allocate(std::size_t bytes) {
    if (enabled && calls++ == fail_at)
        throw std::bad_alloc();
    if (auto* memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc();
}
struct Scope {
    explicit Scope(
        std::size_t failure = std::numeric_limits<std::size_t>::max()) noexcept
        : old_enabled(enabled), old_calls(calls), old_failure(fail_at) {
        enabled = true;
        calls = 0;
        fail_at = failure;
    }
    ~Scope() noexcept {
        enabled = old_enabled;
        calls = old_calls;
        fail_at = old_failure;
    }
    std::size_t count() const noexcept { return calls; }
    bool old_enabled;
    std::size_t old_calls;
    std::size_t old_failure;
};
} // namespace display_allocation_fixture

void* operator new(std::size_t bytes) {
    return display_allocation_fixture::allocate(bytes);
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {
using namespace tdur::assets;
using Bytes = std::vector<std::byte>;
using Profile = VmfProfile;
using Code = VmfErrorCode;
using Result = std::expected<VmfDisplayData, VmfError>;
constexpr std::array Profiles{Profile::pc_2_8, Profile::xbox_2_5};
std::size_t checks{}, failures{}, decode_faults{}, copy_faults{},
    corpus_movies{};

void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 30)
            std::cerr << "FAIL: " << message << '\n';
    }
}
void require(bool passed, std::string_view message) {
    check(passed, message);
    if (!passed)
        throw std::runtime_error(std::string(message));
}

template <class T>
concept TemporaryPlacements =
    requires(T&& value) { std::move(value).placements(); };
template <class T>
concept TemporaryRemovals =
    requires(T&& value) { std::move(value).removals(); };
template <class T>
concept TemporaryFrames = requires(T&& value) { std::move(value).frames(); };
template <class T>
concept TemporaryEvents = requires(T&& value) { std::move(value).events(); };
template <class T>
concept TemporaryFrameEvents =
    requires(T&& value) { std::move(value).events_for_frame(0); };
static_assert(!TemporaryPlacements<VmfDisplayData> &&
              !TemporaryPlacements<const VmfDisplayData>);
static_assert(!TemporaryRemovals<VmfDisplayData> &&
              !TemporaryRemovals<const VmfDisplayData>);
static_assert(!TemporaryFrames<VmfDisplayData> &&
              !TemporaryFrames<const VmfDisplayData>);
static_assert(!TemporaryEvents<VmfDisplayData> &&
              !TemporaryEvents<const VmfDisplayData>);
static_assert(!TemporaryFrameEvents<VmfDisplayData> &&
              !TemporaryFrameEvents<const VmfDisplayData>);
static_assert(!std::is_default_constructible_v<VmfDisplayData>);
static_assert(std::is_copy_constructible_v<VmfDisplayData> &&
              std::is_copy_assignable_v<VmfDisplayData>);
static_assert(std::is_nothrow_move_constructible_v<VmfDisplayData> &&
              std::is_nothrow_move_assignable_v<VmfDisplayData>);
static_assert(noexcept(std::declval<const VmfDisplayData&>().placements()));
static_assert(
    noexcept(std::declval<const VmfDisplayData&>().events_for_frame(0)));

void put(Bytes& bytes, std::size_t offset, std::uint32_t value,
         std::size_t width, Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto index = profile == Profile::pc_2_8 ? i : width - 1 - i;
        bytes.at(offset + i) = std::byte((value >> (8 * index)) & 0xffU);
    }
}
void append(Bytes& bytes, std::span<const std::byte> source) {
    bytes.insert(bytes.end(), source.begin(), source.end());
}
void append_value(Bytes& bytes, std::uint32_t value, std::size_t width,
                  Profile profile) {
    const auto offset = bytes.size();
    bytes.resize(offset + width);
    put(bytes, offset, value, width, profile);
}
void align4(Bytes& bytes) {
    while (bytes.size() % 4)
        bytes.push_back(std::byte{});
}
Bytes record(Profile profile, std::uint32_t type, std::size_t size = 8) {
    Bytes bytes(size);
    put(bytes, 0, type, 4, profile);
    put(bytes, 4, static_cast<std::uint32_t>(size), 4, profile);
    return bytes;
}
void finish_record(Bytes& bytes, Profile profile) {
    align4(bytes);
    put(bytes, 4, static_cast<std::uint32_t>(bytes.size()), 4, profile);
}

constexpr std::array<std::uint32_t, 6> Matrix{0x3f900001U, 0xbf200003U,
                                              0x00000001U, 0x80000000U,
                                              0x42768000U, 0xc3527001U};
constexpr std::array<std::uint32_t, 6> Identity{0x3f800000U, 0x3f800000U, 0,
                                                0,           0,           0};
constexpr std::array<std::uint16_t, 8> Colors{
    0xffffU, 0x8000U, 0, 0x0100U, 0x0203U, 0x7fffU, 0x1122U, 0x0080U};

struct Fields {
    std::uint16_t depth = 3;
    std::uint16_t flags = 0x7e;
    std::uint16_t character = 1;
    std::array<std::uint32_t, 6> matrix = Matrix;
    std::array<std::uint16_t, 8> colors = Colors;
    std::uint16_t ratio = 0xbeefU;
    std::uint16_t clip = 0x3456U;
    std::uint16_t reserved_38 = 0x789aU;
    std::string name = "instance";
};

// Independent source-owned encoders from the frozen field grammar, not calls
// into the decoder under test. PC clip precedes the unaligned name even when
// the original corpus contains no record with both flags40 and20.
Bytes placement(Profile profile, const Fields& fields) {
    if (profile == Profile::xbox_2_5) {
        auto bytes = record(profile, 26, 60);
        put(bytes, 8, fields.depth, 2, profile);
        put(bytes, 10, fields.character, 2, profile);
        for (std::size_t i = 0; i < 6; ++i)
            put(bytes, 12 + 4 * i, fields.matrix[i], 4, profile);
        for (std::size_t i = 0; i < 8; ++i)
            put(bytes, 36 + 2 * i, fields.colors[i], 2, profile);
        put(bytes, 52, fields.flags, 2, profile);
        put(bytes, 54, fields.ratio, 2, profile);
        put(bytes, 56, fields.reserved_38, 2, profile);
        put(bytes, 58, fields.clip, 2, profile);
        for (const auto value : fields.name)
            bytes.push_back(std::byte(static_cast<unsigned char>(value)));
        bytes.push_back(std::byte{});
        finish_record(bytes, profile);
        return bytes;
    }
    auto bytes = record(profile, 26, 12);
    put(bytes, 8, fields.depth, 2, profile);
    put(bytes, 10, fields.flags, 2, profile);
    if (fields.flags & 2U)
        append_value(bytes, fields.character, 2, profile);
    if (fields.flags & 4U) {
        align4(bytes);
        for (const auto [flag, component] :
             {std::pair{0x100U, 0U}, std::pair{0x80U, 2U},
              std::pair{0x200U, 4U}})
            if (fields.flags & flag) {
                append_value(bytes, fields.matrix[component], 4, profile);
                append_value(bytes, fields.matrix[component + 1], 4, profile);
            }
    }
    if (fields.flags & 8U) {
        align4(bytes);
        for (const auto word : fields.colors)
            append_value(bytes, word, 2, profile);
    }
    if (fields.flags & 0x10U)
        append_value(bytes, fields.ratio, 2, profile);
    if (fields.flags & 0x40U)
        append_value(bytes, fields.clip, 2, profile);
    if (fields.flags & 0x20U) {
        for (const auto value : fields.name)
            bytes.push_back(std::byte(static_cast<unsigned char>(value)));
        bytes.push_back(std::byte{});
    }
    finish_record(bytes, profile);
    return bytes;
}
Bytes removal(Profile profile, std::uint16_t depth = 0,
              std::uint16_t raw = 0xabcdU) {
    auto bytes = record(profile, 28, 12);
    put(bytes, 8, depth, 2, profile);
    put(bytes, 10, raw, 2, profile);
    return bytes;
}
Bytes definition(Profile profile, std::uint32_t id = 1) {
    auto bytes = record(profile, 2, 12);
    put(bytes, 8, id, 4, profile);
    return bytes;
}
using Frame = std::vector<Bytes>;
Bytes sprite(Profile profile, std::uint32_t id,
             const std::vector<Frame>& frames,
             std::uint16_t capacity = 0x4000) {
    require(!frames.empty() && frames.size() <= 65535,
            "source-owned sprite frame count");
    const auto metadata = 24 + 4 * frames.size();
    auto bytes = record(profile, 39, metadata);
    put(bytes, 8, id, 4, profile);
    put(bytes, 12, static_cast<std::uint32_t>(frames.size()), 2, profile);
    put(bytes, 14, capacity, 2, profile);
    put(bytes, 20, static_cast<std::uint32_t>(metadata), 4, profile);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        for (const auto& item : frames[i])
            append(bytes, item);
        append(bytes, record(profile, 1));
        put(bytes, 24 + 4 * i, static_cast<std::uint32_t>(bytes.size()), 4,
            profile);
    }
    auto ending = record(profile, 0);
    put(ending, 4, id == 0 ? 0U : 8U, 4, profile);
    append(bytes, ending);
    put(bytes, 16, static_cast<std::uint32_t>(bytes.size()), 4, profile);
    return bytes;
}
Bytes wrap(Profile profile, const Bytes& root, std::uint16_t definitions = 2) {
    Bytes bytes(72);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[4] = std::byte{0x51};
    bytes[5] = std::byte{2};
    put(bytes, 6, profile == Profile::pc_2_8 ? 8U : 5U, 2, profile);
    bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    put(bytes, 60, 60, 2, profile);
    put(bytes, 62, definitions, 2, profile);
    // The validated VMF envelope requires header.raw_40 == root.raw_0e.
    // Copy the already serialized WORD before calculating the header CRC.
    bytes[64] = root.at(14);
    bytes[65] = root.at(15);
    auto canonical = bytes;
    canonical.resize(68);
    if (profile == Profile::xbox_2_5)
        for (const auto offset : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[offset], canonical[offset + 1]);
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : canonical) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    put(bytes, 68, crc ^ 0xffffffffU, 4, profile);
    append(bytes, root);
    auto txix = record(profile, 0x54584958U);
    put(txix, 4, 0, 4, profile);
    append(bytes, txix);
    return bytes;
}
VmfMovie parse(Profile profile, const Bytes& bytes) {
    auto result = VmfMovie::parse(bytes, profile);
    if (!result)
        std::cerr << "VMF fixture parse: code="
                  << static_cast<unsigned>(result.error().code)
                  << " offset=" << result.error().offset
                  << " detail=" << result.error().detail << '\n';
    require(result.has_value(), "source-owned VMF structural fixture valid");
    return std::move(*result);
}
VmfMovie single(Profile profile, Bytes item, std::uint16_t capacity = 0x4000) {
    return parse(profile,
                 wrap(profile, sprite(profile, 0,
                                      {{definition(profile), std::move(item)}},
                                      capacity)));
}
VmfDisplayData decode(const VmfMovie& movie) {
    auto result = VmfDisplayData::decode(movie);
    require(result.has_value(), "valid display fixture decodes");
    return std::move(*result);
}

bool same_location(const VmfDisplayLocation& a, const VmfDisplayLocation& b) {
    return a.record_index == b.record_index &&
           a.sprite_index == b.sprite_index &&
           a.global_frame_index == b.global_frame_index &&
           a.frame_index == b.frame_index && a.source_offset == b.source_offset;
}
bool same(const VmfDisplayData& a, const VmfDisplayData& b) {
    if (a.profile() != b.profile() ||
        a.logical_output_bytes() != b.logical_output_bytes() ||
        a.placements().size() != b.placements().size() ||
        a.removals().size() != b.removals().size() ||
        a.events().size() != b.events().size() ||
        a.frames().size() != b.frames().size())
        return false;
    for (std::size_t i = 0; i < a.placements().size(); ++i) {
        const auto& x = a.placements()[i];
        const auto& y = b.placements()[i];
        if (!same_location(x.location, y.location) || x.depth != y.depth ||
            x.flags != y.flags || x.character_id != y.character_id ||
            x.matrix_bits != y.matrix_bits || x.color_words != y.color_words ||
            x.ratio != y.ratio || x.clip_depth != y.clip_depth ||
            x.name != y.name || x.xbox_fixed_payload != y.xbox_fixed_payload)
            return false;
    }
    for (std::size_t i = 0; i < a.removals().size(); ++i) {
        const auto& x = a.removals()[i];
        const auto& y = b.removals()[i];
        if (!same_location(x.location, y.location) || x.depth != y.depth ||
            x.raw_0a != y.raw_0a)
            return false;
    }
    for (std::size_t i = 0; i < a.frames().size(); ++i)
        if (a.frames()[i].first_event != b.frames()[i].first_event ||
            a.frames()[i].event_count != b.frames()[i].event_count)
            return false;
    for (std::size_t i = 0; i < a.events().size(); ++i)
        if (a.events()[i].kind != b.events()[i].kind ||
            a.events()[i].item_index != b.events()[i].item_index)
            return false;
    return true;
}
void accounting(const VmfDisplayData& data) {
    std::size_t name_bytes = 0;
    for (const auto& item : data.placements())
        name_bytes += item.name.size();
    const auto expected = data.placements().size() * sizeof(VmfPlacement) +
                          data.removals().size() * sizeof(VmfRemoval) +
                          data.frames().size() * sizeof(VmfDisplayFrame) +
                          data.events().size() * sizeof(VmfDisplayEvent) +
                          name_bytes;
    check(data.logical_output_bytes() == expected,
          "logical bytes count four collections and names, no input copy/NUL");
}
void placement_fields() {
    for (const auto profile : Profiles) {
        Fields fields;
        fields.flags = profile == Profile::pc_2_8 ? 0x3ffU : 0xffU;
        fields.depth = 0x2345;
        fields.name = std::string(63, 'N');
        for (const auto byte :
             {std::uint8_t{0xff}, std::uint8_t{0x80}, std::uint8_t{0xc0}})
            fields.name.push_back(std::bit_cast<char>(byte));
        const auto raw = placement(profile, fields);
        auto movie = single(profile, raw);
        const auto data = decode(movie);
        require(data.placements().size() == 1, "one full placement");
        const auto& item = data.placements()[0];
        check(data.profile() == profile && data.removals().empty(),
              "profile identity");
        check(item.depth == fields.depth && item.flags == fields.flags,
              "depth and flags are independent endian WORDs");
        check(item.character_id == fields.character,
              "character is active WORD");
        check(item.matrix_bits == std::optional(Matrix),
              "all six matrix bits preserved");
        check(item.color_words == std::optional(Colors),
              "color raw WORDs unsigned storage");
        check(item.ratio == fields.ratio && item.clip_depth == fields.clip,
              "ratio and clip are separate WORDs; clip precedes name");
        check(item.name == fields.name && item.name.size() > 31,
              "full non-UTF8 byte name, no instance truncation");
        check(item.location.record_index == 2 &&
                  item.location.sprite_index == 0 &&
                  item.location.global_frame_index == 0 &&
                  item.location.frame_index == 0 &&
                  item.location.source_offset ==
                      movie.records()[2].source_offset,
              "placement provenance points at source record and global frame");
        if (profile == Profile::xbox_2_5) {
            require(item.xbox_fixed_payload.has_value(),
                    "Xbox fixed prefix retained");
            check(std::equal(item.xbox_fixed_payload->begin(),
                             item.xbox_fixed_payload->end(), raw.begin() + 8),
                  "all52 Xbox bytes retained exactly");
            check(item.clip_depth == 0x3456 && item.clip_depth != 0x789a,
                  "Xbox clip WORD+3A, reserved WORD+38 not substituted");
        } else {
            check(!item.xbox_fixed_payload, "PC has no fabricated Xbox prefix");
        }
        accounting(data);
    }
    for (unsigned subset = 0; subset != 8; ++subset) {
        Fields fields;
        fields.flags = static_cast<std::uint16_t>(
            4U | 2U | ((subset & 1U) ? 0x100U : 0U) |
            ((subset & 2U) ? 0x80U : 0U) | ((subset & 4U) ? 0x200U : 0U));
        auto expected = Identity;
        for (unsigned pair = 0; pair != 3; ++pair)
            if (subset & (1U << pair)) {
                expected[2 * pair] = Matrix[2 * pair];
                expected[2 * pair + 1] = Matrix[2 * pair + 1];
            }
        const auto data =
            decode(single(Profile::pc_2_8, placement(Profile::pc_2_8, fields)));
        const auto& item = data.placements()[0];
        check(item.matrix_bits == std::optional(expected),
              "PC pair100/pair80/pair200 order and missing-pair defaults");
        check(!item.color_words && !item.ratio && !item.clip_depth &&
                  item.name.empty(),
              "inactive PC fields remain absent");
    }
    for (const auto profile : Profiles) {
        for (const auto flags : {0U, 1U, 2U, 4U, 8U, 0x10U, 0x20U, 0x40U}) {
            Fields fields;
            fields.flags = static_cast<std::uint16_t>(flags);
            if (profile == Profile::xbox_2_5 && flags != 4)
                fields.matrix[0] = 0x7fc01234U; // Opaque when matrix inactive.
            if (profile == Profile::xbox_2_5 && flags != 2)
                fields.character = 0xffff; // Opaque when reference inactive.
            const auto data =
                decode(single(profile, placement(profile, fields)));
            const auto& item = data.placements()[0];
            check(item.character_id.has_value() == ((flags & 2U) != 0),
                  "character optional bit2");
            check(item.matrix_bits.has_value() == ((flags & 4U) != 0),
                  "matrix optional bit4");
            check(item.color_words.has_value() == ((flags & 8U) != 0),
                  "color optional bit8");
            check(item.ratio.has_value() == ((flags & 0x10U) != 0),
                  "ratio optional bit10");
            check(item.clip_depth.has_value() == ((flags & 0x40U) != 0),
                  "clip optional bit40");
            check(item.name ==
                      ((profile == Profile::xbox_2_5 || (flags & 0x20U))
                           ? fields.name
                           : std::string{}),
                  "physical Xbox name retained independently of name-update "
                  "flag");
        }
        Fields fields;
        fields.flags = 0x1b; // No matrix: align the8 WORDs after char WORD.
        const auto data = decode(single(profile, placement(profile, fields)));
        check(data.placements()[0].color_words == std::optional(Colors) &&
                  data.placements()[0].ratio == fields.ratio,
              "color alignment without matrix and following WORD ratio");
        const auto removed =
            decode(single(profile, removal(profile, 0x3210, 0xabcd)));
        require(removed.removals().size() == 1, "one removal");
        check(removed.removals()[0].depth == 0x3210 &&
                  removed.removals()[0].raw_0a == 0xabcd,
              "removal uses two WORDs, not an endian DWORD");
        accounting(removed);
    }
}

void event_order() {
    for (const auto profile : Profiles) {
        Fields outer;
        outer.flags = 0x22;
        outer.depth = 0;
        outer.name = "root-first";
        Fields inner = outer;
        inner.name = "child";
        const auto child = sprite(
            profile, 2,
            {{placement(profile, inner)}, {removal(profile, 1, 0x2345)}});
        outer.name = "root-second";
        const auto bytes =
            wrap(profile,
                 sprite(profile, 0,
                        {{definition(profile), placement(profile, inner), child,
                          removal(profile, 2, 0x7654)},
                         {placement(profile, outer)},
                         {}}),
                 3);
        const auto movie = parse(profile, bytes);
        const auto data = decode(movie);
        require(data.frames().size() == 5 && data.events().size() == 5,
                "all root/child frames retained including empty root frame");
        check(data.placements().size() == 3 && data.removals().size() == 2,
              "nested records are neither duplicated nor lost");
        // Physical record order: root,def,P,child,P,show,R,show,end,R,show,P,
        // show,show,end. Global frames reserve the three root frames first.
        const std::array<std::vector<std::size_t>, 5> expected{
            {{2, 9}, {11}, {}, {4}, {6}}};
        std::size_t consumed = 0;
        for (std::size_t frame = 0; frame < expected.size(); ++frame) {
            const auto events = data.events_for_frame(frame);
            require(events.has_value(), "frame event getter valid");
            check(events->size() == expected[frame].size() &&
                      data.frames()[frame].first_event == consumed &&
                      data.frames()[frame].event_count == events->size(),
                  "frame slices are contiguous in global frame order");
            for (std::size_t i = 0; i < events->size(); ++i) {
                const auto event = (*events)[i];
                const auto& location =
                    event.kind == VmfDisplayEventKind::place
                        ? data.placements()[event.item_index].location
                        : data.removals()[event.item_index].location;
                check(
                    location.record_index == expected[frame][i] &&
                        location.global_frame_index == frame,
                    "events preserve original record order inside each frame");
                const auto& input_frame = movie.frames()[frame];
                check(location.sprite_index == input_frame.sprite_index &&
                          location.frame_index == input_frame.index &&
                          location.source_offset ==
                              movie.records()[location.record_index]
                                  .source_offset,
                      "event location uses input indices, not physical-frame "
                      "guessing");
            }
            consumed += events->size();
        }
        for (const auto index :
             {data.frames().size(), std::numeric_limits<std::size_t>::max()}) {
            const auto bad = data.events_for_frame(index);
            check(!bad && bad.error().code == Code::invalid_frame &&
                      !bad.error().detail.empty(),
                  "out-of-range frame getter returns static error");
        }
        accounting(data);
    }
}

void rejection(const VmfMovie& movie, Code code, std::string_view message) {
    const auto result = VmfDisplayData::decode(movie);
    check(!result && result.error().code == code &&
              !result.error().detail.empty(),
          message);
    if (!result)
        check(result.error().offset <= movie.source_bytes().size(),
              "diagnostic source offset remains in input range");
}
void invalid_fields() {
    for (const auto profile : Profiles) {
        Fields fields;
        fields.flags = 2;
        fields.character = 2;
        rejection(single(profile, placement(profile, fields)),
                  Code::invalid_reference,
                  "active character must exist in dense definition table");
        fields.character = 1;
        fields.depth = 0x4000;
        rejection(single(profile, placement(profile, fields)),
                  Code::invalid_reference,
                  "placement depth equal to capacity rejected");
        rejection(single(profile, removal(profile, 0x4000)),
                  Code::invalid_reference,
                  "removal depth equal to capacity rejected");
        fields.depth = 0;
        const auto allowed =
            decode(single(profile, placement(profile, fields), 1));
        check(allowed.placements()[0].depth == 0,
              "zero-based depth zero allowed");
        rejection(single(profile, placement(profile, fields), 0),
                  Code::invalid_reference,
                  "zero parent capacity admits no placements");
        fields.flags = profile == Profile::pc_2_8 ? 0x404U : 0x104U;
        rejection(single(profile, placement(profile, fields)),
                  Code::unsupported_layout, "unknown profile flags rejected");
        fields.flags = profile == Profile::pc_2_8 ? 0x104U : 4U;
        for (const auto nonfinite :
             {0x7f800000U, 0xff800000U, 0x7fc00001U, 0x7f800001U}) {
            fields.matrix[0] = nonfinite;
            rejection(single(profile, placement(profile, fields)),
                      Code::invalid_numeric,
                      "active matrix exponentFF rejected without arithmetic");
        }
        fields.matrix = Matrix;
        auto short_remove = removal(profile);
        short_remove.resize(8);
        put(short_remove, 4, 8, 4, profile);
        rejection(single(profile, short_remove), Code::truncated,
                  "short removal rejected");
        auto long_remove = removal(profile);
        long_remove.resize(16);
        put(long_remove, 4, 16, 4, profile);
        rejection(single(profile, long_remove), Code::unsupported_layout,
                  "extra removal tail rejected");
        fields.flags = 0x20;
        fields.name = "xx";
        auto raw = placement(profile, fields);
        raw.back() = std::byte{0x6b};
        rejection(single(profile, raw), Code::unsupported_layout,
                  "nonzero final alignment rejected");
        fields.name = "abc";
        raw = placement(profile, fields);
        raw.back() = std::byte{0xff};
        rejection(single(profile, raw), Code::truncated,
                  "NUL absent before record end rejected");
        raw = placement(profile, fields);
        raw.resize(raw.size() + 4);
        put(raw, 4, static_cast<std::uint32_t>(raw.size()), 4, profile);
        rejection(single(profile, raw), Code::unsupported_layout,
                  "extra zero DWORD exceeds align4 tail");
        auto short_place =
            record(profile, 26, profile == Profile::pc_2_8 ? 8 : 56);
        rejection(single(profile, short_place), Code::truncated,
                  "truncated placement fixed fields rejected");
        if (profile == Profile::pc_2_8) {
            fields.flags = 0x100;
            rejection(
                single(profile, placement(profile, fields)),
                Code::unsupported_layout,
                "pair bits without matrix flag are outside observed layout");
            fields.flags = 6;
            fields.name.clear();
            raw = placement(profile, fields);
            raw[14] = std::byte{1}; // char + alignment before empty matrix.
            rejection(single(profile, raw), Code::unsupported_layout,
                      "nonzero internal alignment rejected");
            fields.flags = 0x3fc;
            raw = placement(profile, fields);
            for (const auto size : {12U, 16U, 20U, 24U, 28U}) {
                auto cut = raw;
                cut.resize(size);
                put(cut, 4, size, 4, profile);
                rejection(
                    single(profile, cut), Code::truncated,
                    "each active matrix pair/color read stays inside record");
            }
        }
    }
}

VmfMovie populated(Profile profile, char fill, unsigned count = 4) {
    Frame frame{definition(profile)};
    for (unsigned i = 0; i < count; ++i) {
        Fields fields;
        fields.flags = profile == Profile::pc_2_8 ? 0x3ffU : 0xffU;
        fields.depth = static_cast<std::uint16_t>(i);
        fields.name = std::string(80 + i, fill);
        frame.push_back(placement(profile, fields));
        frame.push_back(removal(profile, static_cast<std::uint16_t>(i),
                                static_cast<std::uint16_t>(0x8000U + i)));
    }
    return parse(profile, wrap(profile, sprite(profile, 0, {frame, {}})));
}
void quota_refusal(const VmfMovie& movie, VmfDisplayLimits limits) {
    std::optional<Result> result;
    std::size_t calls = 0;
    bool threw = false;
    {
        display_allocation_fixture::Scope watch(0);
        try {
            result.emplace(VmfDisplayData::decode(movie, limits));
        } catch (...) {
            threw = true;
        }
        calls = watch.count();
    }
    check(!threw && result && !*result &&
              result->error().code == Code::resource_limit &&
              !result->error().detail.empty(),
          "quota returns static resource_limit");
    check(calls == 0, "entire quota preflight happens before first allocation");
}
void quotas_and_faults() {
    const auto source_movie = populated(Profile::pc_2_8, 'A');
    const auto other_movie = populated(Profile::xbox_2_5, 'B', 2);
    const auto source = decode(source_movie);
    const auto other = decode(other_movie);
    VmfDisplayLimits exact;
    exact.max_input_bytes = source_movie.source_bytes().size();
    exact.max_placements = source.placements().size();
    exact.max_removals = source.removals().size();
    exact.max_frames = source.frames().size();
    exact.max_events = source.events().size();
    exact.max_name_bytes = 83;
    exact.max_total_name_bytes = 80 + 81 + 82 + 83;
    exact.max_output_bytes = source.logical_output_bytes();
    const auto equal_limit = VmfDisplayData::decode(source_movie, exact);
    check(equal_limit && same(*equal_limit, source),
          "all quotas accept exact boundaries excluding NUL");
    for (const auto member :
         {&VmfDisplayLimits::max_input_bytes, &VmfDisplayLimits::max_placements,
          &VmfDisplayLimits::max_removals, &VmfDisplayLimits::max_frames,
          &VmfDisplayLimits::max_events, &VmfDisplayLimits::max_name_bytes,
          &VmfDisplayLimits::max_total_name_bytes,
          &VmfDisplayLimits::max_output_bytes}) {
        auto limits = exact;
        --(limits.*member);
        quota_refusal(source_movie, limits);
    }
    VmfDisplayLimits maximum;
    maximum.max_input_bytes = maximum.max_placements = maximum.max_removals =
        maximum.max_frames = maximum.max_events = maximum.max_name_bytes =
            maximum.max_total_name_bytes = maximum.max_output_bytes =
                std::numeric_limits<std::size_t>::max();
    const auto huge_limits = VmfDisplayData::decode(source_movie, maximum);
    check(huge_limits && same(*huge_limits, source),
          "SIZE_MAX quotas do not overflow or allocate proportional to quota");
    std::size_t allocations = 0;
    {
        display_allocation_fixture::Scope watch;
        const auto result = VmfDisplayData::decode(source_movie);
        allocations = watch.count();
        check(result.has_value(), "allocation baseline decode succeeds");
    }
    require(allocations > 0 && allocations < 128,
            "bounded meaningful decoder fault sweep");
    for (std::size_t failure = 0; failure < allocations; ++failure) {
        std::optional<Result> result;
        bool threw = false;
        {
            display_allocation_fixture::Scope fail(failure);
            try {
                result.emplace(VmfDisplayData::decode(source_movie));
            } catch (...) {
                threw = true;
            }
        }
        check(!threw && result && !*result &&
                  result->error().code == Code::resource_limit &&
                  !result->error().detail.empty(),
              "every decode OOM returns static error");
        ++decode_faults;
        const auto recovered = VmfDisplayData::decode(source_movie);
        check(recovered && same(*recovered, source),
              "decode after OOM remains independent");
    }
    std::size_t copy_allocations = 0;
    {
        auto target = other;
        display_allocation_fixture::Scope watch;
        target = source;
        copy_allocations = watch.count();
        check(same(target, source),
              "copy baseline transfers all values and profile");
    }
    require(copy_allocations > 0 && copy_allocations < 128,
            "meaningful copy allocation sweep");
    for (std::size_t failure = 0; failure < copy_allocations; ++failure) {
        auto target = other;
        bool bad_alloc = false;
        {
            display_allocation_fixture::Scope fail(failure);
            try {
                target = source;
            } catch (const std::bad_alloc&) {
                bad_alloc = true;
            }
        }
        check(bad_alloc && same(target, other),
              "copy OOM preserves distinct destination B entirely");
        check(same(source, *equal_limit),
              "copy OOM leaves distinct source A unchanged");
        target = source;
        check(same(target, source), "copy retry after OOM succeeds");
        ++copy_faults;
    }
    auto copied = source;
    check(copied.placements().data() != source.placements().data() &&
              copied.placements()[0].name.data() !=
                  source.placements()[0].name.data(),
          "copy owns vectors and long names independently");
    const auto* alias = &copied;
    copied = *alias;
    check(same(copied, source), "self-copy assignment is valid");
    const auto lifetime = [] {
        auto temporary_movie = populated(Profile::xbox_2_5, 'Z');
        auto temporary_data = decode(temporary_movie);
        return VmfDisplayData(temporary_data);
    }();
    check(lifetime.profile() == Profile::xbox_2_5 &&
              lifetime.placements()[0].name == std::string(80, 'Z') &&
              lifetime.placements()[0].xbox_fixed_payload.has_value(),
          "result outlives source movie and copied decoder result");
    auto moved = copied;
    auto final = std::move(moved);
    check(same(final, source),
          "move owns all values without rebasing source indices");
    auto empty_movie =
        parse(Profile::pc_2_8,
              wrap(Profile::pc_2_8, sprite(Profile::pc_2_8, 0, {{}, {}}), 1));
    VmfDisplayLimits no_items;
    no_items.max_placements = no_items.max_removals = no_items.max_events =
        no_items.max_total_name_bytes = no_items.max_name_bytes = 0;
    const auto empty = VmfDisplayData::decode(empty_movie, no_items);
    check(
        empty && empty->placements().empty() && empty->removals().empty() &&
            empty->events().empty() && empty->frames().size() == 2,
        "zero placement/removal/name quotas accept structurally empty frames");
}

Bytes read_file(const std::filesystem::path& path, std::size_t limit) {
    const auto size = std::filesystem::file_size(path);
    require(size <= limit, "fixture file exceeds its explicit byte limit");
    Bytes bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "fixture file opens read-only");
    if (!bytes.empty())
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(input) &&
                input.peek() == std::char_traits<char>::eof(),
            "fixture file exact-size read");
    return bytes;
}

// Test-only SHA256 over already bounded buffers: explicit word serialization,
// no operating-system, third-party or production decoder dependency.
std::array<std::byte, 32> sha256(std::span<const std::byte> source) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                       0xa54ff53a, 0x510e527f, 0x9b05688c,
                                       0x1f83d9ab, 0x5be0cd19};
    const auto block = [&](std::span<const std::byte, 64> bytes) {
        std::array<std::uint32_t, 64> words{};
        for (unsigned i = 0; i < 16; ++i)
            for (unsigned j = 0; j < 4; ++j)
                words[i] = (words[i] << 8) |
                           std::to_integer<std::uint32_t>(bytes[4 * i + j]);
        for (unsigned i = 16; i < 64; ++i) {
            const auto x = words[i - 15], y = words[i - 2];
            words[i] = words[i - 16] +
                       (std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3)) +
                       words[i - 7] +
                       (std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10));
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (unsigned i = 0; i < 64; ++i) {
            const auto first =
                h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                ((e & f) ^ (~e & g)) + constants[i] + words[i];
            const auto second =
                (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
                ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        const std::array working{a, b, c, d, e, f, g, h};
        for (unsigned i = 0; i < 8; ++i)
            state[i] += working[i];
    };
    const auto original_size = source.size();
    while (source.size() >= 64) {
        block(std::span<const std::byte, 64>(source.data(), 64));
        source = source.subspan(64);
    }
    std::array<std::byte, 128> tail{};
    std::copy(source.begin(), source.end(), tail.begin());
    tail[source.size()] = std::byte{0x80};
    const std::size_t length = source.size() < 56 ? 64 : 128;
    const auto bits = static_cast<std::uint64_t>(original_size) * 8;
    for (unsigned i = 0; i < 8; ++i)
        tail[length - 1 - i] = std::byte((bits >> (8 * i)) & 255U);
    block(std::span<const std::byte, 64>(tail.data(), 64));
    if (length == 128)
        block(std::span<const std::byte, 64>(tail.data() + 64, 64));
    std::array<std::byte, 32> result{};
    for (unsigned i = 0; i < 32; ++i)
        result[i] = std::byte((state[i / 4] >> (8 * (3 - i % 4))) & 255U);
    return result;
}
std::string hex(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(2 * bytes.size());
    for (const auto value : bytes) {
        const auto byte = std::to_integer<unsigned>(value);
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}
std::size_t decimal(std::string_view text) {
    std::size_t value{};
    const auto [end, code] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    require(code == std::errc{} && end == text.data() + text.size() &&
                !text.empty(),
            "manifest unsigned decimal field");
    return value;
}
std::uint32_t le(std::span<const std::byte> bytes, std::size_t offset,
                 std::size_t count) {
    require(offset <= bytes.size() && count <= bytes.size() - offset &&
                count <= 4,
            "oracle field is inside its admitted region");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < count; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}
struct OracleReader {
    std::span<const std::byte> bytes;
    std::size_t cursor{};
    std::span<const std::byte> take(std::size_t count) {
        require(cursor <= bytes.size() && count <= bytes.size() - cursor,
                "oracle section count and length are bounded");
        const auto result = bytes.subspan(cursor, count);
        cursor += count;
        return result;
    }
};
std::size_t record_at(const VmfMovie& movie, std::uint32_t offset) {
    const auto records = movie.records();
    const auto found =
        std::lower_bound(records.begin(), records.end(), offset,
                         [](const VmfRecord& item, std::uint32_t source) {
                             return item.source_offset < source;
                         });
    require(found != records.end() && found->source_offset == offset,
            "oracle source offset resolves to a structural record");
    return static_cast<std::size_t>(found - records.begin());
}
std::size_t parent_at(const VmfMovie& movie, std::uint32_t offset,
                      std::uint32_t id) {
    const auto sprites = movie.sprites();
    const auto found = std::lower_bound(
        sprites.begin(), sprites.end(), offset,
        [&](const VmfSprite& item, std::uint32_t source) {
            return movie.records()[item.record_index].source_offset < source;
        });
    require(found != sprites.end() && found->character_id == id &&
                movie.records()[found->record_index].source_offset == offset,
            "oracle parent joins by source offset AND character ID");
    return static_cast<std::size_t>(found - sprites.begin());
}
void oracle_location(const VmfDisplayLocation& actual, const VmfMovie& movie,
                     std::span<const std::byte> common, std::uint32_t kind) {
    const auto offset = le(common, 4, 4);
    const auto ri = record_at(movie, offset);
    const auto si = parent_at(movie, le(common, 12, 4), le(common, 16, 4));
    const auto local = le(common, 20, 2);
    const auto& sprite = movie.sprites()[si];
    require(local < sprite.frame_count &&
                le(common, 0, 4) < movie.records().size(),
            "oracle local frame and source ordinal bounds");
    const auto global = sprite.first_frame + local;
    check(movie.records()[ri].type == kind &&
              movie.records()[ri].declared_size == le(common, 8, 4) &&
              movie.records()[ri].parent_sprite_index == si &&
              actual.record_index == ri && actual.sprite_index == si &&
              actual.global_frame_index == global &&
              actual.frame_index == local && actual.source_offset == offset,
          "typed location agrees with independent source/frame join");
}
void compare_oracle(const VmfMovie& movie, const VmfDisplayData& data,
                    const Bytes& oracle,
                    const std::array<std::size_t, 6>& counts) {
    OracleReader reader{oracle};
    const auto header = reader.take(72);
    constexpr std::array magic{std::byte{'V'}, std::byte{'M'}, std::byte{'F'},
                               std::byte{'D'}, std::byte{'S'}, std::byte{'P'},
                               std::byte{'1'}, std::byte{}};
    require(std::equal(magic.begin(), magic.end(), header.begin()),
            "oracle magic v1");
    const auto source_digest = sha256(movie.source_bytes());
    require(le(header, 8, 4) == (data.profile() == Profile::pc_2_8 ? 0U : 1U) &&
                le(header, 12, 4) == movie.source_bytes().size() &&
                std::equal(source_digest.begin(), source_digest.end(),
                           header.begin() + 40),
            "oracle authenticates profile, source size and SHA256");
    for (std::size_t i = 0; i < 6; ++i)
        require(le(header, 16 + 4 * i, 4) == counts[i],
                "oracle header matches manifest counts");
    require(counts[0] == movie.records().size() &&
                counts[1] == movie.sprites().size() &&
                counts[2] == movie.frames().size() &&
                counts[3] == data.placements().size() &&
                counts[4] == data.removals().size() &&
                counts[5] == data.events().size() && counts[3] <= counts[5] &&
                counts[4] == counts[5] - counts[3],
            "independent complete collection counts agree");
    for (std::size_t i = 0; i < counts[1]; ++i) {
        const auto row = reader.take(20);
        const auto si = parent_at(movie, le(row, 4, 4), le(row, 8, 4));
        const auto& sprite = movie.sprites()[si];
        check(sprite.raw_0e == le(row, 16, 2) &&
                  sprite.frame_count == le(row, 18, 2) &&
                  le(row, 0, 4) < counts[0] && le(row, 12, 4) <= counts[2] &&
                  sprite.frame_count <= counts[2] - le(row, 12, 4),
              "oracle sprite capacities and frame extents agree");
    }
    std::vector<std::uint32_t> placement_offsets, removal_offsets;
    placement_offsets.reserve(counts[3]);
    removal_offsets.reserve(counts[4]);
    for (std::size_t i = 0; i < counts[3]; ++i) {
        const auto row = reader.take(80);
        const auto name_bytes = reader.take(le(row, 34, 2));
        const auto fixed_bytes = reader.take(le(row, 76, 1));
        const auto& actual = data.placements()[i];
        oracle_location(actual.location, movie, row.first(28), 26);
        require(le(row, 26, 2) == 0 && le(row, 77, 1) == 0 &&
                    le(row, 78, 2) == 0,
                "oracle placement reserved bytes are zero");
        const auto flags = le(row, 24, 2);
        check(actual.depth == le(row, 22, 2) && actual.flags == flags,
              "corpus depth and raw flags agree exactly");
        check(actual.character_id.has_value() == ((flags & 2U) != 0) &&
                  (!actual.character_id ||
                   *actual.character_id == le(row, 28, 2)),
              "corpus active character WORD agrees");
        check(actual.matrix_bits.has_value() == ((flags & 4U) != 0),
              "corpus matrix presence");
        if (actual.matrix_bits)
            for (std::size_t component = 0; component < 6; ++component)
                check((*actual.matrix_bits)[component] ==
                          le(row, 36 + 4 * component, 4),
                      "corpus exact matrix bits agree");
        check(actual.color_words.has_value() == ((flags & 8U) != 0),
              "corpus color presence");
        if (actual.color_words)
            for (std::size_t component = 0; component < 8; ++component)
                check((*actual.color_words)[component] ==
                          le(row, 60 + 2 * component, 2),
                      "corpus exact color WORD agrees");
        check(actual.ratio.has_value() == ((flags & 0x10U) != 0) &&
                  (!actual.ratio || *actual.ratio == le(row, 30, 2)),
              "corpus ratio WORD");
        check(actual.clip_depth.has_value() == ((flags & 0x40U) != 0) &&
                  (!actual.clip_depth || *actual.clip_depth == le(row, 32, 2)),
              "corpus clip WORD");
        const std::string_view expected_name(
            reinterpret_cast<const char*>(name_bytes.data()),
            name_bytes.size());
        check(actual.name == expected_name, "corpus name exact full bytes");
        if (data.profile() == Profile::xbox_2_5) {
            require(fixed_bytes.size() == 52, "oracle Xbox fixed52 length");
            check(actual.xbox_fixed_payload &&
                      std::equal(fixed_bytes.begin(), fixed_bytes.end(),
                                 actual.xbox_fixed_payload->begin()),
                  "corpus Xbox fixed52 retained without optional "
                  "reinterpretation");
        } else {
            check(fixed_bytes.empty() && !actual.xbox_fixed_payload,
                  "corpus PC has no Xbox fixed data");
        }
        placement_offsets.push_back(le(row, 4, 4));
        if (i)
            check(placement_offsets[i - 1] < placement_offsets[i],
                  "placement order follows source walk");
    }
    for (std::size_t i = 0; i < counts[4]; ++i) {
        const auto row = reader.take(28);
        const auto& actual = data.removals()[i];
        oracle_location(actual.location, movie, row, 28);
        check(actual.depth == le(row, 22, 2) &&
                  actual.raw_0a == le(row, 24, 2) && le(row, 26, 2) == 0,
              "corpus removal WORDs agree");
        removal_offsets.push_back(le(row, 4, 4));
        if (i)
            check(removal_offsets[i - 1] < removal_offsets[i],
                  "removal order follows source walk");
    }
    struct FrameOracle {
        std::size_t global, first, count;
    };
    std::vector<FrameOracle> frames;
    frames.reserve(counts[2]);
    std::vector<bool> visited(counts[2]);
    std::size_t first_event = 0;
    for (std::size_t i = 0; i < counts[2]; ++i) {
        const auto row = reader.take(32);
        const auto si = parent_at(movie, le(row, 0, 4), le(row, 4, 4));
        const auto& sprite = movie.sprites()[si];
        require(le(row, 8, 2) < sprite.frame_count, "oracle frame local index");
        const auto global = sprite.first_frame + le(row, 8, 2);
        require(global < visited.size() && !visited[global],
                "oracle frames cover actual frames once");
        visited[global] = true;
        const auto& actual = movie.frames()[global];
        check(
            le(row, 10, 2) == 0 && actual.begin_offset == le(row, 12, 4) &&
                actual.end_offset == le(row, 16, 4) &&
                movie.records()[actual.show_frame_record_index].source_offset ==
                    le(row, 20, 4),
            "independent direct-frame source boundaries agree");
        const auto first = le(row, 24, 4), count = le(row, 28, 4);
        require(first == first_event && first <= counts[5] &&
                    count <= counts[5] - first,
                "oracle frame event slices are bounded and contiguous");
        frames.push_back({global, first, count});
        first_event += count;
    }
    require(first_event == counts[5],
            "oracle frame slices account for every event");
    const auto events = reader.take(counts[5] * 8);
    require(reader.cursor == oracle.size(),
            "oracle exact EOF, no trailing data");
    for (const auto& frame : frames) {
        const auto actual = data.events_for_frame(frame.global);
        require(actual && actual->size() == frame.count,
                "corpus exact per-frame event count");
        for (std::size_t i = 0; i < frame.count; ++i) {
            const auto row = events.subspan((frame.first + i) * 8, 8);
            const auto kind = le(row, 0, 1), index = le(row, 4, 4);
            require(kind <= 1 && le(row, 1, 1) == 0 && le(row, 2, 2) == 0,
                    "oracle event kind/reserved bounds");
            const auto& expected_offsets =
                kind == 0 ? placement_offsets : removal_offsets;
            require(index < expected_offsets.size(),
                    "oracle event item index bounds");
            const auto event = (*actual)[i];
            const auto want_kind = kind == 0 ? VmfDisplayEventKind::place
                                             : VmfDisplayEventKind::remove;
            require(event.kind == want_kind &&
                        event.item_index < (kind == 0 ? data.placements().size()
                                                      : data.removals().size()),
                    "typed event kind and collection index match");
            const auto& location =
                kind == 0 ? data.placements()[event.item_index].location
                          : data.removals()[event.item_index].location;
            check(location.source_offset == expected_offsets[index] &&
                      location.global_frame_index == frame.global,
                  "per-frame event order matches independent source offsets");
        }
    }
    std::size_t cursor = 0;
    for (const auto& frame : data.frames()) {
        require(frame.first_event == cursor && cursor <= data.events().size() &&
                    frame.event_count <= data.events().size() - cursor,
                "actual global-frame slices are bounded and contiguous");
        cursor += frame.event_count;
    }
    check(cursor == data.events().size(),
          "actual slices use every event exactly once");
    accounting(data);
}
void corpus(const std::filesystem::path& manifest) {
    const auto manifest_bytes = read_file(manifest, 1024U * 1024U);
    const std::string text(reinterpret_cast<const char*>(manifest_bytes.data()),
                           manifest_bytes.size());
    const auto first_end = text.find('\n');
    require(first_end != std::string::npos, "manifest has its header line");
    auto first_line = std::string_view(text).substr(0, first_end);
    if (!first_line.empty() && first_line.back() == '\r')
        first_line.remove_suffix(1);
    require(first_line == "VMF_DISPLAY_ORACLES\t1", "manifest v1 header");
    std::size_t cursor = first_end + 1, source_total = 0, oracle_total = 0;
    std::size_t places = 0, removals = 0, frames = 0, events = 0;
    while (cursor < text.size()) {
        const auto end = text.find('\n', cursor);
        auto line = std::string_view(text).substr(
            cursor,
            end == std::string::npos ? text.size() - cursor : end - cursor);
        cursor = end == std::string::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty() || line.front() == '#')
            continue;
        require(corpus_movies < 39, "manifest movie count bounded");
        std::array<std::string_view, 14> columns{};
        for (std::size_t i = 0; i < columns.size(); ++i) {
            const auto tab = line.find('\t');
            require((i + 1 == columns.size()) ==
                        (tab == std::string_view::npos),
                    "manifest exact14 columns");
            columns[i] = line.substr(0, tab);
            if (tab != std::string_view::npos)
                line.remove_prefix(tab + 1);
        }
        require(columns[0] == "pc_2_8" || columns[0] == "xbox_2_5",
                "manifest explicit profile");
        const auto profile =
            columns[0] == "pc_2_8" ? Profile::pc_2_8 : Profile::xbox_2_5;
        require(columns[5].find_first_of("/\\:") == std::string_view::npos &&
                    columns[5] != "." && columns[5] != "..",
                "oracle is a local manifest basename");
        const auto source =
            read_file(std::filesystem::path(columns[2]), 64U * 1024U * 1024U);
        const auto oracle = read_file(manifest.parent_path() /
                                          std::filesystem::path(columns[5]),
                                      64U * 1024U * 1024U);
        require(source.size() == decimal(columns[3]) &&
                    hex(sha256(source)) == columns[4] &&
                    oracle.size() == decimal(columns[6]) &&
                    hex(sha256(oracle)) == columns[7],
                "manifest source and oracle sizes/SHA256 authenticate frozen "
                "buffers");
        require(source.size() <= 32U * 1024U * 1024U - source_total &&
                    oracle.size() <= 64U * 1024U * 1024U - oracle_total,
                "entire corpus has bounded input/oracle totals");
        source_total += source.size();
        oracle_total += oracle.size();
        std::array<std::size_t, 6> counts{};
        constexpr std::array<std::size_t, 6> caps{250000, 16384,  250000,
                                                  500000, 100000, 600000};
        for (std::size_t i = 0; i < counts.size(); ++i) {
            counts[i] = decimal(columns[8 + i]);
            require(counts[i] <= caps[i],
                    "oracle count cap before count-based allocations");
        }
        const auto movie = parse(profile, source);
        const auto data = decode(movie);
        compare_oracle(movie, data, oracle, counts);
        places += counts[3];
        removals += counts[4];
        frames += counts[2];
        events += counts[5];
        ++corpus_movies;
    }
    check(corpus_movies == 39 && places == 277151 && removals == 27866 &&
              frames == 132788 && events == 305017,
          "all39 frozen independent corpus movies and totals are exercised");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(hex(sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b9"
                                 "34ca495991b7852b855",
              "test-only SHA256 empty known answer");
        const std::array abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        check(hex(sha256(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a39617"
                                  "7a9cb410ff61f20015ad",
              "test-only SHA256 abc known answer");
        placement_fields();
        event_order();
        invalid_fields();
        quotas_and_faults();
        if (argc == 3 && std::string_view(argv[1]) == "--corpus")
            corpus(argv[2]);
        else if (argc != 1)
            throw std::runtime_error(
                "usage: vmf_display_data_test [--corpus manifest.tsv]");
    } catch (const std::exception& error) {
        check(false, error.what());
    }
    std::cout << "VMF display: " << checks << " checks, " << failures
              << " failures, " << decode_faults << " decode OOM, "
              << copy_faults << " copy OOM, " << corpus_movies
              << " corpus movies\n";
    return failures == 0 ? 0 : 1;
}

#include "tdur/ui/vmf_execution.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Envelope adopted from the independently frozen runtime test, SHA
// 9c72eaaa4fa1ff26769b0cf2f928dab556c64cc95213c9d2b02a7158ab67d836.
// New action expectations: vmf-timeline-actions-tests/cases-v1.json, fixed
// before the corresponding production delta. No production CPP was read.
namespace timeline_allocation_fixture {
struct State {
    bool enabled{};
    std::size_t calls{};
    std::size_t fail_at = std::numeric_limits<std::size_t>::max();
};
thread_local State state;
void* allocate(std::size_t bytes) {
    if (state.enabled && state.calls++ == state.fail_at) {
        throw std::bad_alloc();
    }
    if (void* value = std::malloc(bytes == 0 ? 1 : bytes)) {
        return value;
    }
    throw std::bad_alloc();
}
} // namespace timeline_allocation_fixture

void* operator new(std::size_t bytes) {
    return timeline_allocation_fixture::allocate(bytes);
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::assets;
using namespace tdur::ui;
using Bytes = std::vector<std::byte>;
using Profile = VmfProfile;
using RuntimeError = VmfDisplayRuntimeErrorCode;

std::size_t checks{};
std::size_t failures{};

void check(bool passed, std::string_view label) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 48) {
            std::cerr << "FAIL: " << label << '\n';
        }
    }
}

template <class T, class E>
T take(std::expected<T, E> result, std::string_view label) {
    check(result.has_value(), label);
    if (!result) {
        std::cerr << "  error " << static_cast<int>(result.error().code) << ": "
                  << result.error().detail << '\n';
        throw std::runtime_error("required fixture/operation failed");
    }
    return std::move(*result);
}

[[maybe_unused]] void
succeeds(std::expected<void, VmfDisplayRuntimeError> result,
         std::string_view label) {
    check(result.has_value(), label);
    if (!result) {
        std::cerr << "  error " << static_cast<int>(result.error().code) << ": "
                  << result.error().detail << '\n';
        throw std::runtime_error("required runtime operation failed");
    }
}

class AllocationFailureScope {
  public:
    explicit AllocationFailureScope(std::size_t ordinal) noexcept
        : previous_(timeline_allocation_fixture::state) {
        timeline_allocation_fixture::state = {true, 0, ordinal};
    }
    ~AllocationFailureScope() noexcept {
        timeline_allocation_fixture::state = previous_;
    }
    AllocationFailureScope(const AllocationFailureScope&) = delete;
    AllocationFailureScope& operator=(const AllocationFailureScope&) = delete;

  private:
    timeline_allocation_fixture::State previous_;
};

template <class T>
concept RvalueMovie = requires(T&& value) { std::move(value).movie(); };
template <class T>
concept RvalueDisplay = requires(T&& value) { std::move(value).display(); };
static_assert(!RvalueMovie<VmfProgram> && !RvalueMovie<const VmfProgram>);
static_assert(!RvalueDisplay<VmfProgram> && !RvalueDisplay<const VmfProgram>);
static_assert(!std::is_copy_constructible_v<VmfProgram>);
static_assert(!std::is_move_constructible_v<VmfProgram>);
static_assert(!std::is_default_constructible_v<VmfProgram>);
static_assert(!std::is_copy_constructible_v<VmfDisplayRuntime>);
static_assert(!std::is_move_constructible_v<VmfDisplayRuntime>);

constexpr std::array profiles{Profile::pc_2_8, Profile::xbox_2_5};
constexpr std::array<std::uint32_t, 6> identity_matrix{
    0x3f800000U, 0x3f800000U, 0, 0, 0, 0};
constexpr std::array<std::uint16_t, 8> identity_color{256, 256, 256, 256,
                                                      0,   0,   0,   0};

std::array<std::uint32_t, 6> matrix(float a, float d, float b, float c,
                                    float tx, float ty) {
    return {std::bit_cast<std::uint32_t>(a),  std::bit_cast<std::uint32_t>(d),
            std::bit_cast<std::uint32_t>(b),  std::bit_cast<std::uint32_t>(c),
            std::bit_cast<std::uint32_t>(tx), std::bit_cast<std::uint32_t>(ty)};
}

std::byte octet(std::uint32_t value) {
    return static_cast<std::byte>(value & 255U);
}
std::uint32_t narrow(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("fixture too large");
    }
    return static_cast<std::uint32_t>(value);
}
void put(Bytes& bytes, std::size_t at, std::uint32_t value, std::size_t width,
         Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - 1 - i;
        bytes.at(at + i) = octet(value >> (8U * static_cast<unsigned>(lane)));
    }
}
void word(Bytes& bytes, std::size_t at, std::uint16_t value, Profile profile) {
    put(bytes, at, value, 2, profile);
}
void dword(Bytes& bytes, std::size_t at, std::uint32_t value, Profile profile) {
    put(bytes, at, value, 4, profile);
}
void append(Bytes& bytes, std::span<const std::byte> suffix) {
    bytes.insert(bytes.end(), suffix.begin(), suffix.end());
}
void align4(Bytes& bytes) {
    while (bytes.size() % 4 != 0) {
        bytes.push_back(std::byte{});
    }
}
void append_word(Bytes& bytes, std::uint16_t value, Profile profile) {
    const auto at = bytes.size();
    bytes.resize(at + 2);
    word(bytes, at, value, profile);
}
void append_dword(Bytes& bytes, std::uint32_t value, Profile profile) {
    const auto at = bytes.size();
    bytes.resize(at + 4);
    dword(bytes, at, value, profile);
}

struct Marker {
    std::string_view label;
    std::size_t offset{};
};
struct Element {
    Bytes bytes;
    std::vector<Marker> markers;
};

Element tag(Profile profile, std::uint32_t type, Bytes payload = {}) {
    Element out{Bytes(8), {}};
    append(out.bytes, payload);
    align4(out.bytes);
    dword(out.bytes, 0, type, profile);
    dword(out.bytes, 4, narrow(out.bytes.size()), profile);
    return out;
}

Element define(Profile profile, std::uint32_t type, std::uint32_t id) {
    Bytes payload(4);
    dword(payload, 0, id, profile);
    return tag(profile, type, std::move(payload));
}

Element sprite(Profile profile, std::uint32_t id, std::uint16_t frames,
               std::uint16_t capacity, std::vector<Element> elements,
               bool root = false) {
    const auto metadata = 24U + 4U * static_cast<std::size_t>(frames);
    Element out{Bytes(metadata), {}};
    dword(out.bytes, 0, 39, profile);
    dword(out.bytes, 4, narrow(metadata), profile);
    dword(out.bytes, 8, id, profile);
    word(out.bytes, 12, frames, profile);
    word(out.bytes, 14, capacity, profile);
    dword(out.bytes, 20, narrow(metadata), profile);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame == 0) {
            for (const auto& element : elements) {
                const auto at = out.bytes.size();
                append(out.bytes, element.bytes);
                for (const auto& marker : element.markers) {
                    out.markers.push_back({marker.label, at + marker.offset});
                }
            }
        }
        append(out.bytes, tag(profile, 1).bytes);
        dword(out.bytes, 24 + 4 * frame, narrow(out.bytes.size()), profile);
    }
    Bytes end(8);
    dword(end, 4, root ? 0U : 8U, profile);
    append(out.bytes, end);
    dword(out.bytes, 16, narrow(out.bytes.size()), profile);
    return out;
}

Element button(Profile profile) {
    const std::size_t fixed = profile == Profile::pc_2_8 ? 20U : 16U;
    const auto terminator = fixed + 48;
    Element out{Bytes(terminator + 4), {}};
    dword(out.bytes, 0, 7, profile);
    dword(out.bytes, 8, 4, profile);
    dword(out.bytes, fixed - 4, narrow(terminator), profile);
    dword(out.bytes, fixed, 5, profile);
    word(out.bytes, fixed + 4, 3, profile);
    for (std::size_t i = 0; i < identity_matrix.size(); ++i) {
        dword(out.bytes, fixed + 8 + 4 * i, identity_matrix[i], profile);
    }
    for (std::size_t i = 0; i < identity_color.size(); ++i) {
        word(out.bytes, fixed + 32 + 2 * i, identity_color[i], profile);
    }
    append_word(out.bytes, 0, profile); // Last condition, physical size0.
    append_word(out.bytes, 0, profile); // Opaque condition flags.
    append_word(out.bytes, 0, profile); // Action End.
    align4(out.bytes);
    dword(out.bytes, 4, narrow(out.bytes.size()), profile);
    return out;
}

Element placement(Profile profile, std::string_view label, std::uint16_t depth,
                  std::uint16_t flags, std::uint16_t character = 0,
                  std::array<std::uint32_t, 6> values = identity_matrix,
                  std::array<std::uint16_t, 8> colors = identity_color,
                  std::uint16_t ratio = 0, std::uint16_t clip = 0,
                  std::string_view name = {}) {
    Element out{Bytes(profile == Profile::pc_2_8 ? 12U : 60U), {{label, 0}}};
    dword(out.bytes, 0, 26, profile);
    word(out.bytes, 8, depth, profile);
    if (profile == Profile::pc_2_8) {
        word(out.bytes, 10, flags, profile);
        if ((flags & 2U) != 0) {
            append_word(out.bytes, character, profile);
        }
        if ((flags & 4U) != 0) {
            align4(out.bytes);
            const std::array<std::uint16_t, 3> pairs{0x100, 0x80, 0x200};
            for (std::size_t i = 0; i < pairs.size(); ++i) {
                if ((flags & pairs[i]) != 0) {
                    append_dword(out.bytes, values[2 * i], profile);
                    append_dword(out.bytes, values[2 * i + 1], profile);
                }
            }
        }
        if ((flags & 8U) != 0) {
            align4(out.bytes);
            for (auto value : colors) {
                append_word(out.bytes, value, profile);
            }
        }
        if ((flags & 0x10U) != 0) {
            append_word(out.bytes, ratio, profile);
        }
        if ((flags & 0x40U) != 0) {
            append_word(out.bytes, clip, profile);
        }
    } else {
        word(out.bytes, 10, character, profile);
        for (std::size_t i = 0; i < values.size(); ++i) {
            dword(out.bytes, 12 + 4 * i, values[i], profile);
        }
        for (std::size_t i = 0; i < colors.size(); ++i) {
            word(out.bytes, 36 + 2 * i, colors[i], profile);
        }
        word(out.bytes, 52, static_cast<std::uint16_t>(flags & 0xffU), profile);
        word(out.bytes, 54, ratio, profile);
        word(out.bytes, 58, clip, profile);
    }
    if ((flags & 0x20U) != 0) {
        for (unsigned char value : name) {
            out.bytes.push_back(octet(value));
        }
        out.bytes.push_back(std::byte{});
    } else if (profile == Profile::xbox_2_5) {
        // Xbox always stores the physical name terminator after its fixed
        // 52-byte payload, including when the active name is empty.
        out.bytes.push_back(std::byte{});
    }
    align4(out.bytes);
    dword(out.bytes, 4, narrow(out.bytes.size()), profile);
    return out;
}

Element removal(Profile profile, std::string_view label, std::uint16_t depth) {
    Element out{Bytes(12), {{label, 0}}};
    dword(out.bytes, 0, 28, profile);
    dword(out.bytes, 4, 12, profile);
    word(out.bytes, 8, depth, profile);
    return out;
}

std::uint32_t crc(std::span<const std::byte> bytes) {
    std::uint32_t value = 0xffffffffU;
    for (auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned i = 0; i < 8; ++i) {
            value = (value >> 1) ^ ((value & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return value ^ 0xffffffffU;
}

struct Fixture {
    Bytes bytes;
    std::vector<Marker> markers;
};

Fixture fixture(Profile profile, Bytes actions = {},
                std::uint16_t root_frames = 8) {
    const auto styled = matrix(2, 3, 4, 5, 6, 7);
    constexpr std::array<std::uint16_t, 8> old_colors{256, 128, 512, 64,
                                                      1,   2,   3,   4};
    constexpr std::array<std::uint16_t, 8> incoming{10, 20, 30, 40, 1, 2, 3, 4};
    std::vector<Element> items;
    // Deliberately physical order B2, then A1: IDs are not sprite indices.
    items.push_back(
        sprite(profile, 2, 1, 4, {placement(profile, "b_button", 1, 2, 4)}));
    items.push_back(
        sprite(profile, 1, 3, 8, {placement(profile, "a_shape", 1, 2, 3)}));
    items.push_back(define(profile, 2, 3));
    items.push_back(button(profile));
    items.push_back(define(profile, 37, 5));
    items.push_back(placement(profile, "a4", 4, 2, 1));
    items.push_back(placement(profile, "b4", 4, 2, 2));
    items.push_back(placement(profile, "shape2", 2, 2, 3));
    items.push_back(placement(profile, "a7", 7, 2, 1));
    items.push_back(placement(profile, "b7", 7, 2, 2));
    items.push_back(placement(profile, "a6", 6, 2, 1));
    items.push_back(placement(profile, "styled6", 6, 0x3fd, 0, styled,
                              old_colors, 12345, 9, "residu"));
    items.push_back(
        placement(profile, "char_move", 4, 0x38f, 2, styled, incoming));
    items.push_back(
        placement(profile, "char_reset", 4, 0x38e, 2, styled, incoming));
    items.push_back(
        placement(profile, "move_only", 4, 0x38d, 0, styled, incoming));
    items.push_back(placement(profile, "button5", 5, 2, 4));
    items.push_back(placement(profile, "a5", 5, 2, 1));
    items.push_back(placement(profile, "a2", 2, 2, 1));
    items.push_back(placement(profile, "shape9", 9, 2, 3));
    items.push_back(placement(profile, "a1", 1, 2, 1));
    items.push_back(placement(profile, "text3", 3, 2, 5));
    items.push_back(placement(profile, "self1", 1, 2, 0));
    items.push_back(placement(profile, "name4", 4, 0x21, 0, identity_matrix,
                              identity_color, 0, 0,
                              "0123456789abcdefghijklmnopqrstuvXYZ_EXTRA"));
    items.push_back(placement(profile, "reset4", 4, 0));
    for (const auto& [label, depth] :
         std::array<std::pair<std::string_view, std::uint16_t>, 8>{
             {{"rm4", std::uint16_t{4}},
              {"rm6", std::uint16_t{6}},
              {"rm7", std::uint16_t{7}},
              {"rm5", std::uint16_t{5}},
              {"rm2", std::uint16_t{2}},
              {"rm1", std::uint16_t{1}},
              {"rm9", std::uint16_t{9}},
              {"rm11", std::uint16_t{11}}}}) {
        items.push_back(removal(profile, label, depth));
    }
    if (!actions.empty()) {
        items.push_back(tag(profile, 12, std::move(actions)));
    }
    auto root = sprite(profile, 0, root_frames, 12, std::move(items), true);
    Fixture out{Bytes(72), {}};
    out.bytes[0] = std::byte{'V'};
    out.bytes[1] = std::byte{'M'};
    out.bytes[2] = std::byte{'F'};
    out.bytes[5] = std::byte{2};
    word(out.bytes, 6, profile == Profile::pc_2_8 ? 8U : 5U, profile);
    out.bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    for (std::size_t i = 0; i < 6; ++i) {
        out.bytes[12 + i] = octet(static_cast<std::uint32_t>("ORACLE"[i]));
    }
    dword(out.bytes, 48, std::bit_cast<std::uint32_t>(1280.0F), profile);
    dword(out.bytes, 56, std::bit_cast<std::uint32_t>(720.0F), profile);
    word(out.bytes, 60, 30, profile);
    word(out.bytes, 62, 6, profile);
    word(out.bytes, 64, 12, profile);
    append(out.bytes, root.bytes);
    for (const auto& marker : root.markers) {
        out.markers.push_back({marker.label, 72 + marker.offset});
    }
    const auto footer = out.bytes.size();
    out.bytes.resize(footer + 8);
    dword(out.bytes, footer, 0x54584958U, profile);
    Bytes canonical(out.bytes.begin(), out.bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U}) {
            std::swap(canonical[at], canonical[at + 1]);
        }
        for (const auto at : {44U, 48U, 52U, 56U}) {
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
        }
    }
    dword(out.bytes, 68, crc(canonical), profile);
    return out;
}

VmfMovie parse_fixture(const Fixture& data, Profile profile) {
    auto result = VmfMovie::parse(data.bytes, profile);
    if (!result) {
        std::cerr << "fixture parse offset " << result.error().offset << '\n';
    }
    return take(std::move(result), "source-owned movie parses");
}

std::size_t placement_index(const Fixture& data, const VmfDisplayData& decoded,
                            std::string_view label) {
    const auto marker =
        std::find_if(data.markers.begin(), data.markers.end(),
                     [label](auto value) { return value.label == label; });
    if (marker == data.markers.end()) {
        throw std::runtime_error("unknown fixture marker");
    }
    const auto places = decoded.placements();
    const auto found =
        std::find_if(places.begin(), places.end(), [marker](const auto& value) {
            return value.location.source_offset == marker->offset;
        });
    if (found == places.end()) {
        throw std::runtime_error("marker not decoded as placement");
    }
    return static_cast<std::size_t>(found - places.begin());
}

std::size_t removal_index(const Fixture& data, const VmfDisplayData& decoded,
                          std::string_view label) {
    const auto marker =
        std::find_if(data.markers.begin(), data.markers.end(),
                     [label](auto value) { return value.label == label; });
    if (marker == data.markers.end()) {
        throw std::runtime_error("unknown removal marker");
    }
    const auto values = decoded.removals();
    const auto found =
        std::find_if(values.begin(), values.end(), [marker](const auto& value) {
            return value.location.source_offset == marker->offset;
        });
    if (found == values.end()) {
        throw std::runtime_error("marker not decoded as removal");
    }
    return static_cast<std::size_t>(found - values.begin());
}

void fixture_preflight(Profile profile) {
    const auto source = fixture(profile);
    const auto movie = parse_fixture(source, profile);
    auto decoded_result = VmfDisplayData::decode(movie);
    if (!decoded_result) {
        const auto offset = decoded_result.error().offset;
        std::cerr << "  fixture profile=" << static_cast<int>(profile)
                  << " display_offset=" << offset << '\n';
        for (const auto& record : movie.records()) {
            if (record.type == 26 && offset >= record.source_offset &&
                offset <= record.source_offset + record.full_size) {
                std::cerr << "  record=" << record.source_offset
                          << " size=" << record.full_size << '\n';
                for (const auto& marker : source.markers) {
                    if (marker.offset == record.source_offset) {
                        std::cerr << "  marker=" << marker.label << '\n';
                    }
                }
            }
        }
    }
    const auto decoded =
        take(std::move(decoded_result), "display fixture decodes");
    check(movie.definitions().size() == 6 && movie.sprites().size() == 3,
          "six definitions / three sprites");
    check(movie.sprites()[1].character_id == 2 &&
              movie.sprites()[2].character_id == 1,
          "physical sprite order differs from character order");
    check(decoded.placements().size() == 21 && decoded.removals().size() == 8,
          "fixture event counts");
    const auto& styled =
        decoded.placements()[placement_index(source, decoded, "styled6")];
    const auto expected_flags = profile == Profile::pc_2_8 ? 0x3fdU : 0xfdU;
    check(styled.flags == expected_flags && styled.depth == 6 &&
              !styled.character_id,
          "move-only style input flags");
    check(styled.matrix_bits == matrix(2, 3, 4, 5, 6, 7),
          "all six independent matrix operands decoded");
    check(styled.ratio == std::optional<std::uint16_t>{std::uint16_t{12345}} &&
              styled.clip_depth ==
                  std::optional<std::uint16_t>{std::uint16_t{9}} &&
              styled.name == "residu",
          "style fixture scalar/name operands");
    for (const auto& marker : source.markers) {
        if (marker.label.starts_with("rm")) {
            check(removal_index(source, decoded, marker.label) <
                      decoded.removals().size(),
                  "removal marker belongs to decoded record");
        } else {
            check(placement_index(source, decoded, marker.label) <
                      decoded.placements().size(),
                  "placement marker belongs to decoded record");
        }
    }
}

using Action = VmfTimelineAction;
struct TimelineCase {
    Action action;
    std::uint16_t operand;
    std::uint16_t frame_count;
    VmfDriverFrames before;
    std::uint32_t play_before;
    VmfDriverFrames after;
    std::uint32_t play_after;
};

// Exact hand-fixed rows from cases-v1.json; no production helper calculates
// the expectations. Current is intentionally separate from requested.
constexpr std::array<TimelineCase, 18> cases{{
    {Action::next_frame, 65535, 3, {1, 3, true}, 2, {1, 2, true}, 2},
    {Action::next_frame, 0, 3, {3, 1, true}, 1, {3, 3, false}, 1},
    {Action::next_frame, 0, 3, {4, 1, false}, 2, {4, 3, true}, 2},
    {Action::previous_frame, 65535, 3, {1, 3, true}, 2, {1, 3, true}, 2},
    {Action::previous_frame, 0, 3, {1, 1, true}, 1, {1, 1, true}, 1},
    {Action::previous_frame, 0, 3, {3, 1, false}, 1, {3, 2, true}, 1},
    {Action::previous_frame, 0, 3, {4, 1, false}, 2, {4, 3, true}, 2},
    {Action::play, 65535, 3, {2, 3, true}, 2, {2, 3, true}, 1},
    {Action::stop, 65535, 3, {2, 1, false}, 1, {2, 1, false}, 2},
    {Action::goto_frame, 0, 3, {2, 3, false}, 1, {2, 1, true}, 2},
    {Action::goto_frame, 1, 3, {2, 1, true}, 1, {2, 2, false}, 2},
    {Action::goto_frame, 65535, 3, {2, 1, false}, 1, {2, 3, true}, 2},
    {Action::goto_frame, 32767, 3, {3, 1, false}, 1, {3, 3, false}, 2},
    {Action::next_frame, 0, 1, {1, 1, true}, 1, {1, 1, false}, 1},
    {Action::previous_frame, 0, 1, {1, 1, true}, 1, {1, 1, true}, 1},
    {Action::next_frame, 0, 1, {2, 1, false}, 2, {2, 1, true}, 2},
    {Action::previous_frame, 0, 1, {2, 1, false}, 1, {2, 1, true}, 1},
    {Action::goto_frame, 65535, 1, {1, 1, true}, 1, {1, 1, false}, 2},
}};

std::uint16_t opcode(Action action) {
    switch (action) {
    case Action::next_frame:
        return 4;
    case Action::previous_frame:
        return 5;
    case Action::play:
        return 6;
    case Action::stop:
        return 7;
    case Action::goto_frame:
        return 0x81;
    }
    throw std::runtime_error("invalid fixture action");
}
void op(Bytes& out, Profile profile, std::uint16_t code, Bytes payload = {}) {
    append_word(out, code, profile);
    if (code >= 0x80) {
        append_word(out, static_cast<std::uint16_t>(payload.size()), profile);
        append(out, payload);
        if (out.size() % 2 != 0)
            out.push_back(std::byte{});
    } else if (!payload.empty()) {
        throw std::runtime_error("short fixture opcode has payload");
    }
}
void action_op(Bytes& out, Profile profile, Action action,
               std::uint16_t operand = 0) {
    Bytes payload;
    if (action == Action::goto_frame) {
        payload.resize(2);
        word(payload, 0, operand, profile);
    }
    op(out, profile, opcode(action), std::move(payload));
}
Bytes one_action(Profile profile, Action action, std::uint16_t operand = 0) {
    Bytes out;
    action_op(out, profile, action, operand);
    op(out, profile, 0);
    return out;
}

struct Prepared {
    Fixture input;
    VmfProgramPtr program;
    std::size_t script;
    std::size_t p(std::string_view name) const {
        return placement_index(input, program->display(), name);
    }
    std::size_t r(std::string_view name) const {
        return removal_index(input, program->display(), name);
    }
};
std::size_t root_script(const VmfMovie& movie) {
    const auto scripts = movie.scripts();
    const auto matches = [](const auto& value) {
        return value.kind == VmfScriptKind::timeline &&
               value.parent_sprite_index == 0;
    };
    const auto found = std::find_if(scripts.begin(), scripts.end(), matches);
    check(found != scripts.end() &&
              std::count_if(scripts.begin(), scripts.end(), matches) == 1,
          "exactly one root timeline script, distinct from button conditions");
    if (found == scripts.end())
        throw std::runtime_error("missing root script");
    return static_cast<std::size_t>(found - scripts.begin());
}
Prepared prepare(Profile profile, Bytes actions, std::uint16_t frames = 8) {
    auto input = fixture(profile, std::move(actions), frames);
    auto movie = parse_fixture(input, profile);
    auto program = take(VmfProgram::build(movie), "timeline program built");
    const auto script = root_script(program->movie());
    return {std::move(input), std::move(program), script};
}
std::unique_ptr<VmfDisplayRuntime> runtime(const Prepared& data) {
    return take(VmfDisplayRuntime::create(data.program,
                                          {8, 4, 32, 16, 2U * 1024U * 1024U}),
                "timeline runtime created");
}
VmfStack stack() {
    return take(VmfStack::create(
                    take(VmfTextScratch::create(), "numeric scratch created")),
                "execution stack created");
}
VmfInstanceSnapshot instance(const VmfDisplayRuntime& display,
                             VmfInstanceHandle handle) {
    return take(display.inspect_instance(handle),
                "timeline instance inspected");
}
VmfNodeSnapshot node(const VmfDisplayRuntime& display, VmfNodeHandle handle) {
    return take(display.inspect_node(handle), "timeline node inspected");
}
VmfNodeHandle place(VmfDisplayRuntime& display, const Prepared& data,
                    std::string_view label) {
    const auto result =
        take(display.place(display.root(), data.p(label)), "setup placement");
    check(result.node.has_value(), "setup node exists");
    if (!result.node)
        throw std::runtime_error("missing fixture node");
    return *result.node;
}
VmfInstanceHandle child(const VmfDisplayRuntime& display,
                        VmfNodeHandle handle) {
    auto value = node(display, handle).child;
    check(value.has_value(), "setup child exists");
    if (!value)
        throw std::runtime_error("missing fixture child");
    return *value;
}
bool same(const VmfInstanceSnapshot& a, const VmfInstanceSnapshot& b) {
    return a.handle == b.handle && a.kind == b.kind &&
           a.character_id == b.character_id && a.parent == b.parent &&
           a.attached_node == b.attached_node && a.depth == b.depth &&
           a.birth_frame == b.birth_frame && a.scope_id == b.scope_id &&
           a.cached == b.cached && a.frames == b.frames &&
           a.play_state == b.play_state && a.flags == b.flags &&
           a.record_balance == b.record_balance &&
           a.active_nodes == b.active_nodes &&
           a.cached_children == b.cached_children && a.local == b.local &&
           a.composed == b.composed;
}
bool same(const VmfNodeSnapshot& a, const VmfNodeSnapshot& b) {
    return a.handle == b.handle && a.owner == b.owner &&
           a.character_id == b.character_id && a.depth == b.depth &&
           a.clip_depth == b.clip_depth && a.ratio == b.ratio &&
           a.placement_flags == b.placement_flags && a.name == b.name &&
           a.child == b.child && a.local == b.local && a.composed == b.composed;
}
bool same(const VmfDisplaySnapshot& a, const VmfDisplaySnapshot& b) {
    return a.program == b.program && a.instances.size() == b.instances.size() &&
           a.nodes.size() == b.nodes.size() &&
           std::equal(
               a.instances.begin(), a.instances.end(), b.instances.begin(),
               [](const auto& x, const auto& y) { return same(x, y); }) &&
           std::equal(a.nodes.begin(), a.nodes.end(), b.nodes.begin(),
                      [](const auto& x, const auto& y) { return same(x, y); });
}
VmfDisplaySnapshot snapshot(const VmfDisplayRuntime& display) {
    return take(display.snapshot(), "whole display snapshot");
}
VmfDisplaySnapshot changed(VmfDisplaySnapshot expected,
                           VmfInstanceHandle handle, VmfDriverFrames frames,
                           std::uint32_t play) {
    auto found = std::find_if(
        expected.instances.begin(), expected.instances.end(),
        [handle](const auto& value) { return value.handle == handle; });
    if (found == expected.instances.end())
        throw std::runtime_error("expected target missing");
    found->frames = frames;
    found->play_state = play;
    return expected;
}
void seed(VmfDisplayRuntime& display, VmfInstanceHandle target,
          VmfDriverFrames frames, std::uint32_t play) {
    succeeds(display.set_driver_frames(target, frames),
             "seed independent driver state");
    succeeds(display.apply_timeline_action(target, play == 1 ? Action::play
                                                             : Action::stop),
             "seed play state");
    const auto actual = instance(display, target);
    check(actual.frames == frames && actual.play_state == play,
          "seeding Play/Stop must not normalize driver state");
}
struct StackState {
    std::size_t size;
    std::size_t owned;
    std::size_t linked;
    std::optional<VmfValueKind> kind;
    std::optional<std::uint32_t> bits;
    std::optional<VmfSpriteIdentity> sprite;
    std::string text;
    friend bool operator==(const StackState&, const StackState&) = default;
};
StackState stack_state(const VmfStack& value) {
    StackState out{value.size(),
                   value.owned_text_bytes(),
                   value.linked_reference_bytes(),
                   {},
                   {},
                   {},
                   {}};
    if (const auto* top = value.peek()) {
        out.kind = top->kind();
        out.bits = top->number_bits();
        out.sprite = top->sprite_identity();
        if (const auto reference = top->text_reference())
            out.text = std::string(reference->text());
    }
    return out;
}
void report_is(const VmfExecutionReport& report, VmfExecutionStatus status,
               std::size_t count, std::uint16_t next) {
    check(report.status == status && report.instructions_executed == count &&
              report.text_bytes_consumed == 0 && report.next_opcode == next,
          "execution report status/count/text/next opcode");
}

enum class TargetLocation { root, child, cached };
void numeric_cases(Profile profile) {
    for (const auto& row : cases) {
        for (auto location : {TargetLocation::root, TargetLocation::child,
                              TargetLocation::cached}) {
            auto data =
                prepare(profile, one_action(profile, row.action, row.operand),
                        location == TargetLocation::root ? row.frame_count
                                                         : std::uint16_t{8});
            auto display = runtime(data);
            auto target = display->root();
            if (location != TargetLocation::root) {
                succeeds(display->set_driver_frames(target, {5, 5, false}),
                         "parent birth frame5");
                const auto placed =
                    place(*display, data, row.frame_count == 3 ? "a4" : "b7");
                target = child(*display, placed);
                if (location == TargetLocation::cached) {
                    const auto label = row.frame_count == 3 ? "rm4" : "rm7";
                    check(take(display->remove(display->root(), data.r(label)),
                               "cache target"),
                          "setup remove found");
                    check(instance(*display, target).cached,
                          "target is alive cached");
                }
            }
            seed(*display, target, row.before, row.play_before);
            auto before = snapshot(*display);
            const auto expected =
                changed(before, target, row.after, row.play_after);
            succeeds(
                display->apply_timeline_action(target, row.action, row.operand),
                "direct native timeline action");
            check(same(expected, snapshot(*display)),
                  "direct action changes ONLY expected fields of target");

            seed(*display, target, row.before, row.play_before);
            before = snapshot(*display);
            auto operands = stack();
            check(operands.push_text("sentinel kept").has_value(),
                  "sentinel text pushed");
            const auto old_stack = stack_state(operands);
            VmfExecutionCursor cursor{data.script};
            const auto result = take(
                execute_vmf_actions(*display, target, cursor, operands, {2, 0}),
                "routed script action plus End");
            report_is(result, VmfExecutionStatus::ended, 2, 0);
            check(cursor.ended && !cursor.faulted && cursor.next_action == 1,
                  "End cursor stays at End after one routed action");
            check(stack_state(operands) == old_stack,
                  "five actions do not pop/convert/reset stack");
            check(same(changed(before, target, row.after, row.play_after),
                       snapshot(*display)),
                  "runner changes ONLY target frame request/play fields");
        }
    }
}

void endian_and_binding(Profile profile) {
    for (const auto [operand, requested] :
         std::array<std::pair<std::uint16_t, std::int32_t>, 2>{
             {{std::uint16_t{0x0100}, 257}, {std::uint16_t{1}, 2}}}) {
        auto data = prepare(
            profile, one_action(profile, Action::goto_frame, operand), 300);
        auto display = runtime(data);
        const auto root = display->root();
        seed(*display, root, {299, 1, true}, 1);
        const auto before = snapshot(*display);
        auto operands = stack();
        VmfExecutionCursor cursor{data.script};
        report_is(take(execute_vmf_actions(*display, root, cursor, operands),
                       "non-symmetric Goto operand"),
                  VmfExecutionStatus::ended, 2, 0);
        check(same(changed(before, root, {299, requested, true}, 2),
                   snapshot(*display)),
              "Goto endian decoded without clamp masking byte reversal");
    }
    auto data =
        prepare(profile, one_action(profile, Action::goto_frame, 65535));
    auto display = runtime(data);
    const auto root = display->root();
    const auto original_node = place(*display, data, "a4");
    const auto target = child(*display, original_node);
    check(take(display->remove(root, data.r("rm4")), "detach A same frame"),
          "removed A");
    const auto rebound = place(*display, data, "b4");
    check(child(*display, rebound) == target &&
              node(*display, rebound).character_id == 2 &&
              instance(*display, target).character_id == 1,
          "node resource1frame differs from target resource3frames");
    seed(*display, target, {2, 1, false}, 1);
    const auto before = snapshot(*display);
    auto operands = stack();
    VmfExecutionCursor cursor{data.script};
    report_is(take(execute_vmf_actions(*display, target, cursor, operands),
                   "root script targets A bound through B"),
              VmfExecutionStatus::ended, 2, 0);
    check(same(changed(before, target, {2, 3, true}, 2), snapshot(*display)),
          "target OWN resource provides frameCount, never script or node "
          "resource");
}

void budgets_and_loops(Profile profile) {
    Bytes instructions;
    for (auto command : {Action::play, Action::next_frame, Action::stop,
                         Action::goto_frame, Action::previous_frame}) {
        action_op(instructions, profile, command, 1);
    }
    op(instructions, profile, 0);
    auto data = prepare(profile, std::move(instructions));
    auto display = runtime(data);
    const auto target = child(*display, place(*display, data, "a4"));
    seed(*display, target, {1, 3, true}, 2);
    auto operands = stack();
    check(
        operands.push_sprite(vmf_sprite_identity(display->root())).has_value(),
        "sprite sentinel");
    const auto old_stack = stack_state(operands);
    VmfExecutionCursor cursor{data.script};
    struct Step {
        std::size_t budget;
        std::size_t executed;
        std::size_t index;
        std::uint16_t next;
        std::int32_t requested;
        bool pending;
        std::uint32_t play;
        bool ended;
    };
    constexpr std::array<Step, 6> steps{{
        {0, 0, 0, 6, 3, true, 2, false},
        {1, 1, 1, 4, 3, true, 1, false},
        {2, 2, 3, 0x81, 2, true, 2, false},
        {1, 1, 4, 5, 2, true, 2, false},
        {1, 1, 5, 0, 2, true, 2, false},
        {1, 1, 5, 0, 2, true, 2, true},
    }};
    for (const auto& step : steps) {
        const auto before = snapshot(*display);
        const auto result =
            take(execute_vmf_actions(*display, target, cursor, operands,
                                     {step.budget, 0}),
                 "budgeted timeline script resumes");
        report_is(result,
                  step.ended ? VmfExecutionStatus::ended
                             : VmfExecutionStatus::instruction_limit,
                  step.executed, step.next);
        check(cursor.next_action == step.index && cursor.ended == step.ended &&
                  !cursor.faulted,
              "cursor names exact next instruction without replay");
        check(same(changed(before, target, {1, step.requested, step.pending},
                           step.play),
                   snapshot(*display)),
              "budget stop preserves prefix effects only");
        check(stack_state(operands) == old_stack,
              "timeline sequence leaves sprite operand untouched");
    }
    const auto ended_state = snapshot(*display);
    report_is(
        take(execute_vmf_actions(*display, target, cursor, operands, {100, 0}),
             "ended cursor repeats"),
        VmfExecutionStatus::ended, 0, 0);
    check(same(ended_state, snapshot(*display)), "ended cursor idempotent");

    Bytes loop;
    op(loop, profile, 4);
    Bytes delta(2);
    word(delta, 0, 0xfffaU, profile); // Compact next1 + jump5 => -6 toNext.
    op(loop, profile, 0x99, delta);
    op(loop, profile, 0);
    auto loop_data = prepare(profile, std::move(loop), 3);
    auto loop_display = runtime(loop_data);
    const auto root = loop_display->root();
    seed(*loop_display, root, {1, 3, true}, 2);
    const auto before = snapshot(*loop_display);
    cursor = {loop_data.script};
    report_is(
        take(execute_vmf_actions(*loop_display, root, cursor, operands, {3, 0}),
             "bounded timeline backedge"),
        VmfExecutionStatus::instruction_limit, 3, 0x99);
    check(cursor.next_action == 1 && !cursor.ended && !cursor.faulted,
          "backedge budget ends at Jump");
    check(same(changed(before, root, {1, 2, true}, 2), snapshot(*loop_display)),
          "repeated Next still derives request from current, never ticks");
    const auto after = snapshot(*loop_display);
    report_is(
        take(execute_vmf_actions(*loop_display, root, cursor, operands, {1, 0}),
             "resume Jump alone"),
        VmfExecutionStatus::instruction_limit, 1, 4);
    check(cursor.next_action == 0 && same(after, snapshot(*loop_display)),
          "Jump resume itself does not replay a frame action");

    auto no_alloc_data =
        prepare(profile, one_action(profile, Action::goto_frame, 1), 3);
    auto no_alloc_display = runtime(no_alloc_data);
    VmfExecutionCursor no_alloc_cursor{no_alloc_data.script};
    std::expected<VmfExecutionReport, VmfExecutionError> result;
    std::size_t allocations{};
    {
        AllocationFailureScope observe(std::numeric_limits<std::size_t>::max());
        result =
            execute_vmf_actions(*no_alloc_display, no_alloc_display->root(),
                                no_alloc_cursor, operands, {2, 0});
        allocations = timeline_allocation_fixture::state.calls;
    }
    check(result.has_value() && allocations == 0,
          "routed execution performs no ordinary allocations");
}

Bytes barrier_payload(Profile profile, std::uint16_t code) {
    (void)profile;
    if (code == 0x83)
        return Bytes(2); // Two empty NUL-terminated strings.
    if (code == 0x8b || code == 0x8c || code == 0x9a || code == 0x9f)
        return Bytes(1);
    return {};
}
void old_and_remaining_barriers(Profile profile) {
    constexpr std::array<std::uint16_t, 12> barriers{
        0x1c, 0x1d, 0x20, 0x22, 0x23, 0x26, 0x83, 0x8b, 0x8c, 0x9a, 0x9e, 0x9f};
    for (auto code : barriers) {
        Bytes instructions;
        op(instructions, profile, 0x0a); // Prefix Add consumes two numbers.
        op(instructions, profile, code, barrier_payload(profile, code));
        op(instructions, profile, 0);
        auto data = prepare(profile, std::move(instructions));
        auto display = runtime(data);
        auto operands = stack();
        check(operands.push_number(1).has_value() &&
                  operands.push_number(2).has_value(),
              "barrier prefix operands");
        VmfExecutionCursor cursor{data.script};
        const auto before = snapshot(*display);
        report_is(take(execute_vmf_actions(*display, display->root(), cursor,
                                           operands),
                       "remaining host barrier"),
                  VmfExecutionStatus::needs_host, 1, code);
        check(cursor.next_action == 1 && !cursor.faulted && !cursor.ended,
              "other host action does not advance cursor");
        check(operands.size() == 1 &&
                  operands.peek()->number_bits() ==
                      std::optional<std::uint32_t>{0x40400000U},
              "only Add prefix executed, host did not pop");
        check(same(before, snapshot(*display)),
              "other host barrier does not mutate display");
        const auto waiting_stack = stack_state(operands);
        report_is(take(execute_vmf_actions(*display, display->root(), cursor,
                                           operands),
                       "same host boundary repeats"),
                  VmfExecutionStatus::needs_host, 0, code);
        check(cursor.next_action == 1 &&
                  stack_state(operands) == waiting_stack &&
                  same(before, snapshot(*display)),
              "waiting host does not replay prefix or reset stack");
    }
    for (auto command : {Action::next_frame, Action::previous_frame,
                         Action::play, Action::stop, Action::goto_frame}) {
        Bytes instructions;
        op(instructions, profile, 0x0a);
        action_op(instructions, profile, command, 1);
        op(instructions, profile, 0);
        auto data = prepare(profile, std::move(instructions));
        auto display = runtime(data);
        auto operands = stack();
        check(operands.push_number(1).has_value() &&
                  operands.push_number(2).has_value(),
              "old API prefix operands");
        const auto before = snapshot(*display);
        VmfExecutionCursor cursor{data.script};
        report_is(
            take(execute_vmf_actions(data.program->movie(), cursor, operands),
                 "legacy pure API remains strict"),
            VmfExecutionStatus::needs_host, 1, opcode(command));
        check(cursor.next_action == 1 && operands.size() == 1 &&
                  operands.peek()->number_bits() ==
                      std::optional<std::uint32_t>{0x40400000U} &&
                  same(before, snapshot(*display)),
              "old 19/17 path is not silently widened");
    }
}

void target_errors(Profile profile) {
    Bytes instructions;
    op(instructions, profile, 0x0a);
    op(instructions, profile, 6);
    op(instructions, profile, 0);
    const auto data = prepare(profile, std::move(instructions));
    auto display = runtime(data);
    const auto root = display->root();
    auto foreign_display = runtime(data);
    const auto stale = child(*display, place(*display, data, "a4"));
    succeeds(display->set_driver_frames(root, {2, 2, false}),
             "advance owner observation for destruction");
    check(take(display->remove(root, data.r("rm4")), "destroy old timeline"),
          "old child removed");
    const auto replacement = child(*display, place(*display, data, "a4"));
    check(replacement != stale, "replacement is a new logical handle");
    const auto button_target =
        child(*display, place(*display, data, "button5"));
    const std::array<std::pair<VmfInstanceHandle, RuntimeError>, 4> invalid{{
        {VmfInstanceHandle{}, RuntimeError::invalid_handle},
        {foreign_display->root(), RuntimeError::invalid_handle},
        {stale, RuntimeError::invalid_handle},
        {button_target, RuntimeError::wrong_kind},
    }};
    for (const auto& [target, expected_error] : invalid) {
        const auto before = snapshot(*display);
        const auto direct =
            display->apply_timeline_action(target, Action::play);
        check(!direct && direct.error().code == expected_error &&
                  same(before, snapshot(*display)),
              "invalid direct target refused without implicit root");
        auto operands = stack();
        check(operands.push_number(1).has_value() &&
                  operands.push_number(2).has_value(),
              "invalid target prefix operands");
        const auto stack_before = stack_state(operands);
        VmfExecutionCursor cursor{data.script};
        const auto result =
            execute_vmf_actions(*display, target, cursor, operands);
        check(
            !result && result.error().code == VmfExecutionErrorCode::timeline &&
                result.error().display_error &&
                result.error().display_error->code == expected_error &&
                !result.error().stack_error &&
                result.error().instructions_executed == 0 &&
                result.error().script_index == data.script &&
                result.error().action_index == 0 && result.error().opcode == 0,
            "target is validated before scalar prefix and reports nested "
            "display error");
        check(cursor.faulted && !cursor.ended && cursor.next_action == 0 &&
                  stack_state(operands) == stack_before &&
                  same(before, snapshot(*display)),
              "target error only faults cursor, preserves stack/display "
              "entirely");
        const auto retry =
            execute_vmf_actions(*display, root, cursor, operands);
        check(!retry &&
                  retry.error().code == VmfExecutionErrorCode::invalid_cursor &&
                  stack_state(operands) == stack_before &&
                  same(before, snapshot(*display)),
              "faulted cursor cannot retry effects with a repaired target");
    }
    const auto before = snapshot(*display);
    const auto unknown_enum =
        display->apply_timeline_action(root, static_cast<Action>(999), 65535);
    check(!unknown_enum &&
              unknown_enum.error().code == RuntimeError::invalid_driver_state &&
              same(before, snapshot(*display)),
          "unknown action enum has no default successful behavior");
    auto operands = stack();
    check(operands.push_text("closed sentinel").has_value(),
          "closed sentinel prepared");
    const auto stack_before = stack_state(operands);
    auto owning = display->program();
    check(owning == data.program,
          "program() returns associated owning program");
    succeeds(display->close(), "close target context");
    const auto direct = display->apply_timeline_action(root, Action::play);
    check(!direct && direct.error().code == RuntimeError::closed,
          "closed target direct refusal");
    VmfExecutionCursor cursor{data.script};
    const auto closed = execute_vmf_actions(*display, root, cursor, operands);
    check(!closed && closed.error().code == VmfExecutionErrorCode::timeline &&
              closed.error().display_error &&
              closed.error().display_error->code == RuntimeError::closed &&
              closed.error().instructions_executed == 0 &&
              closed.error().opcode == 0 && cursor.faulted &&
              stack_state(operands) == stack_before,
          "closed target faults cursor before touching scalar prefix");
    check(display->program() == owning, "program remains owned after close");
    display.reset();
    check(owning->movie().source_bytes()[0] == std::byte{'V'},
          "owning program survives runtime destruction");
}

void inherited_cursor_guards(Profile profile) {
    const auto data = prepare(profile, one_action(profile, Action::play));
    auto display = runtime(data);
    auto operands = stack();
    const auto before = snapshot(*display);
    VmfExecutionCursor forged{data.script};
    forged.ended = true;
    const auto bad_end =
        execute_vmf_actions(*display, display->root(), forged, operands);
    check(!bad_end &&
              bad_end.error().code == VmfExecutionErrorCode::invalid_cursor &&
              same(before, snapshot(*display)),
          "forged ended cursor cannot skip a live Play");
    VmfExecutionCursor invalid_index{data.script};
    invalid_index.next_action =
        data.program->movie().scripts()[data.script].action_count;
    const auto bad_index =
        execute_vmf_actions(*display, display->root(), invalid_index, operands);
    check(!bad_index &&
              bad_index.error().code == VmfExecutionErrorCode::invalid_cursor &&
              same(before, snapshot(*display)),
          "new context preserves old action cursor bounds");
    VmfExecutionCursor invalid_limits{data.script};
    const auto bad_limit = execute_vmf_actions(
        *display, display->root(), invalid_limits, operands, {2, 16'777'217});
    check(!bad_limit &&
              bad_limit.error().code == VmfExecutionErrorCode::invalid_limits &&
              same(before, snapshot(*display)),
          "new context preserves numeric/text safety domain");
}

void timeline_fixture_preflight(Profile profile) {
    const auto source =
        fixture(profile, one_action(profile, Action::goto_frame, 0x0100), 300);
    const auto movie = parse_fixture(source, profile);
    const auto index = root_script(movie);
    const auto script = movie.scripts()[index];
    const auto first = script.first_action;
    check(movie.scripts().size() == 2 && script.action_count == 2,
          "root timeline script distinct from button-condition End");
    check(movie.sprites()[0].frame_count == 300 &&
              movie.actions()[first].opcode == 0x81 &&
              movie.actions()[first].encoded_size == 6 &&
              movie.actions()[first + 1].opcode == 0,
          "non-symmetric Goto fixture has six physical bytes and correct "
          "frameCount");
    const auto payload =
        take(movie.bytes(movie.actions()[first].payload), "Goto bytes bounded");
    check(payload.size() == 2 &&
              payload[0] ==
                  (profile == Profile::pc_2_8 ? std::byte{0} : std::byte{1}) &&
              payload[1] ==
                  (profile == Profile::pc_2_8 ? std::byte{1} : std::byte{0}),
          "Goto0100 endian fixture bytes are fixed independently");
    const auto decoded =
        take(VmfDisplayData::decode(movie), "timeline fixture display data");
    check(decoded.placements().size() == 21 && decoded.removals().size() == 8,
          "adding script does not alter display inputs");
}
} // namespace

int main(int argc, char** argv) {
    try {
        for (auto profile : profiles) {
            fixture_preflight(profile);
            timeline_fixture_preflight(profile);
        }
        const bool fixture_only =
            argc == 2 && std::string_view(argv[1]) == "--fixture-only";
        if (!fixture_only) {
            for (auto profile : profiles) {
                numeric_cases(profile);
                endian_and_binding(profile);
                budgets_and_loops(profile);
                old_and_remaining_barriers(profile);
                target_errors(profile);
                inherited_cursor_guards(profile);
            }
        }
        std::cout << "VMF timeline execution: " << checks << " checks, "
                  << failures << " failures\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FATAL: " << error.what() << '\n';
    }
    return failures == 0 ? 0 : 1;
}

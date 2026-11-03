#include "tdur/ui/vmf_display_runtime.hpp"

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

// Independent expectations: vmf-runtime-lifecycle-oracle/ORACLES-v1.md,
// frozen before vmf_display_runtime.cpp existed. Neither production CPP was
// read to write this harness. Byte fixtures below are source-owned.
namespace runtime_allocation_fixture {
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
} // namespace runtime_allocation_fixture

void* operator new(std::size_t bytes) {
    return runtime_allocation_fixture::allocate(bytes);
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
        : previous_(runtime_allocation_fixture::state) {
        runtime_allocation_fixture::state = {true, 0, ordinal};
    }
    ~AllocationFailureScope() noexcept {
        runtime_allocation_fixture::state = previous_;
    }
    AllocationFailureScope(const AllocationFailureScope&) = delete;
    AllocationFailureScope& operator=(const AllocationFailureScope&) = delete;

  private:
    runtime_allocation_fixture::State previous_;
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

Fixture fixture(Profile profile) {
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
    auto root = sprite(profile, 0, 8, 12, std::move(items), true);
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

#ifndef VMF_RUNTIME_FIXTURE_ONLY

// Runtime tests follow; assertions are mapped from the pre-implementation
// oracle, not derived from a production method body.

struct Prepared {
    Fixture input;
    VmfProgramPtr program;
    std::size_t p(std::string_view label) const {
        return placement_index(input, program->display(), label);
    }
    std::size_t r(std::string_view label) const {
        return removal_index(input, program->display(), label);
    }
};

Prepared prepare(Profile profile) {
    auto input = fixture(profile);
    auto movie = parse_fixture(input, profile);
    auto program = take(VmfProgram::build(movie), "owning program built");
    return {std::move(input), std::move(program)};
}

VmfDisplayRuntimeLimits small_limits() {
    return {8, 4, 32, 16, 2U * 1024U * 1024U};
}

std::unique_ptr<VmfDisplayRuntime>
runtime(const Prepared& data, VmfDisplayRuntimeLimits limits = small_limits()) {
    return take(VmfDisplayRuntime::create(data.program, limits),
                "bounded runtime created");
}

VmfInstanceSnapshot instance(const VmfDisplayRuntime& value,
                             VmfInstanceHandle handle) {
    return take(value.inspect_instance(handle), "instance handle valid");
}
VmfNodeSnapshot node(const VmfDisplayRuntime& value, VmfNodeHandle handle) {
    return take(value.inspect_node(handle), "node handle valid");
}
VmfNodeHandle
place(VmfDisplayRuntime& value, VmfInstanceHandle owner, const Prepared& data,
      std::string_view label,
      VmfPlaceDisposition disposition = VmfPlaceDisposition::created_node) {
    const auto result =
        take(value.place(owner, data.p(label)), "place succeeds");
    check(result.disposition == disposition, "place disposition");
    check(result.native_al == (disposition != VmfPlaceDisposition::reused_node),
          "native AL means new/ignored, not generic success");
    check(result.node.has_value(), "successful in-range place returns a node");
    if (!result.node) {
        throw std::runtime_error("missing placed node");
    }
    return *result.node;
}
VmfInstanceHandle child(const VmfDisplayRuntime& value, VmfNodeHandle handle) {
    auto result = node(value, handle).child;
    check(result.has_value(), "node owns expected child");
    if (!result) {
        throw std::runtime_error("missing child");
    }
    return *result;
}
void driver(VmfDisplayRuntime& value, VmfInstanceHandle owner,
            std::int32_t current, std::int32_t requested,
            bool pending = false) {
    succeeds(value.set_driver_frames(owner, {current, requested, pending}),
             "explicit driver frame state accepted");
    check(instance(value, owner).frames ==
              VmfDriverFrames{current, requested, pending},
          "driver setter preserves independent pending bit");
}
void remove_found(VmfDisplayRuntime& value, VmfInstanceHandle owner,
                  const Prepared& data, std::string_view label) {
    check(take(value.remove(owner, data.r(label)), "remove succeeds"),
          "expected active depth removed");
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

template <class Action>
void refuses_without_mutation(VmfDisplayRuntime& value, Action action,
                              RuntimeError error, std::string_view label) {
    const auto before = take(value.snapshot(), "snapshot before refusal");
    const auto result = action();
    check(!result, label);
    if (!result) {
        check(result.error().code == error, label);
        check(!result.error().detail.empty(), "error has static diagnostic");
    }
    const auto after = take(value.snapshot(), "snapshot after refusal");
    check(same(before, after), "refusal preserves ALL observable state");
}

template <class T>
void rejects(std::expected<T, VmfDisplayRuntimeError> result,
             RuntimeError error, std::string_view label) {
    check(!result, label);
    if (!result) {
        check(result.error().code == error, label);
    }
}

std::string_view bounded_name(const VmfNodeSnapshot& value) {
    const auto end = std::find(value.name.begin(), value.name.end(), '\0');
    return {value.name.data(),
            static_cast<std::size_t>(end - value.name.begin())};
}

void program_contract(Profile profile) {
    auto source = fixture(profile);
    const auto movie = parse_fixture(source, profile);
    auto program = take(VmfProgram::build(movie), "program owns its own pair");
    check(program->sprite_for_character(0) == std::optional<std::size_t>{0} &&
              program->sprite_for_character(1) ==
                  std::optional<std::size_t>{2} &&
              program->sprite_for_character(2) == std::optional<std::size_t>{1},
          "character to sprite association is not ID/index identity");
    check(!program->sprite_for_character(3) &&
              !program->sprite_for_character(4) &&
              !program->sprite_for_character(
                  std::numeric_limits<std::uint32_t>::max()),
          "non-sprite and unknown characters have no sprite index");
    const auto expected = sizeof(VmfProgram) + movie.logical_output_bytes() +
                          program->display().logical_output_bytes() +
                          sizeof(std::size_t) * movie.definitions().size();
    check(program->logical_output_bytes() == expected,
          "program logical byte contract");
    check(VmfProgram::build(movie, {}, expected).has_value(),
          "exact program budget admitted");
    const auto too_small = VmfProgram::build(movie, {}, expected - 1);
    check(!too_small && too_small.error().code == VmfErrorCode::resource_limit,
          "one byte below program budget refused");
    auto no_places = VmfDisplayLimits{};
    no_places.max_placements = 0;
    check(!VmfProgram::build(movie, no_places),
          "display decode limits reach program factory");
    std::fill(source.bytes.begin(), source.bytes.end(), std::byte{0xff});
    check(program->movie().source_bytes()[0] == std::byte{'V'},
          "program does not borrow input storage");
    const auto long_name = placement_index(source, program->display(), "name4");
    check(program->display().placements()[long_name].name.size() > 31,
          "immutable program keeps full placement name");
}

void initial_and_driver(Profile profile) {
    const auto data = prepare(profile);
    auto value = runtime(data);
    const auto root = value->root();
    const auto initial = instance(*value, root);
    check(initial.kind == VmfInstanceKind::timeline &&
              initial.character_id == 0 &&
              initial.scope_id == std::optional<std::uint32_t>{0} &&
              initial.birth_frame == 1 && initial.depth == -1 &&
              !initial.parent && !initial.attached_node && !initial.cached,
          "root identity / birth / parent defaults");
    check(initial.frames == VmfDriverFrames{1, 1, false} &&
              initial.play_state == 1 && initial.flags == 7 &&
              initial.record_balance == 0 && initial.active_nodes == 0 &&
              initial.cached_children == 0,
          "root frame/play/list defaults");
    check(initial.local == VmfTransform{} && initial.composed == VmfTransform{},
          "root copies identity local without child double composition");
    for (const bool pending : {false, true}) {
        driver(*value, root, 1, 1, pending);
        driver(*value, root, 2, 1, pending);
    }
    driver(*value, root, 9, 8, true);
    check(instance(*value, root).active_nodes == 0,
          "driver setters do not execute queued placements");
    for (const auto bad : std::array<VmfDriverFrames, 4>{
             {{0, 1, false}, {10, 1, false}, {1, 0, false}, {1, 9, false}}}) {
        refuses_without_mutation(
            *value, [&] { return value->set_driver_frames(root, bad); },
            RuntimeError::invalid_driver_state, "driver bounds");
    }
    const auto absent =
        take(value->remove(root, data.r("rm11")), "absent remove is admitted");
    check(!absent && instance(*value, root).record_balance == 0xffffffffU,
          "absent remove decrements balance modulo32");
    place(*value, root, data, "shape2");
    check(instance(*value, root).record_balance == 0,
          "new place increments wrapped balance back to zero");
    const VmfInstanceHandle arbitrary{0xffffffffU, 0xfffffffeU, 0xfffffffdU};
    check(vmf_instance_handle(vmf_sprite_identity(arbitrary)) == arbitrary,
          "sprite identity roundtrip preserves all three DWORDs");
    check(vmf_instance_handle(vmf_sprite_identity(root)) == root,
          "valid sprite identity roundtrip");
}

void pooled_residue(Profile profile) {
    const auto data = prepare(profile);
    auto limits = small_limits();
    limits.max_timelines = 2;
    limits.max_nodes = 1;
    auto value = runtime(data, limits);
    const auto root = value->root();
    const auto first_node = place(*value, root, data, "a6");
    const auto first_child = child(*value, first_node);
    check(instance(*value, first_child).scope_id ==
                  std::optional<std::uint32_t>{1} &&
              instance(*value, first_child).composed.flags == 0,
          "fresh child scope1 / double compose flags0");
    const auto same_node =
        place(*value, root, data, "styled6", VmfPlaceDisposition::reused_node);
    check(same_node == first_node && child(*value, same_node) == first_child,
          "move-only keeps node and child identities");
    const auto styled = node(*value, first_node);
    const auto expected_flags = profile == Profile::pc_2_8 ? 0x3ffU : 0xffU;
    check(styled.placement_flags == expected_flags && styled.ratio == 12345 &&
              styled.clip_depth == 9 && bounded_name(styled) == "residu",
          "style flags/ratio/clip/name");
    check(styled.local.flags == 3 && styled.composed.flags == 3 &&
              styled.local.matrix_bits == matrix(2, 3, 4, 5, 6, 7),
          "move-only updates matrix and stateful composed flags");
    driver(*value, root, 3, 3);
    remove_found(*value, root, data, "rm6");
    rejects(value->inspect_node(first_node), RuntimeError::invalid_handle,
            "freed node handle invalid");
    rejects(value->inspect_instance(first_child), RuntimeError::invalid_handle,
            "destroyed child handle invalid");
    const auto reused_node = place(*value, root, data, "b4");
    const auto reused_child = child(*value, reused_node);
    const auto now = node(*value, reused_node);
    const auto timeline = instance(*value, reused_child);
    check(reused_node.slot == first_node.slot &&
              reused_node.generation != first_node.generation,
          "node storage reused with a new generation");
    check(reused_child.slot == first_child.slot &&
              reused_child.generation != first_child.generation,
          "timeline storage reused with a new generation");
    check(timeline.character_id == 2 && timeline.birth_frame == 3 &&
              timeline.scope_id == std::optional<std::uint32_t>{2} &&
              timeline.play_state == 2 &&
              timeline.frames == VmfDriverFrames{1, 1, false} &&
              timeline.flags == 7 && timeline.active_nodes == 0 &&
              timeline.cached_children == 0,
          "new logical timeline initialization on old slot");
    check(now.ratio == 12345 && now.clip_depth == 9 &&
              bounded_name(now).empty() && now.placement_flags == 2 &&
              now.local == VmfTransform{},
          "node retains scalar residues but resets active transform/name");
    check(timeline.local == VmfTransform{} && timeline.composed.flags == 3 &&
              now.composed.flags == 3 &&
              timeline.composed.matrix_bits == identity_matrix &&
              timeline.composed.color_words == identity_color,
          "timeline SLOT composed flags survive logical reinitialization");
    check(instance(*value, root).record_balance == 1, "reuse balance");
    place(*value, root, data, "name4", VmfPlaceDisposition::reused_node);
    const auto named = node(*value, reused_node);
    check(bounded_name(named) == "0123456789abcdefghijklmnopqrstu" &&
              named.name[31] == '\0',
          "name copy exactly31 bytes plus NUL");
}

void cache_rebind_rewind(Profile profile) {
    const auto data = prepare(profile);
    auto value = runtime(data);
    const auto root = value->root();
    driver(*value, root, 5, 5);
    const auto original_node = place(*value, root, data, "a4");
    const auto late = child(*value, original_node);
    check(instance(*value, late).birth_frame == 5,
          "child records parent birth frame");
    remove_found(*value, root, data, "rm4");
    const auto detached = instance(*value, late);
    check(detached.cached && !detached.attached_node && detached.depth == 4 &&
              detached.parent == std::optional{root} &&
              detached.scope_id == std::optional<std::uint32_t>{1},
          "same-frame remove caches the SAME logical child");
    rejects(value->inspect_node(original_node), RuntimeError::invalid_handle,
            "cached child does not keep node alive");
    place(*value, root, data, "shape2");
    driver(*value, root, 3, 3);
    const auto rebound_node = place(*value, root, data, "b4");
    check(child(*value, rebound_node) == late,
          "cache reattaches original handle");
    const auto mismatch = instance(*value, late);
    check(node(*value, rebound_node).character_id == 2 &&
              mismatch.character_id == 1 && mismatch.birth_frame == 5 &&
              mismatch.play_state == 1 &&
              mismatch.scope_id == std::optional<std::uint32_t>{1} &&
              mismatch.attached_node == std::optional{rebound_node} &&
              !mismatch.cached && mismatch.composed.flags == 3,
          "node resourceB / live child resourceA are intentionally distinct");
    refuses_without_mutation(
        *value, [&] { return value->place(late, data.p("b_button")); },
        RuntimeError::invalid_record,
        "node resource cannot authorize owner records");
    place(*value, late, data, "a_shape");
    const auto early_node = place(*value, root, data, "a7");
    const auto early = child(*value, early_node);
    check(instance(*value, early).scope_id == std::optional<std::uint32_t>{2} &&
              instance(*value, early).birth_frame == 3,
          "retained child did not consume a new ScopeID");
    driver(*value, root, 6, 4, true);
    succeeds(value->rewind(root), "rewind helper succeeds");
    const auto cleared = instance(*value, root);
    check(cleared.frames == VmfDriverFrames{6, 4, true} &&
              cleared.record_balance == 3 && cleared.active_nodes == 0 &&
              cleared.cached_children == 1,
          "rewind uses requested and does NOT reset frames/balance");
    rejects(value->inspect_instance(late), RuntimeError::invalid_handle,
            "rewind destroys birth5 > target4 and its subtree");
    check(instance(*value, early).cached &&
              !instance(*value, early).attached_node,
          "rewind retains birth3 <= target4");
    driver(*value, root, 3, 4, true);
    const auto replayed = place(*value, root, data, "b7");
    check(child(*value, replayed) == early &&
              node(*value, replayed).character_id == 2 &&
              instance(*value, early).character_id == 1 &&
              instance(*value, early).scope_id ==
                  std::optional<std::uint32_t>{2} &&
              instance(*value, early).composed.flags == 3 &&
              instance(*value, root).record_balance == 4,
          "replay attaches retained scope2 without rebuilding it");
    const auto saved = take(value->snapshot(), "owning snapshot before close");
    const auto saved_copy = saved;
    succeeds(value->close(), "close destroys active and cached context");
    succeeds(value->close(), "close idempotent");
    rejects(value->inspect_instance(root), RuntimeError::closed, "root closed");
    rejects(value->inspect_instance(early), RuntimeError::closed,
            "child closed");
    rejects(value->inspect_node(replayed), RuntimeError::closed, "node closed");
    rejects(value->snapshot(), RuntimeError::closed, "closed snapshot refused");
    rejects(value->place(root, data.p("a4")), RuntimeError::closed,
            "closed place refused");
    rejects(value->rewind(root), RuntimeError::closed, "closed rewind refused");
    value.reset();
    check(same(saved, saved_copy) &&
              saved.program->movie().source_bytes()[0] == std::byte{'V'},
          "snapshot and program survive close and runtime destruction");
}

void overrides(Profile profile) {
    const auto data = prepare(profile);
    struct Case {
        std::uint32_t mask;
        std::array<std::uint32_t, 6> expected;
        std::uint16_t alpha;
    };
    // Hand-fixed component selections from 446E76..446EF0, no production
    // helper is used to calculate these expectations.
    const std::array cases{Case{0, matrix(2, 3, 4, 5, 6, 7), 40},
                           Case{0x20, matrix(2, 3, 4, 5, 6, 7), 123},
                           Case{0x08, matrix(2, 3, 4, 5, 13, 7), 40},
                           Case{0x10, matrix(2, 3, 4, 5, 6, 14), 40},
                           Case{0x40, matrix(9, 3, 11, 5, 6, 7), 40},
                           Case{0x80, matrix(2, 10, 4, 12, 6, 7), 40},
                           Case{0x100, matrix(9, 10, 11, 12, 6, 7), 40},
                           Case{0x138, matrix(9, 10, 11, 12, 13, 14), 123}};
    for (const auto& item : cases) {
        for (std::string_view operation :
             {"char_move", "char_reset", "move_only"}) {
            auto value = runtime(data);
            const auto root = value->root();
            const auto attached = place(*value, root, data, "a4");
            const auto handle = child(*value, attached);
            VmfTransform old;
            old.flags = 0x80;
            old.matrix_bits = matrix(9, 10, 11, 12, 13, 14);
            old.color_words = {99, 98, 97, 123, 9, 8, 7, 6};
            succeeds(value->set_driver_properties(handle, old, 7U | item.mask),
                     "explicit instance overrides published");
            place(*value, root, data, operation,
                  VmfPlaceDisposition::reused_node);
            const auto actual = instance(*value, handle);
            const bool has_character = operation != "move_only";
            check(
                actual.local.matrix_bits ==
                    (has_character ? item.expected : matrix(2, 3, 4, 5, 6, 7)),
                "only character retention performs component restoration");
            const auto alpha = has_character ? item.alpha : std::uint16_t{40};
            check(
                actual.local.color_words ==
                    std::array<std::uint16_t, 8>{10, 20, 30, alpha, 1, 2, 3, 4},
                "alpha override restores multiplier only, not other words");
            check(actual.local.flags ==
                      (operation == "char_reset" ? 3U : 0x83U),
                  "Move keeps old local flags, absent Move resets them");
            check(actual.flags == (7U | item.mask) && actual.birth_frame == 1 &&
                      actual.scope_id == std::optional<std::uint32_t>{1} &&
                      actual.character_id == 1 &&
                      child(*value, attached) == handle,
                  "override placement preserves instance identity/state");
            const auto display = node(*value, attached);
            check(display.local.matrix_bits == matrix(2, 3, 4, 5, 6, 7) &&
                      display.local.color_words ==
                          std::array<std::uint16_t, 8>{10, 20, 30, 40, 1, 2, 3,
                                                       4},
                  "node local does not inherit child's local overrides");
            check(display.composed.matrix_bits == actual.composed.matrix_bits &&
                      display.composed.color_words ==
                          actual.composed.color_words,
                  "node composed properties copy live child output");
        }
    }
}

void comparison_edges(Profile profile) {
    const auto data = prepare(profile);
    for (const std::int32_t birth : {4, 5, 6}) {
        auto value = runtime(data);
        const auto root = value->root();
        driver(*value, root, birth, birth);
        const auto attached = place(*value, root, data, "a4");
        const auto handle = child(*value, attached);
        driver(*value, root, 5, 5);
        remove_found(*value, root, data, "rm4");
        const auto remaining = value->inspect_instance(handle);
        check(remaining.has_value() == (birth >= 5),
              "remove retains equality/newer, destroys older");
        if (remaining) {
            check(remaining->cached && !remaining->attached_node,
                  "remove survivor is detached/cached");
            const auto before = *remaining;
            check(!take(value->remove(root, data.r("rm4")),
                        "second absent remove admitted"),
                  "cached depth is not an active node");
            check(same(before, instance(*value, handle)),
                  "absent active depth does not touch matching cached child");
        }
    }
    for (const std::int32_t birth : {4, 5, 6}) {
        auto value = runtime(data);
        const auto root = value->root();
        driver(*value, root, birth, birth);
        const auto attached = place(*value, root, data, "a4");
        const auto handle = child(*value, attached);
        driver(*value, root, 7, 5, true);
        succeeds(value->rewind(root), "rewind comparison boundary");
        const auto remaining = value->inspect_instance(handle);
        check(remaining.has_value() == (birth <= 5),
              "rewind retains equality/older, destroys newer");
        check(instance(*value, root).record_balance == 1,
              "rewind helper preserves wrapper balance at boundary");
        if (remaining) {
            check(remaining->cached, "rewind survivor is cached");
        }
    }
    // Cached entries are also pruned, before active-list cleanup.
    auto value = runtime(data);
    const auto root = value->root();
    driver(*value, root, 6, 6);
    const auto attached = place(*value, root, data, "a4");
    const auto late = child(*value, attached);
    remove_found(*value, root, data, "rm4");
    driver(*value, root, 7, 5, true);
    succeeds(value->rewind(root), "rewind prunes already-cached future child");
    rejects(value->inspect_instance(late), RuntimeError::invalid_handle,
            "cached birth6 > requested5 destroyed");
    check(instance(*value, root).cached_children == 0,
          "pruned cache count is exact");
}

void buttons_and_isolation(Profile profile) {
    const auto data = prepare(profile);
    auto value = runtime(data);
    const auto root = value->root();
    auto attached = place(*value, root, data, "button5");
    const auto first = child(*value, attached);
    const auto original = instance(*value, first);
    check(original.kind == VmfInstanceKind::button &&
              original.character_id == 4 && !original.scope_id &&
              original.birth_frame == 1 && original.depth == 5 &&
              original.parent == std::optional{root},
          "button has its own kind/resource and no timeline ScopeID");
    refuses_without_mutation(
        *value, [&] { return value->set_driver_frames(first, {1, 1, false}); },
        RuntimeError::wrong_kind, "button is not a frame-driver timeline");
    refuses_without_mutation(
        *value, [&] { return value->set_driver_properties(first, {}, 7); },
        RuntimeError::wrong_kind, "button is not a property-driver timeline");
    remove_found(*value, root, data, "rm5");
    attached = place(*value, root, data, "a5");
    check(child(*value, attached) == first &&
              node(*value, attached).character_id == 1 &&
              instance(*value, first).character_id == 4 &&
              instance(*value, first).kind == VmfInstanceKind::button,
          "retained button may differ from node's new timeline resource");
    driver(*value, root, 2, 2);
    remove_found(*value, root, data, "rm5");
    rejects(value->inspect_instance(first), RuntimeError::invalid_handle,
            "older button destroyed");
    attached = place(*value, root, data, "button5");
    const auto second = child(*value, attached);
    check(first.slot == second.slot && first.generation != second.generation &&
              instance(*value, second).birth_frame == 2,
          "button pool reuse increments handle generation");
    const auto a4 = place(*value, root, data, "a4");
    const auto timeline1 = child(*value, a4);
    const auto a2 = place(*value, root, data, "a2");
    const auto timeline2 = child(*value, a2);
    check(timeline1 != timeline2 &&
              instance(*value, timeline1).scope_id ==
                  std::optional<std::uint32_t>{1} &&
              instance(*value, timeline2).scope_id ==
                  std::optional<std::uint32_t>{2},
          "buttons do not consume timeline IDs; same definition makes distinct "
          "scopes");
    const auto untouched = instance(*value, timeline2);
    driver(*value, timeline1, 2, 1, false);
    place(*value, timeline1, data, "a_shape");
    check(same(untouched, instance(*value, timeline2)),
          "same-definition instances keep independent state and lists");
    const auto snapshot = take(value->snapshot(), "ordered snapshot");
    check(std::is_sorted(snapshot.instances.begin(), snapshot.instances.end(),
                         [](const auto& a, const auto& b) {
                             return a.handle.slot < b.handle.slot;
                         }),
          "snapshot instances ordered by slot");
    check(std::is_sorted(snapshot.nodes.begin(), snapshot.nodes.end(),
                         [](const auto& a, const auto& b) {
                             return a.owner.slot < b.owner.slot ||
                                    (a.owner.slot == b.owner.slot &&
                                     a.depth < b.depth);
                         }),
          "snapshot nodes ordered by owner, then native depth");
    const auto root2 = instance(*value, root);
    check(root2.active_nodes == 3, "root has only its own nodes");
    driver(*value, root, 2, 2);
    remove_found(*value, root, data, "rm4");
    const auto cached = instance(*value, timeline1);
    check(cached.cached && cached.active_nodes == 1 && !cached.attached_node,
          "cached timeline keeps its own active subtree");
    const auto with_cache =
        take(value->snapshot(), "snapshot includes cached subtree");
    check(std::any_of(
              with_cache.nodes.begin(), with_cache.nodes.end(),
              [timeline1](const auto& n) { return n.owner == timeline1; }),
          "snapshot owns nodes belonging to cached timelines");
}

void protections(Profile profile) {
    const auto data = prepare(profile);
    auto value = runtime(data);
    const auto root = value->root();
    auto other = runtime(data);
    const auto foreign = other->root();
    check(root.context != foreign.context,
          "different runtimes have different context IDs");
    refuses_without_mutation(
        *value,
        [&] { return value->set_driver_frames(foreign, {1, 1, false}); },
        RuntimeError::invalid_handle, "foreign owner rejected");
    auto forged = root;
    forged.generation ^= 0x80000000U;
    refuses_without_mutation(
        *value, [&] { return value->place(forged, data.p("a4")); },
        RuntimeError::invalid_handle, "forged generation rejected");
    refuses_without_mutation(
        *value,
        [&] {
            return value->place(root, std::numeric_limits<std::size_t>::max());
        },
        RuntimeError::invalid_record, "out of range placement index");
    refuses_without_mutation(
        *value,
        [&] {
            return value->remove(root, std::numeric_limits<std::size_t>::max());
        },
        RuntimeError::invalid_record, "out of range removal index");
    refuses_without_mutation(
        *value, [&] { return value->place(root, data.p("a_shape")); },
        RuntimeError::invalid_record,
        "nested sprite record cannot be applied to root");
    refuses_without_mutation(
        *value, [&] { return value->place(root, data.p("text3")); },
        RuntimeError::needs_services, "type37 needs real external services");
    VmfTransform changed;
    changed.matrix_bits = matrix(2, 3, 4, 5, 6, 7);
    refuses_without_mutation(
        *value,
        [&] { return value->set_driver_properties(root, changed, 0x207); },
        RuntimeError::needs_services, "dirty variable cleanup is not a no-op");

    auto limits = small_limits();
    limits.max_nodes = 1;
    auto one_node = runtime(data, limits);
    const auto one_root = one_node->root();
    const auto first_node = place(*one_node, one_root, data, "a4");
    const auto first_child = child(*one_node, first_node);
    refuses_without_mutation(
        *one_node, [&] { return one_node->place(one_root, data.p("shape2")); },
        RuntimeError::resource_limit, "node exhaustion is atomic");
    remove_found(*one_node, one_root, data, "rm4");
    place(*one_node, one_root, data, "shape2");
    refuses_without_mutation(
        *one_node, [&] { return one_node->place(one_root, data.p("b4")); },
        RuntimeError::resource_limit,
        "failed node allocation does not extract cached child");
    check(instance(*one_node, first_child).cached,
          "cache preserved on quota refusal");
    remove_found(*one_node, one_root, data, "rm2");
    check(child(*one_node, place(*one_node, one_root, data, "b4")) ==
              first_child,
          "cache can be reattached after quota pressure ends");

    limits = small_limits();
    limits.max_timelines = 2;
    auto one_child = runtime(data, limits);
    const auto child_root = one_child->root();
    const auto attached = place(*one_child, child_root, data, "a4");
    const auto retained = child(*one_child, attached);
    remove_found(*one_child, child_root, data, "rm4");
    refuses_without_mutation(
        *one_child, [&] { return one_child->place(child_root, data.p("a6")); },
        RuntimeError::resource_limit, "cached timeline still consumes a slot");
    const auto reattached = place(*one_child, child_root, data, "b4");
    check(child(*one_child, reattached) == retained,
          "full pool still permits cache identity reuse");
    driver(*one_child, child_root, 2, 2);
    remove_found(*one_child, child_root, data, "rm4");
    const auto replacement =
        child(*one_child, place(*one_child, child_root, data, "a6"));
    check(instance(*one_child, replacement).scope_id ==
              std::optional<std::uint32_t>{2},
          "failed admission did not consume ScopeID");

    limits = small_limits();
    limits.max_buttons = 0;
    auto no_buttons = runtime(data, limits);
    refuses_without_mutation(
        *no_buttons,
        [&] {
            return no_buttons->place(no_buttons->root(), data.p("button5"));
        },
        RuntimeError::resource_limit, "button pool zero is a safe refusal");
    limits.max_nodes = 0;
    limits.max_timelines = 1;
    auto root_only = runtime(data, limits);
    check(instance(*root_only, root_only->root()).active_nodes == 0,
          "zero button/node capacity admits root-only context");
    refuses_without_mutation(
        *root_only,
        [&] { return root_only->place(root_only->root(), data.p("shape2")); },
        RuntimeError::resource_limit, "zero node pool refuses placement");

    limits = small_limits();
    limits.max_parent_depth = 2;
    auto shallow = runtime(data, limits);
    const auto nested =
        child(*shallow, place(*shallow, shallow->root(), data, "self1"));
    refuses_without_mutation(
        *shallow, [&] { return shallow->place(nested, data.p("self1")); },
        RuntimeError::resource_limit,
        "parent depth includes root and prevents recursive resource expansion");
}

void limits_and_snapshots(Profile profile) {
    const auto data = prepare(profile);
    for (const auto bad_depth : {std::size_t{0}, std::size_t{257}}) {
        auto limits = small_limits();
        limits.max_parent_depth = bad_depth;
        rejects(VmfDisplayRuntime::create(data.program, limits),
                RuntimeError::invalid_limits, "invalid parent depth bounds");
    }
    auto limits = small_limits();
    limits.max_timelines = 0;
    rejects(VmfDisplayRuntime::create(data.program, limits),
            RuntimeError::invalid_limits, "root requires timeline capacity");
    limits = small_limits();
    limits.max_storage_bytes = 0;
    rejects(VmfDisplayRuntime::create(data.program, limits),
            RuntimeError::resource_limit,
            "zero storage budget is resource_limit");
    for (bool invalid_nodes : {false, true}) {
        limits = small_limits();
        if (invalid_nodes) {
            limits.max_nodes = std::numeric_limits<std::uint32_t>::max();
        } else {
            limits.max_timelines = std::numeric_limits<std::size_t>::max();
        }
        std::expected<std::unique_ptr<VmfDisplayRuntime>,
                      VmfDisplayRuntimeError>
            result;
        {
            AllocationFailureScope fail(0);
            result = VmfDisplayRuntime::create(data.program, limits);
        }
        rejects(std::move(result), RuntimeError::invalid_limits,
                "unrepresentable counts refused before allocations");
    }
    rejects(VmfDisplayRuntime::create({}), RuntimeError::invalid_limits,
            "null immutable program refused");
    limits = small_limits();
    limits.max_parent_depth = 256;
    check(VmfDisplayRuntime::create(data.program, limits).has_value(),
          "maximum supported parent depth accepted");

    auto value = runtime(data);
    const auto root = value->root();
    place(*value, root, data, "a4");
    place(*value, root, data, "shape9");
    const auto saved = take(value->snapshot(), "snapshot baseline");
    const auto exact_bytes =
        sizeof(VmfDisplaySnapshot) +
        sizeof(VmfInstanceSnapshot) * saved.instances.size() +
        sizeof(VmfNodeSnapshot) * saved.nodes.size();
    check(value->snapshot(exact_bytes).has_value(),
          "exact snapshot quota accepted");
    refuses_without_mutation(
        *value, [&] { return value->snapshot(exact_bytes - 1); },
        RuntimeError::resource_limit, "snapshot one byte short refused");
    auto edited = saved;
    edited.nodes[0].ratio = 0x12345678U;
    edited.nodes[0].name.fill('x');
    edited.instances[0].frames.current = 7;
    check(same(saved, take(value->snapshot(),
                           "unmodified runtime after editing result")),
          "snapshot edits do not alias pool storage");
    VmfTransform root_properties;
    root_properties.flags = 3;
    root_properties.matrix_bits = matrix(1, 1, 0, 0, 10, 20);
    succeeds(value->set_driver_properties(root, root_properties, 7),
             "root transform explicitly published");
    check(instance(*value, root).composed == root_properties,
          "root composed copies root local without XOR composition");
    const auto after = take(value->snapshot(), "propagated transform snapshot");
    for (const auto& item : after.nodes) {
        check(item.composed.matrix_bits == root_properties.matrix_bits,
              "root properties propagate to plain nodes and child instances");
    }
    check(saved.instances.front().local == VmfTransform{} &&
              saved.nodes.front().composed.matrix_bits == identity_matrix,
          "saved owning snapshot does not change after root propagation");

    VmfProgramPtr external;
    {
        auto temporary = prepare(profile);
        external = temporary.program;
    }
    std::weak_ptr<const VmfProgram> weak = external;
    auto holder = take(VmfDisplayRuntime::create(external, small_limits()),
                       "runtime owns program");
    external.reset();
    check(!weak.expired(), "runtime keeps program alive after caller release");
    auto owned = take(holder->snapshot(), "snapshot owns program");
    holder.reset();
    check(!weak.expired() &&
              owned.program->movie().source_bytes()[0] == std::byte{'V'},
          "snapshot retains movie after runtime/input destruction");
    owned.program.reset();
    check(weak.expired(), "program ownership is released with final snapshot");
}

void allocation_failures(Profile profile) {
    const auto input = fixture(profile);
    const auto movie = parse_fixture(input, profile);
    std::size_t program_failures{};
    bool reached_success = false;
    for (std::size_t ordinal = 0; ordinal < 128; ++ordinal) {
        std::expected<VmfProgramPtr, VmfError> result;
        {
            AllocationFailureScope fail(ordinal);
            result = VmfProgram::build(movie);
        }
        if (result) {
            reached_success = true;
            break;
        }
        ++program_failures;
        check(result.error().code == VmfErrorCode::resource_limit &&
                  !result.error().detail.empty(),
              "program allocation failure reported without throwing");
        check(std::equal(movie.source_bytes().begin(),
                         movie.source_bytes().end(), input.bytes.begin(),
                         input.bytes.end()),
              "program OOM preserves input validated movie");
    }
    check(reached_success && program_failures > 0,
          "program OOM sweep reaches success");
    const auto program =
        take(VmfProgram::build(movie), "program recovered after OOM");
    reached_success = false;
    std::size_t creation_failures{};
    for (std::size_t ordinal = 0; ordinal < 64; ++ordinal) {
        std::expected<std::unique_ptr<VmfDisplayRuntime>,
                      VmfDisplayRuntimeError>
            result;
        {
            AllocationFailureScope fail(ordinal);
            result = VmfDisplayRuntime::create(program, small_limits());
        }
        if (result) {
            reached_success = true;
            check(instance(**result, (*result)->root()).scope_id ==
                      std::optional<std::uint32_t>{0},
                  "successful context still starts at root ScopeID0");
            break;
        }
        ++creation_failures;
        check(result.error().code == RuntimeError::allocation_failure &&
                  !result.error().detail.empty(),
              "runtime creation OOM is explicit allocation_failure");
    }
    check(reached_success && creation_failures > 0,
          "runtime creation OOM sweep reaches success");

    auto prepared = prepare(profile);
    auto value = runtime(prepared);
    const auto root = value->root();
    place(*value, root, prepared, "a4");
    const auto before = take(value->snapshot(), "baseline before snapshot OOM");
    reached_success = false;
    std::size_t snapshot_failures{};
    for (std::size_t ordinal = 0; ordinal < 16; ++ordinal) {
        std::expected<VmfDisplaySnapshot, VmfDisplayRuntimeError> result;
        {
            AllocationFailureScope fail(ordinal);
            result = value->snapshot();
        }
        if (result) {
            reached_success = true;
            check(same(before, *result),
                  "successful snapshot after OOM matches baseline");
            break;
        }
        ++snapshot_failures;
        check(result.error().code == RuntimeError::allocation_failure &&
                  !result.error().detail.empty(),
              "snapshot OOM is explicit allocation_failure");
        check(same(before, take(value->snapshot(), "snapshot after failure")),
              "snapshot OOM preserves live runtime");
    }
    check(reached_success && snapshot_failures > 0,
          "snapshot OOM sweep reaches success");
}

#endif
} // namespace

int main(int argc, char** argv) {
    try {
        for (auto profile : profiles) {
            fixture_preflight(profile);
        }
        const bool fixture_only =
            argc == 2 && std::string_view(argv[1]) == "--fixture-only";
#ifndef VMF_RUNTIME_FIXTURE_ONLY
        if (!fixture_only) {
            for (auto profile : profiles) {
                program_contract(profile);
                initial_and_driver(profile);
                pooled_residue(profile);
                cache_rebind_rewind(profile);
                overrides(profile);
                comparison_edges(profile);
                buttons_and_isolation(profile);
                protections(profile);
                limits_and_snapshots(profile);
                allocation_failures(profile);
            }
        }
#else
        (void)fixture_only;
#endif
        std::cout << "VMF display runtime: " << checks << " checks, "
                  << failures << " failures\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FATAL: " << error.what() << '\n';
    }
    return failures == 0 ? 0 : 1;
}

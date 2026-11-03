#include "tdur/ui/vmf_script_context.hpp"

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

#include <clocale>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Test envelope adopted from timeline test SHA
// 235e1151b3371836c7add9ac665ca8561bdb01f99faf41e098beb4bfd4dc5a49. Native
// target/label oracle: vmf-target-execution-tests/ORACLE-v1.md. Frozen before
// new production CPP; no production CPP read.
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

Bytes text_bytes(std::string_view text) {
    Bytes out;
    for (unsigned char value : text)
        out.push_back(octet(value));
    out.push_back(std::byte{});
    return out;
}
Element label(Profile profile, std::string_view text) {
    return tag(profile, 43, text_bytes(text));
}
Element frame_sprite(Profile profile, std::uint32_t id, std::uint16_t capacity,
                     std::vector<std::vector<Element>> frames,
                     bool root = false) {
    const auto metadata = 24U + 4U * frames.size();
    Element out{Bytes(metadata), {}};
    dword(out.bytes, 0, 39, profile);
    dword(out.bytes, 4, narrow(metadata), profile);
    dword(out.bytes, 8, id, profile);
    word(out.bytes, 12, static_cast<std::uint16_t>(frames.size()), profile);
    word(out.bytes, 14, capacity, profile);
    dword(out.bytes, 20, narrow(metadata), profile);
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        for (const auto& element : frames[frame]) {
            const auto at = out.bytes.size();
            append(out.bytes, element.bytes);
            for (const auto& marker : element.markers)
                out.markers.push_back({marker.label, at + marker.offset});
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

Fixture fixture(Profile profile, Bytes actions, std::uint16_t root_frames = 8) {
    std::vector<std::vector<Element>> a_frames(3);
    a_frames[0].push_back(placement(profile, "childC", 1, 0x22, 1,
                                    identity_matrix, identity_color, 0, 0,
                                    "c"));
    a_frames[0].push_back(placement(profile, "childB", 2, 0x22, 2,
                                    identity_matrix, identity_color, 0, 0,
                                    "b"));
    a_frames[0].push_back(placement(profile, "childFoo", 3, 0x22, 1,
                                    identity_matrix, identity_color, 0, 0,
                                    "foo"));
    a_frames[0].push_back(placement(profile, "childLong", 4, 0x22, 1,
                                    identity_matrix, identity_color, 0, 0,
                                    "klmnopqrstKLMNOPQRST"));
    a_frames[0].push_back(label(profile, "child_first"));
    a_frames[0].push_back(label(profile, "shared"));
    a_frames[1].push_back(label(profile, "child_middle"));
    a_frames[2].push_back(label(profile, "child_last"));
    std::vector<std::vector<Element>> frames(root_frames);
    auto& items = frames[0];
    items.push_back(sprite(profile, 2, 1, 64, {label(profile, "one")}));
    items.push_back(frame_sprite(profile, 1, 64, std::move(a_frames)));
    items.push_back(define(profile, 2, 3));
    items.push_back(button(profile));
    items.push_back(define(profile, 37, 5));
    const auto add = [&](std::string_view marker, std::uint16_t depth,
                         std::uint16_t character, std::string_view name) {
        items.push_back(placement(profile, marker, depth, 0x22, character,
                                  identity_matrix, identity_color, 0, 0, name));
    };
    add("baseA", 10, 1, "a");
    add("baseB", 20, 1, "b");
    add("rootFoo", 30, 1, "foo");
    add("literalABFirst", 5, 1, "a/b");
    add("literalABLast", 30, 1, "a/b");
    add("literalColonFirst", 5, 1, "a:b");
    add("literalColonLast", 30, 1, "a:b");
    add("dotName", 15, 1, ".");
    add("emptyName", 0, 1, "");
    add("dupHigh", 40, 1, "dup");
    add("dupLow", 20, 1, "dup");
    add("dupButton", 10, 4, "dup");
    add("dupShape", 5, 3, "dup");
    add("asciiName", 1, 1, "AbC");
    add("atName", 1, 1, "@");
    add("bracketName", 2, 1, "[");
    add("highName", 3, 1, "\x80");
    add("spaceName", 4, 1, " ");
    add("partialSpace", 7, 1, "a ");
    add("longName", 8, 1, "1234567890123456789012345678901XYZ");
    add("longPath", 10, 1, "abcdefghijABCDEFGHIJ");
    add("numericName", 6, 1, "1");
    add("rebindAtoB", 10, 2, "a");
    add("buttonName", 12, 4, "button");
    items.push_back(placement(profile, "style", 50, 0x3fd, 0,
                              matrix(2, 3, 4, 5, 6, 7), identity_color, 42, 53,
                              "style"));
    items.push_back(removal(profile, "rm10", 10));
    items.push_back(removal(profile, "rm20", 20));
    items.push_back(label(profile, "ROOT_FIRST"));
    items.push_back(label(profile, "duplicate"));
    items.push_back(label(profile, ""));
    items.push_back(tag(profile, 12, std::move(actions)));
    frames.at(1).push_back(label(profile, "duplicate"));
    frames.at(1).push_back(label(profile, "shared"));
    frames.at(2).push_back(label(profile, "Camel"));
    frames.at(2).push_back(label(profile, "@"));
    frames.at(2).push_back(label(profile, "["));
    frames.at(2).push_back(label(profile, "\x80"));
    frames.at(2).push_back(label(profile, std::string(63, 'A')));
    frames.at(2).push_back(label(profile, std::string(63, 'B') + "X"));
    frames.at(2).push_back(label(profile, std::string(63, 'C') + "XY"));
    frames.back().push_back(label(profile, "last"));
    auto root = frame_sprite(profile, 0, 64, std::move(frames), true);
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

    word(out.bytes, 64, 64, profile);

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

using Action = VmfTimelineAction;
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

                                          {32, 8, 128, 16, 2U * 1024U * 1024U}),

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

// New independent target tests follow.

static_assert(!std::is_copy_constructible_v<VmfScriptContext>);
static_assert(std::is_nothrow_move_constructible_v<VmfScriptContext>);
static_assert(std::is_nothrow_move_assignable_v<VmfScriptContext>);

void text_op(Bytes& out, Profile profile, std::uint16_t code,
             std::string_view text) {
    auto payload = text_bytes(text);
    if (code == 0x96)
        payload.insert(payload.begin(), std::byte{}); // Push type0 string.
    op(out, profile, code, std::move(payload));
}
Bytes end_only(Profile profile) {
    Bytes out;
    op(out, profile, 0);
    return out;
}
VmfNodeHandle place_on(VmfDisplayRuntime& display, const Prepared& data,
                       VmfInstanceHandle owner, std::string_view marker) {
    const auto result = take(display.place(owner, data.p(marker)),
                             "named source placement on explicit owner");
    check(result.node.has_value(), "explicit-owner node exists");
    if (!result.node)
        throw std::runtime_error("missing explicit-owner node");
    return *result.node;
}
void resolve_is(const VmfDisplayRuntime& display, VmfInstanceHandle origin,
                std::string_view path,
                std::optional<VmfInstanceHandle> expected,
                VmfTargetLookupLimits limits = {}) {
    const auto before = snapshot(display);
    const auto answer = take(display.resolve_target(origin, path, limits),
                             "target resolution returns ordinary result");
    check(answer.found == expected.has_value() && answer.target == expected,
          "exact resolved identity or explicit native miss");
    check(same(before, snapshot(display)),
          "const lookup preserves every public instance/node field");
}
void resolve_error(const VmfDisplayRuntime& display, VmfInstanceHandle origin,
                   std::string_view path, RuntimeError code,
                   VmfTargetLookupLimits limits = {}) {
    const auto before = snapshot(display);
    const auto result = display.resolve_target(origin, path, limits);
    check(!result && result.error().code == code,
          "modern lookup guard has the declared error code");
    check(same(before, snapshot(display)),
          "lookup error has no display side effect");
}
VmfScriptContext context(VmfDisplayRuntime& display, const Prepared& data,
                         VmfInstanceHandle origin,
                         VmfTargetLookupLimits limits = {}) {
    return take(VmfScriptContext::create(display, origin, data.script, limits),
                "persistent script context created");
}
bool same(const VmfScriptSnapshot& a, const VmfScriptSnapshot& b) {
    return a.origin == b.origin && a.current == b.current &&
           a.phase == b.phase &&
           a.cursor.script_index == b.cursor.script_index &&
           a.cursor.next_action == b.cursor.next_action &&
           a.cursor.ended == b.cursor.ended &&
           a.cursor.faulted == b.cursor.faulted;
}
void state_is(const VmfScriptContext& ctx, std::size_t script,
              std::size_t action, VmfScriptPhase phase,
              VmfInstanceHandle origin,
              std::optional<VmfInstanceHandle> current) {
    const auto value = ctx.snapshot();
    check(value.cursor.script_index == script &&
              value.cursor.next_action == action && value.phase == phase &&
              value.origin == origin && value.current == current &&
              value.cursor.ended == (phase == VmfScriptPhase::ended) &&
              value.cursor.faulted == (phase == VmfScriptPhase::faulted),
          "full script snapshot: origin/current/cursor/phase");
}
void report_is(const VmfExecutionReport& value, VmfExecutionStatus status,
               std::size_t instructions, std::size_t text, std::size_t skipped,
               std::uint16_t next) {
    check(value.status == status &&
              value.instructions_executed == instructions &&
              value.text_bytes_consumed == text &&
              value.actions_skipped == skipped && value.next_opcode == next,
          "complete report: execution/text/scan budgets and next opcode");
}
VmfExecutionReport run(VmfDisplayRuntime& display, VmfScriptContext& ctx,
                       VmfStack& operands, VmfExecutionLimits limits = {}) {
    return take(execute_vmf_actions(display, ctx, operands, limits),
                "persistent executor returns report");
}
void seed_stack(VmfStack& operands) {
    check(operands.push_number_bits(0x80000000U).has_value(),
          "lower negative-zero sentinel");
    check(operands.push_text("lower-text").has_value(),
          "lower owned-text sentinel");
    check(operands.push_number_bits(0x7fc12345U).has_value(),
          "top NaN-bit sentinel");
}
void drain_sentinels(VmfStack& operands) {
    const auto a = operands.pop_value();
    const auto b = operands.pop_value();
    const auto c = operands.pop_value();
    check(a && a->kind() == VmfValueKind::number &&
              a->number_bits() == 0x7fc12345U,
          "entire stack retains top number payload");
    check(b && b->kind() == VmfValueKind::owned_text && b->text_reference() &&
              b->text_reference()->text() == "lower-text",
          "entire stack retains lower text and kind");
    check(c && c->kind() == VmfValueKind::number &&
              c->number_bits() == 0x80000000U && operands.size() == 0,
          "entire stack retains lower negative-zero payload");
}

void fixture_preflight(Profile profile) {
    Bytes actions;
    text_op(actions, profile, 0x8b, "a");
    text_op(actions, profile, 0x8c, "last");
    op(actions, profile, 0);
    const auto data = prepare(profile, std::move(actions));
    const auto& movie = data.program->movie();
    const auto& decoded = data.program->display();
    check(movie.definitions().size() == 6 && movie.sprites().size() == 3 &&
              movie.sprites()[1].character_id == 2 &&
              movie.sprites()[2].character_id == 1,
          "dense IDs differ from physical sprite order");
    check(movie.labels().size() == 18,
          "18 labels include duplicates/nested/63/64/65/empty/high-byte");
    check(decoded.placements().size() == 29 && decoded.removals().size() == 2,
          "named placement/remove envelope counts");
    const auto& script = movie.scripts()[data.script];
    check(data.script != 0 && movie.scripts().size() == 2 &&
              script.kind == VmfScriptKind::timeline &&
              script.parent_sprite_index == 0 && script.action_count == 3,
          "root script selected explicitly rather than button-condition0");
    check(movie.actions()[script.first_action].opcode == 0x8b &&
              movie.actions()[script.first_action].encoded_size == 6 &&
              movie.actions()[script.first_action + 1].opcode == 0x8c &&
              movie.actions()[script.first_action + 1].compact_offset == 5,
          "target text action has distinct compact and physical offsets");
    for (const auto& marker : data.input.markers) {
        check((marker.label.starts_with("rm")
                   ? removal_index(data.input, decoded, marker.label)
                   : placement_index(data.input, decoded, marker.label)) <
                  (marker.label.starts_with("rm")
                       ? decoded.removals().size()
                       : decoded.placements().size()),
              "every fixture marker resolves to the appropriate decoded table");
    }
    const auto& style = decoded.placements()[data.p("style")];
    check(style.flags == (profile == Profile::pc_2_8 ? 0x3fdU : 0xfdU) &&
              style.matrix_bits == matrix(2, 3, 4, 5, 6, 7),
          "profile-specific style flags preserve numeric matrix fixture");
}

void path_basics(Profile profile) {
    const auto data = prepare(profile, end_only(profile));
    auto display = runtime(data);
    const auto root = display->root();
    const auto a = child(*display, place(*display, data, "baseA"));
    const auto b = child(*display, place(*display, data, "baseB"));
    const auto c = child(*display, place_on(*display, data, a, "childC"));
    const auto child_b = child(*display, place_on(*display, data, a, "childB"));
    const auto foo = child(*display, place(*display, data, "rootFoo"));
    const auto dot = child(*display, place(*display, data, "dotName"));
    resolve_is(*display, c, "", c);
    resolve_is(*display, c, ":ignored/suffix", c);
    resolve_is(*display, c, "/", root);
    resolve_is(*display, c, "/:ignored", root);
    resolve_is(*display, c, "..", a);
    resolve_is(*display, c, "../b", child_b);
    resolve_is(*display, a, "..foo", foo);
    resolve_is(*display, root, ".", dot);
    resolve_is(*display, c, "/a/c", c);
    resolve_is(*display, root, "a/", a);
    resolve_is(*display, root, "a:ignored/b", a);
    resolve_is(*display, root, "b", b);
    resolve_is(*display, root, "ab", {});
    resolve_is(*display, root, "a/absent", {});
    resolve_is(*display, root, "..", {});
    resolve_is(*display, c, "../../..", {});
    resolve_is(*display, c, "/..", {});
    resolve_is(*display, root, "absent", {});
    resolve_is(*display, root, "", root, {0, 0});
    resolve_is(*display, root, "/", root, {1, 0});
    resolve_error(*display, root, "..", RuntimeError::resource_limit, {2, 0});
    resolve_is(*display, root, "..", {}, {2, 1});
    resolve_error(*display, c, "/", RuntimeError::resource_limit, {1, 1});
    resolve_is(*display, c, "/", root, {1, 2});
    resolve_error(*display, root, "b", RuntimeError::resource_limit, {1, 2});
    // root's active list is a10,dot15,b20,foo30: rejected names still count.
    resolve_is(*display, root, "b", b, {1, 3});
    resolve_error(*display, root, "a", RuntimeError::resource_limit, {0, 5});
    resolve_is(*display, root, "a", a, {1, 1});
    resolve_error(*display, root, ":ignored", RuntimeError::resource_limit,
                  {7, 0});
    resolve_is(*display, root, ":ignored", root, {8, 0});
    const std::string embedded("a\0/absent-and-long-suffix", 25);
    resolve_is(*display, root, embedded, a, {1, 1});
    const std::array<char, 1> no_nul{'a'};
    resolve_is(*display, root, std::string_view(no_nul.data(), no_nul.size()),
               a, {1, 1});
}

void path_order_and_bytes(Profile profile) {
    const auto data = prepare(profile, end_only(profile));
    for (bool literal_first : {false, true}) {
        auto display = runtime(data);
        const auto a = child(*display, place(*display, data, "baseA"));
        const auto literal =
            child(*display,
                  place(*display, data,
                        literal_first ? "literalABFirst" : "literalABLast"));
        resolve_is(*display, display->root(), "a/b",
                   literal_first ? std::optional{literal} : std::nullopt);
        check(a != literal, "literal and partial-match children are distinct");
    }
    for (bool literal_first : {false, true}) {
        auto display = runtime(data);
        const auto a = child(*display, place(*display, data, "baseA"));
        const auto literal =
            child(*display, place(*display, data,
                                  literal_first ? "literalColonFirst"
                                                : "literalColonLast"));
        resolve_is(*display, display->root(), "a:b",
                   literal_first ? literal : a);
    }
    {
        auto display = runtime(data);
        const auto high = child(*display, place(*display, data, "dupHigh"));
        place(*display, data, "dupButton");
        const auto low = child(*display, place(*display, data, "dupLow"));
        place(*display, data, "dupShape");
        check(low != high, "same-name instances remain distinct");
        resolve_error(*display, display->root(), "dup",
                      RuntimeError::resource_limit, {3, 2});
        resolve_is(*display, display->root(), "dup", low, {3, 3});
    }
    {
        auto display = runtime(data);
        const auto empty = child(*display, place(*display, data, "emptyName"));
        resolve_is(*display, display->root(), "//", empty);
        resolve_is(*display, display->root(), "/", display->root());
        resolve_is(*display, display->root(), "///", {});
    }
    for (const auto& [marker, query] :
         std::array<std::pair<std::string_view, std::string_view>, 4>{
             {{"asciiName", "aBc"},
              {"atName", "`"},
              {"bracketName", "{"},
              {"highName", "\xa0"}}}) {
        auto display = runtime(data);
        const auto target = child(*display, place(*display, data, marker));
        resolve_is(*display, display->root(), query, target);
    }
    {
        auto display = runtime(data);
        const auto space = child(*display, place(*display, data, "spaceName"));
        resolve_is(*display, display->root(), "", display->root(), {0, 0});
        resolve_is(*display, display->root(), " ", space);
    }
    {
        auto display = runtime(data);
        place(*display, data, "partialSpace");
        resolve_error(*display, display->root(), "a",
                      RuntimeError::invalid_native_state, {1, 1});
    }
}

void cache_long_and_guards(Profile profile) {
    const auto data = prepare(profile, end_only(profile));
    {
        auto display = runtime(data);
        const auto root = display->root();
        const auto a = child(*display, place(*display, data, "baseA"));
        const auto c = child(*display, place_on(*display, data, a, "childC"));
        check(take(display->remove(root, data.r("rm10")), "detach A to cache"),
              "removal found A");
        check(instance(*display, a).cached && !instance(*display, c).cached,
              "cached parent retains its own active subtree");
        resolve_is(*display, root, "a", {});
        resolve_is(*display, root, "a/c", {});
        resolve_is(*display, a, "", a);
        resolve_is(*display, a, "c", c);
        resolve_is(*display, c, "/", root);
        resolve_is(*display, a, "..", root);
    }
    {
        auto display = runtime(data);
        const auto a = child(*display, place(*display, data, "longPath"));
        const auto c =
            child(*display, place_on(*display, data, a, "childLong"));
        constexpr std::string_view long_path =
            "abcdefghijABCDEFGHIJ/klmnopqrstKLMNOPQRST";
        static_assert(long_path.size() == 41);
        resolve_is(*display, display->root(), long_path, c, {41, 2});
        resolve_error(*display, display->root(), long_path,
                      RuntimeError::resource_limit, {40, 2});
    }
    {
        auto display = runtime(data);
        const auto placed = place(*display, data, "longName");
        const auto target = child(*display, placed);
        const auto actual = node(*display, placed);
        constexpr std::string_view name31 = "1234567890123456789012345678901";
        check(std::string_view(actual.name.data()) == name31 &&
                  actual.name[31] == '\0',
              "node stores exactly31 bytes, not full direct operand");
        resolve_is(*display, display->root(), name31, target);
        resolve_is(*display, display->root(), std::string(name31) + "X", {});
    }
    {
        auto display = runtime(data);
        auto foreign = runtime(data);
        const auto root = display->root();
        const auto a = child(*display, place(*display, data, "baseA"));
        const auto button =
            child(*display, place(*display, data, "buttonName"));
        resolve_error(*display, {}, "", RuntimeError::invalid_handle);
        resolve_error(*display, foreign->root(), "",
                      RuntimeError::invalid_handle);
        resolve_error(*display, button, "", RuntimeError::wrong_kind);
        succeeds(display->set_driver_frames(root, {2, 2, false}),
                 "advance parent driver solely for destruction");
        check(take(display->remove(root, data.r("rm10")), "destroy older A"),
              "older A was found");
        resolve_error(*display, a, "", RuntimeError::invalid_handle);
        const auto owned_program = display->program();
        succeeds(display->close(), "close guarded display");
        const auto closed = display->resolve_target(root, "");
        check(!closed && closed.error().code == RuntimeError::closed &&
                  display->program() == owned_program,
              "closed lookup refuses without losing owning program");
    }
}

class CTypeScope {
  public:
    CTypeScope() {
        const char* previous = std::setlocale(LC_CTYPE, nullptr);
        if (!previous)
            throw std::runtime_error("query LC_CTYPE failed");
        previous_ = previous;
        if (!std::setlocale(LC_CTYPE, "C"))
            throw std::runtime_error("set fixture LC_CTYPE C failed");
    }
    ~CTypeScope() noexcept { std::setlocale(LC_CTYPE, previous_.c_str()); }
    CTypeScope(const CTypeScope&) = delete;
    CTypeScope& operator=(const CTypeScope&) = delete;

  private:
    std::string previous_;
};

void label_is(const VmfDisplayRuntime& display, VmfInstanceHandle target,
              std::string_view text, std::optional<std::uint32_t> expected) {
    const auto before = snapshot(display);
    const auto answer = take(display.find_label(target, text), "find label");
    check(answer == expected, "exact first label base1 frame or absence");
    check(same(before, snapshot(display)),
          "find_label restores all visible scan/driver state");
}
void goto_is(VmfDisplayRuntime& display, VmfInstanceHandle target,
             std::string_view text, bool found, VmfDriverFrames expected) {
    const auto before = snapshot(display);
    const auto answer =
        take(display.apply_goto_label(target, text), "apply GotoLabel");
    check(answer == found, "GotoLabel distinguishes match from absence");
    check(same(changed(before, target, expected, 2), snapshot(display)),
          "GotoLabel changes only requested/pending/play, never current");
}
void labels(Profile profile) {
    CTypeScope c_locale;
    const auto data = prepare(profile, end_only(profile));
    auto display = runtime(data);
    const auto root = display->root();
    seed(*display, root, {4, 7, true}, 1);
    label_is(*display, root, "ROOT_FIRST", 1U);
    label_is(*display, root, "root_first", 1U);
    label_is(*display, root, "duplicate", 1U);
    label_is(*display, root, "shared", 2U);
    label_is(*display, root, "last", 8U);
    label_is(*display, root, "", 1U);
    label_is(*display, root, "child_first", {});
    label_is(*display, root, "one", {});
    label_is(*display, root, "absent", {});
    label_is(*display, root, "cAMEL", 3U);
    label_is(*display, root, "`", {}); // DF would wrongly match '@'.
    label_is(*display, root, "{", {}); // DF would wrongly match '['.
    label_is(*display, root, "\x80", 3U);
    label_is(*display, root, "\xa0", {}); // Explicit test CRT C locale only.
    label_is(*display, root, std::string(63, 'A'), 3U);
    label_is(*display, root, std::string(63, 'a') + "different-tail", 3U);
    label_is(*display, root, std::string(62, 'A') + "Z", {});
    label_is(*display, root, std::string(63, 'B') + "Y", 3U);
    label_is(*display, root, std::string(63, 'C') + "ZZ", 3U);
    label_is(*display, root, std::string("last\0ignored", 12), 8U);
    const std::array<char, 4> no_nul{'l', 'a', 's', 't'};
    label_is(*display, root, std::string_view(no_nul.data(), no_nul.size()),
             8U);
    goto_is(*display, root, "absent", false, {4, 7, true});
    seed(*display, root, {4, 2, false}, 1);
    goto_is(*display, root, "absent", false, {4, 2, false});
    goto_is(*display, root, "last", true, {4, 8, true});
    seed(*display, root, {2, 8, true}, 1);
    goto_is(*display, root, "shared", true, {2, 2, false});

    const auto a_node = place(*display, data, "baseA");
    const auto a = child(*display, a_node);
    // Parent current2 gives birth2. Replacing at the same driver frame keeps
    // this child despite the node's new character2 (a one-frame resource).
    const auto kept_node = place(*display, data, "rebindAtoB");
    check(kept_node == a_node && child(*display, kept_node) == a &&
              node(*display, kept_node).character_id == 2 &&
              instance(*display, a).character_id == 1,
          "node resource differs from retained child's own resource");
    seed(*display, a, {2, 1, false}, 1);
    label_is(*display, a, "shared", 1U);
    label_is(*display, a, "child_last", 3U);
    label_is(*display, a, "last", {});
    goto_is(*display, a, "child_last", true, {2, 3, true});
    check(take(display->remove(root, data.r("rm10")), "cache label target"),
          "label target removed at birth frame");
    check(instance(*display, a).cached, "label target is really cached");
    seed(*display, a, {3, 1, true}, 1);
    goto_is(*display, a, "child_last", true, {3, 3, false});
    check(instance(*display, a).cached,
          "label update does not activate cached timeline");
    const auto button = child(*display, place(*display, data, "buttonName"));
    const auto before = snapshot(*display);
    const auto bad_find = display->find_label(button, "one");
    const auto bad_goto = display->apply_goto_label(button, "one");
    const auto null_find = display->find_label({}, "");
    const auto null_goto = display->apply_goto_label({}, "");
    check(!bad_find && !bad_goto &&
              bad_find.error().code == RuntimeError::wrong_kind &&
              bad_goto.error().code == RuntimeError::wrong_kind && !null_find &&
              !null_goto &&
              null_find.error().code == RuntimeError::invalid_handle &&
              null_goto.error().code == RuntimeError::invalid_handle &&
              same(before, snapshot(*display)),
          "label guard errors preserve whole display");
    succeeds(display->close(), "close label display");
    const auto closed_find = display->find_label(root, "last");
    const auto closed_goto = display->apply_goto_label(root, "last");
    check(!closed_find && !closed_goto &&
              closed_find.error().code == RuntimeError::closed &&
              closed_goto.error().code == RuntimeError::closed,
          "closed label lookup/mutation are refused");
}

void selected_targets_and_labels(Profile profile) {
    Bytes actions;
    text_op(actions, profile, 0x8b, "a");
    op(actions, profile, 4);
    text_op(actions, profile, 0x8b, "b");
    op(actions, profile, 7);
    text_op(actions, profile, 0x8b, "");
    op(actions, profile, 6);
    op(actions, profile, 0);
    const auto data = prepare(profile, std::move(actions));
    auto display = runtime(data);
    const auto root = display->root();
    const auto a = child(*display, place(*display, data, "baseA"));
    const auto b = child(*display, place(*display, data, "baseB"));
    const auto child_b = child(*display, place_on(*display, data, a, "childB"));
    seed(*display, root, {4, 8, true}, 2);
    seed(*display, a, {1, 3, false}, 2);
    seed(*display, b, {3, 1, true}, 1);
    auto expected = snapshot(*display);
    auto operands = stack();
    seed_stack(operands);
    const auto initial_stack = stack_state(operands);
    auto ctx = context(*display, data, root);
    state_is(ctx, data.script, 0, VmfScriptPhase::execute, root, root);
    report_is(run(*display, ctx, operands, {2, 1024, 1024}),
              VmfExecutionStatus::instruction_limit, 2, 1, 0, 0x8b);
    state_is(ctx, data.script, 2, VmfScriptPhase::execute, root, a);
    expected = changed(std::move(expected), a, {1, 2, true}, 2);
    check(same(expected, snapshot(*display)), "Next acts on selected A only");
    report_is(run(*display, ctx, operands, {2, 1024, 1024}),
              VmfExecutionStatus::instruction_limit, 2, 1, 0, 0x8b);
    state_is(ctx, data.script, 4, VmfScriptPhase::execute, root, b);
    expected = changed(std::move(expected), b, {3, 1, true}, 2);
    check(same(expected, snapshot(*display)) &&
              instance(*display, child_b).play_state == 2,
          "second relative target starts at origin, not A's one-frame B");
    report_is(run(*display, ctx, operands, {1, 1024, 1024}),
              VmfExecutionStatus::instruction_limit, 1, 0, 0, 6);
    state_is(ctx, data.script, 5, VmfScriptPhase::execute, root, root);
    report_is(run(*display, ctx, operands), VmfExecutionStatus::ended, 2, 0, 0,
              0);
    expected = changed(std::move(expected), root, {4, 8, true}, 1);
    check(
        same(expected, snapshot(*display)) &&
            stack_state(operands) == initial_stack,
        "End restores origin without undoing target effects or touching stack");
    state_is(ctx, data.script, 6, VmfScriptPhase::ended, root, root);
    drain_sentinels(operands);

    for (bool cached : {false, true}) {
        Bytes commands;
        text_op(commands, profile, 0x8b, "");
        text_op(commands, profile, 0x8c, "child_last");
        op(commands, profile, 0);
        const auto target_data = prepare(profile, std::move(commands));
        auto target_display = runtime(target_data);
        const auto parent = target_display->root();
        const auto target = child(*target_display,
                                  place(*target_display, target_data, "baseA"));
        place(*target_display, target_data, "rebindAtoB");
        if (cached)
            check(take(target_display->remove(parent, target_data.r("rm10")),
                       "detach own-resource label target"),
                  "target cached");
        seed(*target_display, target, {2, 1, false}, 1);
        const auto before = snapshot(*target_display);
        auto values = stack();
        auto target_context = context(*target_display, target_data, target);
        report_is(run(*target_display, target_context, values),
                  VmfExecutionStatus::ended, 3, 10, 0, 0);
        check(same(changed(before, target, {2, 3, true}, 2),
                   snapshot(*target_display)) &&
                  instance(*target_display, target).cached == cached,
              "root script can label own-resource child/cached caller without "
              "tick");
        state_is(target_context, target_data.script, 2, VmfScriptPhase::ended,
                 target, target);
    }
}

void skip_budgets(Profile profile) {
    Bytes actions;
    text_op(actions, profile, 0x8b, "missing");
    text_op(actions, profile, 0x96, "never");
    op(actions, profile, 0x17); // Pop would consume sentinel if executed.
    text_op(actions, profile, 0x8b, "");
    op(actions, profile, 7);
    op(actions, profile, 0);
    const auto data = prepare(profile, std::move(actions));
    auto display = runtime(data);
    const auto root = display->root();
    seed(*display, root, {4, 8, true}, 1);
    const auto before = snapshot(*display);
    auto operands = stack();
    seed_stack(operands);
    const auto values = stack_state(operands);
    auto ctx = context(*display, data, root);
    const auto initial_context = ctx.snapshot();
    report_is(run(*display, ctx, operands, {0, 1024, 0}),
              VmfExecutionStatus::instruction_limit, 0, 0, 0, 0x8b);
    check(same(ctx.snapshot(), initial_context),
          "zero execution quota has no hidden selection reset/scan");
    report_is(run(*display, ctx, operands, {1, 1024, 1}),
              VmfExecutionStatus::scan_limit, 1, 7, 1, 0x17);
    state_is(ctx, data.script, 2, VmfScriptPhase::skip_to_target, root, {});
    check(same(before, snapshot(*display)) && stack_state(operands) == values,
          "failed target and first skipped action have no scalar effects");
    report_is(run(*display, ctx, operands, {0, 1024, 1}),
              VmfExecutionStatus::instruction_limit, 0, 0, 1, 0x8b);
    state_is(ctx, data.script, 3, VmfScriptPhase::execute, root, {});
    report_is(run(*display, ctx, operands, {1, 1024, 0}),
              VmfExecutionStatus::instruction_limit, 1, 0, 0, 7);
    state_is(ctx, data.script, 4, VmfScriptPhase::execute, root, root);
    report_is(run(*display, ctx, operands, {1, 1024, 0}),
              VmfExecutionStatus::instruction_limit, 1, 0, 0, 0);
    check(same(changed(before, root, {4, 8, true}, 2), snapshot(*display)),
          "only executed Stop changes the display");
    report_is(run(*display, ctx, operands, {1, 1024, 0}),
              VmfExecutionStatus::ended, 1, 0, 0, 0);
    state_is(ctx, data.script, 5, VmfScriptPhase::ended, root, root);
    const auto ended = ctx.snapshot();
    report_is(run(*display, ctx, operands), VmfExecutionStatus::ended, 0, 0, 0,
              0);
    check(same(ctx.snapshot(), ended) && stack_state(operands) == values,
          "ended persistent context neither rescans nor replays pops");
    drain_sentinels(operands);

    auto whole_display = runtime(data);
    const auto whole_root = whole_display->root();
    seed(*whole_display, whole_root, {4, 8, true}, 1);
    const auto whole_before = snapshot(*whole_display);
    auto whole_values = stack();
    seed_stack(whole_values);
    auto whole_context = context(*whole_display, data, whole_root);
    report_is(run(*whole_display, whole_context, whole_values),
              VmfExecutionStatus::ended, 4, 7, 2, 0);
    check(same(changed(whole_before, whole_root, {4, 8, true}, 2),
               snapshot(*whole_display)),
          "uninterrupted execution has exactly the same symbolic final state");
    drain_sentinels(whole_values);
}

void skipped_control_and_boundaries(Profile profile) {
    Bytes actions;
    text_op(actions, profile, 0x8b, "a/absent");
    Bytes self_jump(2);
    word(self_jump, 0, 0xfffbU, profile); // Compact -5: Jump to itself.
    op(actions, profile, 0x99, self_jump);
    op(actions, profile, 0x9d, Bytes(2)); // If next instruction, would pop.
    op(actions, profile, 0x17);
    text_op(actions, profile, 0x96, "not-written");
    text_op(actions, profile, 0x8c, "last");
    op(actions, profile, 7);
    op(actions, profile, 0x1c); // GetVariable remains a host boundary.
    op(actions, profile, 0x20); // Stop BEFORE this TellTarget boundary.
    op(actions, profile, 7);
    op(actions, profile, 0);
    const auto data = prepare(profile, std::move(actions));
    auto display = runtime(data);
    const auto root = display->root();
    const auto a = child(*display, place(*display, data, "baseA"));
    seed(*display, root, {4, 8, true}, 1);
    seed(*display, a, {2, 1, true}, 1);
    const auto before = snapshot(*display);
    auto operands = stack();
    check(operands.push_text("").has_value(), "TellTarget boundary operand");
    const auto values = stack_state(operands);
    auto ctx = context(*display, data, root);
    report_is(run(*display, ctx, operands, {1, 1024, 1024}),
              VmfExecutionStatus::instruction_limit, 1, 8, 7, 0x20);
    state_is(ctx, data.script, 8, VmfScriptPhase::execute, root, {});
    check(same(before, snapshot(*display)) && stack_state(operands) == values,
          "linear skip ignores self Jump, If, Pop, Push, label, Stop and host");
    report_is(run(*display, ctx, operands), VmfExecutionStatus::ended, 3, 0, 0,
              0);
    check(operands.size() == 0 &&
              same(changed(before, root, {4, 8, true}, 2), snapshot(*display)),
          "recovered TellTarget consumes once, resets origin, then executes "
          "Stop");
    state_is(ctx, data.script, 10, VmfScriptPhase::ended, root, root);

    for (bool immediate_end : {false, true}) {
        Bytes tail;
        text_op(tail, profile, 0x8b, "absent");
        if (!immediate_end) {
            op(tail, profile, 7);
            text_op(tail, profile, 0x96, "ignored");
        }
        op(tail, profile, 0);
        const auto tail_data = prepare(profile, std::move(tail));
        auto tail_display = runtime(tail_data);
        auto tail_values = stack();
        auto tail_context =
            context(*tail_display, tail_data, tail_display->root());
        const auto tail_before = snapshot(*tail_display);
        report_is(
            run(*tail_display, tail_context, tail_values, {1, 1024, 1024}),
            VmfExecutionStatus::instruction_limit, 1, 6,
            immediate_end ? 0U : 2U, 0);
        state_is(tail_context, tail_data.script, immediate_end ? 1U : 3U,
                 VmfScriptPhase::execute, tail_display->root(), {});
        report_is(run(*tail_display, tail_context, tail_values),
                  VmfExecutionStatus::ended, 1, 0, 0, 0);
        check(same(tail_before, snapshot(*tail_display)) &&
                  tail_values.size() == 0 &&
                  tail_context.snapshot().current == tail_display->root(),
              "End after target miss restores origin without executing skipped "
              "Stop");
    }
}

void telltarget_sources(Profile profile) {
    for (int source_kind = 0; source_kind != 4; ++source_kind) {
        Bytes actions;
        op(actions, profile, 0x20);
        op(actions, profile, 7);
        op(actions, profile, 0);
        const auto data = prepare(profile, std::move(actions));
        auto display = runtime(data);
        const auto root = display->root();
        auto target = root;
        if (source_kind == 0) {
            target = child(*display, place(*display, data, "baseA"));
        } else if (source_kind == 3) {
            target = child(*display, place(*display, data, "numericName"));
        } else {
            const auto a = child(*display, place(*display, data, "longPath"));
            target = child(*display, place_on(*display, data, a, "childLong"));
        }
        seed(*display, target, {2, 1, true}, 1);
        const auto before = snapshot(*display);
        auto operands = stack();
        seed_stack(operands);
        if (source_kind == 0) {
            check(operands.push_text("a").has_value(),
                  "owned TellTarget source");
        } else if (source_kind == 1) {
            // Deliberately destroy the only external source before execution.
            // The pop result must retain ownership THROUGH the lookup.
            const auto temporary =
                take(VmfLinkedText::copy(
                         "abcdefghijABCDEFGHIJ/klmnopqrstKLMNOPQRST"),
                     "temporary full linked path");
            check(operands.push_linked(temporary).has_value(),
                  "stack owns last source reference after block ends");
        } else if (source_kind == 2) {
            check(
                operands.push_text("abcdefghijABCDEFGHIJ/klmnopqrstKLMNOPQRST")
                    .has_value(),
                "owned path is intentionally truncated31 by Push semantics");
        } else {
            check(operands.push_number(1).has_value(),
                  "numeric TellTarget source");
        }
        auto ctx = context(*display, data, root);
        const bool native_miss = source_kind == 2;
        report_is(run(*display, ctx, operands), VmfExecutionStatus::ended,
                  native_miss ? 2U : 3U,
                  source_kind == 1   ? 41U
                  : source_kind == 2 ? 31U
                                     : 1U,
                  native_miss ? 1U : 0U, 0);
        check(same(native_miss ? before
                               : changed(before, target, {2, 1, true}, 2),
                   snapshot(*display)),
              "source kinds preserve full link vs Push31 and route Stop only "
              "on success");
        state_is(ctx, data.script, 2, VmfScriptPhase::ended, root, root);
        drain_sentinels(operands);
    }
    // Also exercise the last-owner lifetime through a normal path miss.
    {
        Bytes actions;
        op(actions, profile, 0x20);
        op(actions, profile, 7);
        op(actions, profile, 0);
        const auto data = prepare(profile, std::move(actions));
        auto display = runtime(data);
        auto operands = stack();
        {
            const auto source = take(VmfLinkedText::copy(std::string(512, 'z')),
                                     "long temporary miss source");
            check(operands.push_linked(source).has_value(),
                  "temporary miss linked");
        }
        const auto before = snapshot(*display);
        auto ctx = context(*display, data, display->root());
        report_is(run(*display, ctx, operands), VmfExecutionStatus::ended, 2,
                  512, 1, 0);
        check(operands.size() == 0 && same(before, snapshot(*display)),
              "last linked owner remains valid through missed lookup");
    }
}

void reset_before_pop_and_faults(Profile profile) {
    for (int operand_kind = 0; operand_kind != 4; ++operand_kind) {
        Bytes actions;
        text_op(actions, profile, 0x8b, "a");
        op(actions, profile, 0x20);
        op(actions, profile, 7);
        op(actions, profile, 0);
        const auto data = prepare(profile, std::move(actions));
        auto display = runtime(data);
        const auto root = display->root();
        const auto a = child(*display, place(*display, data, "baseA"));
        const auto b = child(*display, place(*display, data, "baseB"));
        const auto ab = child(*display, place_on(*display, data, a, "childB"));
        auto operands = stack();
        if (operand_kind == 1)
            check(operands.push_sprite({123, 7}).has_value(),
                  "sprite bad text source");
        else if (operand_kind >= 2)
            check(operands.push_text("b").has_value(),
                  "TellTarget origin-relative path");
        auto ctx = context(*display, data, root);
        const auto before = snapshot(*display);
        report_is(run(*display, ctx, operands, {1, 1024, 0}),
                  VmfExecutionStatus::instruction_limit, 1, 1, 0, 0x20);
        state_is(ctx, data.script, 1, VmfScriptPhase::execute, root, a);
        const auto old_stack = stack_state(operands);
        if (operand_kind < 2 || operand_kind == 3) {
            const auto answer =
                execute_vmf_actions(*display, ctx, operands,
                                    {100, operand_kind == 3 ? 0U : 1024U, 100});
            check(!answer && answer.error().opcode == 0x20 &&
                      answer.error().action_index == 1,
                  "TellTarget error names its own unadvanced instruction");
            if (answer && failures == 0)
                throw std::runtime_error("expected TellTarget fault");
            if (!answer) {
                if (operand_kind < 2) {
                    check(answer.error().code == VmfExecutionErrorCode::stack &&
                              answer.error().stack_error &&
                              answer.error().stack_error->code ==
                                  (operand_kind == 0
                                       ? VmfStackErrorCode::empty_text_stack
                                       : VmfStackErrorCode::sprite_conversion),
                          "empty/sprite PopText failures remain explicit");
                    check(stack_state(operands) == old_stack,
                          "failed PopText does not consume its value");
                } else {
                    check(answer.error().code ==
                                  VmfExecutionErrorCode::text_limit &&
                              operands.size() == 0,
                          "TellTarget text quota occurs AFTER the successful "
                          "pop");
                }
            }
            state_is(ctx, data.script, 1, VmfScriptPhase::faulted, root, root);
            check(same(before, snapshot(*display)),
                  "pop/text failure resets selection but cannot resolve or "
                  "mutate display");
            const auto failed = ctx.snapshot();
            const auto retry = execute_vmf_actions(*display, ctx, operands);
            check(!retry && same(failed, ctx.snapshot()),
                  "fault cannot replay pop or change reset origin");
        } else {
            report_is(run(*display, ctx, operands, {1, 1024, 0}),
                      VmfExecutionStatus::instruction_limit, 1, 1, 0, 7);
            state_is(ctx, data.script, 2, VmfScriptPhase::execute, root, b);
            check(
                b != ab && operands.size() == 0 &&
                    same(before, snapshot(*display)),
                "TellTarget starts from saved root, not previously selected A");
        }
    }
    for (bool text_quota : {false, true}) {
        Bytes actions;
        text_op(actions, profile, 0x8b, "b");
        text_op(actions, profile, 0x8b, "a");
        op(actions, profile, 0);
        const auto data = prepare(profile, std::move(actions));
        auto display = runtime(data);
        const auto root = display->root();
        const auto b = child(*display, place(*display, data, "baseB"));
        place(*display, data, "partialSpace");
        auto operands = stack();
        seed_stack(operands);
        const auto values = stack_state(operands);
        auto ctx = context(*display, data, root);
        const auto before = snapshot(*display);
        report_is(run(*display, ctx, operands, {1, 1024, 0}),
                  VmfExecutionStatus::instruction_limit, 1, 1, 0, 0x8b);
        state_is(ctx, data.script, 1, VmfScriptPhase::execute, root, b);
        const auto answer = execute_vmf_actions(
            *display, ctx, operands, {100, text_quota ? 0U : 1024U, 100});
        check(!answer && answer.error().action_index == 1 &&
                  answer.error().opcode == 0x8b &&
                  answer.error().code ==
                      (text_quota ? VmfExecutionErrorCode::text_limit
                                  : VmfExecutionErrorCode::timeline),
              "modern direct target error names exact failing operation");
        if (!text_quota && !answer)
            check(answer.error().display_error &&
                      answer.error().display_error->code ==
                          RuntimeError::invalid_native_state,
                  "guarded DF overread is not an ordinary native miss");
        state_is(ctx, data.script, 1, VmfScriptPhase::faulted, root, root);
        check(same(before, snapshot(*display)) &&
                  stack_state(operands) == values,
              "modern lookup fault publishes origin reset, not partial/null "
              "selection");
        drain_sentinels(operands);
    }
}

Bytes host_payload(std::uint16_t code) {
    if (code == 0x83)
        return Bytes(2);
    if (code == 0x9a || code == 0x9f)
        return Bytes(1);
    return {};
}
void preserved_boundaries(Profile profile) {
    constexpr std::array<std::uint16_t, 9> host{0x1c, 0x1d, 0x22, 0x23, 0x26,
                                                0x83, 0x9a, 0x9e, 0x9f};
    for (auto opcode : host) {
        Bytes actions;
        text_op(actions, profile, 0x8b, "a");
        text_op(actions, profile, 0x96, "argument");
        op(actions, profile, opcode, host_payload(opcode));
        op(actions, profile, 0);
        const auto data = prepare(profile, std::move(actions));
        auto display = runtime(data);
        const auto root = display->root();
        const auto a = child(*display, place(*display, data, "baseA"));
        auto operands = stack();
        auto ctx = context(*display, data, root);
        const auto before = snapshot(*display);
        report_is(run(*display, ctx, operands), VmfExecutionStatus::needs_host,
                  2, 9, 0, opcode);
        state_is(ctx, data.script, 2, VmfScriptPhase::execute, root, a);
        const auto values = stack_state(operands);
        check(values.size == 1 && values.text == "argument",
              "host boundary retains its actual operand");
        report_is(run(*display, ctx, operands), VmfExecutionStatus::needs_host,
                  0, 0, 0, opcode);
        state_is(ctx, data.script, 2, VmfScriptPhase::execute, root, a);
        check(stack_state(operands) == values &&
                  same(before, snapshot(*display)),
              "repeated host boundary preserves target, stack, cursor and "
              "display");
    }
    for (auto code :
         {std::uint16_t{0x20}, std::uint16_t{0x8b}, std::uint16_t{0x8c}}) {
        Bytes actions;
        if (code == 0x20)
            op(actions, profile, code);
        else
            text_op(actions, profile, code, "");
        op(actions, profile, 0);
        const auto data = prepare(profile, std::move(actions));
        auto display = runtime(data);
        auto operands = stack();
        check(operands.push_text("argument").has_value(), "old API operand");
        const auto values = stack_state(operands);
        const auto before = snapshot(*display);
        VmfExecutionCursor pure{data.script};
        VmfExecutionCursor routed{data.script};
        report_is(
            take(execute_vmf_actions(data.program->movie(), pure, operands),
                 "old scalar boundary"),
            VmfExecutionStatus::needs_host, 0, 0, 0, code);
        report_is(take(execute_vmf_actions(*display, display->root(), routed,
                                           operands),
                       "old 24-action boundary"),
                  VmfExecutionStatus::needs_host, 0, 0, 0, code);
        check(
            pure.next_action == 0 && routed.next_action == 0 && !pure.faulted &&
                !routed.faulted && stack_state(operands) == values &&
                same(before, snapshot(*display)),
            "old19/24 entry points do not silently gain target/label effects");
    }
}

void identity_and_factory_guards(Profile profile) {
    Bytes actions;
    text_op(actions, profile, 0x8b, "a");
    op(actions, profile, 7);
    op(actions, profile, 0);
    const auto data = prepare(profile, std::move(actions));
    auto display = runtime(data);
    auto other_display = runtime(data);
    const auto root = display->root();
    const auto target = child(*display, place(*display, data, "baseA"));
    const auto button = child(*display, place(*display, data, "buttonName"));
    const auto before = snapshot(*display);
    const auto bad_script = VmfScriptContext::create(
        *display, root, data.program->movie().scripts().size());
    check(!bad_script &&
              bad_script.error().code ==
                  VmfExecutionErrorCode::invalid_cursor &&
              bad_script.error().opcode == 0 &&
              bad_script.error().action_index == 0 &&
              bad_script.error().instructions_executed == 0,
          "factory rejects script index at exact end before effects");
    for (auto wrong : {VmfInstanceHandle{}, other_display->root(), button}) {
        const auto result =
            VmfScriptContext::create(*display, wrong, data.script);
        check(
            !result && result.error().code == VmfExecutionErrorCode::timeline &&
                result.error().display_error &&
                result.error().display_error->code ==
                    (wrong == button ? RuntimeError::wrong_kind
                                     : RuntimeError::invalid_handle) &&
                result.error().opcode == 0 &&
                result.error().instructions_executed == 0,
            "factory validates live timeline origin even before empty script");
    }
    check(same(before, snapshot(*display)),
          "failed factories preserve full display");
    {
        auto operands = stack();
        seed_stack(operands);
        const auto values = stack_state(operands);
        auto ctx = context(*display, data, root);
        const auto answer = execute_vmf_actions(*other_display, ctx, operands);
        check(!answer &&
                  answer.error().code == VmfExecutionErrorCode::timeline &&
                  answer.error().display_error &&
                  answer.error().display_error->code ==
                      RuntimeError::invalid_handle &&
                  answer.error().opcode == 0 &&
                  answer.error().instructions_executed == 0 &&
                  stack_state(operands) == values,
              "same program cannot substitute a foreign display context");
        state_is(ctx, data.script, 0, VmfScriptPhase::faulted, root, root);
        drain_sentinels(operands);
    }
    {
        const auto different_data = prepare(profile, end_only(profile));
        auto different_display = runtime(different_data);
        auto ctx = context(*display, data, root);
        auto operands = stack();
        const auto answer =
            execute_vmf_actions(*different_display, ctx, operands);
        check(!answer &&
                  answer.error().code ==
                      VmfExecutionErrorCode::invalid_program &&
                  answer.error().opcode == 0 &&
                  answer.error().instructions_executed == 0 &&
                  operands.size() == 0,
              "program mismatch is identified before origin/command access");
        state_is(ctx, data.script, 0, VmfScriptPhase::faulted, root, root);
    }
    {
        auto operands = stack();
        auto ctx = context(*display, data, root, {1, 0});
        const auto answer = execute_vmf_actions(*display, ctx, operands);
        check(!answer &&
                  answer.error().code == VmfExecutionErrorCode::timeline &&
                  answer.error().display_error &&
                  answer.error().display_error->code ==
                      RuntimeError::resource_limit,
              "context preserves its explicit lookup node budget");
        state_is(ctx, data.script, 0, VmfScriptPhase::faulted, root, root);
    }
    {
        auto operands = stack();
        auto ctx = context(*display, data, root);
        report_is(run(*display, ctx, operands, {1, 1024, 0}),
                  VmfExecutionStatus::instruction_limit, 1, 1, 0, 7);
        state_is(ctx, data.script, 1, VmfScriptPhase::execute, root, target);
        succeeds(display->set_driver_frames(root, {2, 2, false}),
                 "only driver advances before selected-child destruction");
        check(take(display->remove(root, data.r("rm10")),
                   "destroy selected child"),
              "selected child destruction is real");
        const auto after_destroy = snapshot(*display);
        const auto answer = execute_vmf_actions(*display, ctx, operands);
        check(!answer &&
                  answer.error().code == VmfExecutionErrorCode::timeline &&
                  answer.error().display_error &&
                  answer.error().display_error->code ==
                      RuntimeError::invalid_handle &&
                  same(after_destroy, snapshot(*display)),
              "stale current target is checked only when executing handler "
              "needs it");
        state_is(ctx, data.script, 1, VmfScriptPhase::faulted, root, target);
    }
    {
        auto operands = stack();
        auto ctx = context(*display, data, root);
        succeeds(display->close(), "close after context construction");
        const auto answer = execute_vmf_actions(*display, ctx, operands);
        check(!answer &&
                  answer.error().code == VmfExecutionErrorCode::timeline &&
                  answer.error().display_error &&
                  answer.error().display_error->code == RuntimeError::closed &&
                  answer.error().opcode == 0 &&
                  answer.error().instructions_executed == 0,
              "closed runtime cannot be entered through retained "
              "program/context");
        state_is(ctx, data.script, 0, VmfScriptPhase::faulted, root, root);
    }
    // Script kind is independent of the target: the button's validated End
    // script is admissible for the explicit timeline caller.
    {
        auto live = runtime(data);
        auto operands = stack();
        auto ctx =
            take(VmfScriptContext::create(*live, live->root(), 0),
                 "button-condition script with explicit timeline caller");
        report_is(run(*live, ctx, operands), VmfExecutionStatus::ended, 1, 0, 0,
                  0);
        state_is(ctx, 0, 0, VmfScriptPhase::ended, live->root(), live->root());
    }
}

#ifdef _WIN32
class GuardedText {
  public:
    explicit GuardedText(std::string_view bytes) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page_ = info.dwPageSize;
        if (bytes.size() > page_)
            throw std::runtime_error("guard text exceeds page");
        memory_ = static_cast<char*>(VirtualAlloc(
            nullptr, page_ * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!memory_)
            throw std::runtime_error("fixture VirtualAlloc failed");
        DWORD old{};
        if (!VirtualProtect(memory_ + page_, page_, PAGE_NOACCESS, &old)) {
            VirtualFree(memory_, 0, MEM_RELEASE);
            memory_ = nullptr;
            throw std::runtime_error("fixture guard-page setup failed");
        }
        data_ = memory_ + page_ - bytes.size();
        size_ = bytes.size();
        if (size_ != 0)
            std::memcpy(data_, bytes.data(), size_);
    }
    ~GuardedText() noexcept {
        if (memory_)
            VirtualFree(memory_, 0, MEM_RELEASE);
    }
    GuardedText(const GuardedText&) = delete;
    GuardedText& operator=(const GuardedText&) = delete;
    std::string_view view() const noexcept { return {data_, size_}; }

  private:
    char* memory_{};
    char* data_{};
    std::size_t page_{};
    std::size_t size_{};
};
#endif
void guarded_views(Profile profile) {
#ifdef _WIN32
    CTypeScope c_locale;
    const auto data = prepare(profile, end_only(profile));
    auto display = runtime(data);
    const auto root = display->root();
    const auto a = child(*display, place(*display, data, "baseA"));
    {
        const GuardedText query("a");
        resolve_is(*display, root, query.view(), a);
    }
    {
        const GuardedText empty(""); // data points directly into PAGE_NOACCESS.
        resolve_is(*display, root, empty.view(), root, {0, 0});
        label_is(*display, root, empty.view(), 1U);
    }
    {
        const GuardedText query(std::string("a\0unread-suffix", 15));
        resolve_is(*display, root, query.view(), a, {1, 1});
    }
    {
        const GuardedText query("last");
        label_is(*display, root, query.view(), 8U);
    }
    {
        const GuardedText query(std::string(63, 'B'));
        label_is(*display, root, query.view(), 3U);
    }
    auto unsafe_display = runtime(data);
    place(*unsafe_display, data, "partialSpace");
    const GuardedText final_a("a");
    resolve_error(*unsafe_display, unsafe_display->root(), final_a.view(),
                  RuntimeError::invalid_native_state);
#else
    (void)profile;
#endif
}
} // namespace

int main(int argc, char** argv) {
    try {
        for (auto profile : profiles)
            fixture_preflight(profile);
        const bool fixture_only =
            argc == 2 && std::string_view(argv[1]) == "--fixture-only";
        if (!fixture_only) {
            for (auto profile : profiles) {
                path_basics(profile);
                path_order_and_bytes(profile);
                cache_long_and_guards(profile);
                labels(profile);
                selected_targets_and_labels(profile);
                skip_budgets(profile);
                skipped_control_and_boundaries(profile);
                telltarget_sources(profile);
                reset_before_pop_and_faults(profile);
                preserved_boundaries(profile);
                identity_and_factory_guards(profile);
                guarded_views(profile);
            }
        }
        std::cout << "VMF target execution: " << checks << " checks, "
                  << failures << " failures\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FATAL: " << error.what() << '\n';
    }
    return failures == 0 ? 0 : 1;
}

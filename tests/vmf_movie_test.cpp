#include "tdur/assets/vmf_movie.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// This observes/fails ordinary operator-new allocations, including the
// std::vector buffers of the public normally aligned VMF structures. It is
// not a claim to observe direct malloc calls or arbitrary aligned allocators.
namespace vmf_allocation_fixture {
std::atomic<bool> enabled{false};
std::atomic<std::size_t> calls{0};
std::atomic<std::size_t> fail_at{std::numeric_limits<std::size_t>::max()};
std::atomic<std::size_t> failure_bytes{0};
std::atomic<bool> owner_only{false};
thread_local bool owner_thread = false;

void* allocate(std::size_t size) {
    if (enabled.load(std::memory_order_relaxed) &&
        (!owner_only.load(std::memory_order_relaxed) || owner_thread)) {
        const auto ordinal = calls.fetch_add(1, std::memory_order_relaxed);
        if (ordinal == fail_at.load(std::memory_order_relaxed)) {
            failure_bytes.store(size, std::memory_order_relaxed);
            throw std::bad_alloc();
        }
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}
} // namespace vmf_allocation_fixture

void* operator new(std::size_t size) {
    return vmf_allocation_fixture::allocate(size);
}
void* operator new[](std::size_t size) { return ::operator new(size); }
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
using Error = VmfErrorCode;

std::size_t checks = 0;
std::size_t failures = 0;

// Dedicated single-owner allocation-fault interval. No diagnostic/snapshot
// allocations occur until its destructor restores every prior fixture value.
class AllocationFailureScope {
  public:
    explicit AllocationFailureScope(std::size_t ordinal) noexcept
        : previous_enabled_(vmf_allocation_fixture::enabled.exchange(false)),
          previous_calls_(vmf_allocation_fixture::calls.load()),
          previous_failure_(vmf_allocation_fixture::fail_at.load()),
          previous_bytes_(vmf_allocation_fixture::failure_bytes.load()),
          previous_owner_only_(vmf_allocation_fixture::owner_only.load()),
          previous_owner_(vmf_allocation_fixture::owner_thread) {
        vmf_allocation_fixture::calls = 0;
        vmf_allocation_fixture::failure_bytes = 0;
        vmf_allocation_fixture::fail_at = ordinal;
        vmf_allocation_fixture::owner_thread = true;
        vmf_allocation_fixture::owner_only = true;
        vmf_allocation_fixture::enabled = true;
    }

    ~AllocationFailureScope() noexcept {
        vmf_allocation_fixture::enabled = false;
        vmf_allocation_fixture::calls = previous_calls_;
        vmf_allocation_fixture::fail_at = previous_failure_;
        vmf_allocation_fixture::failure_bytes = previous_bytes_;
        vmf_allocation_fixture::owner_thread = previous_owner_;
        vmf_allocation_fixture::owner_only = previous_owner_only_;
        vmf_allocation_fixture::enabled = previous_enabled_;
    }

    AllocationFailureScope(const AllocationFailureScope&) = delete;
    AllocationFailureScope& operator=(const AllocationFailureScope&) = delete;

    [[nodiscard]] std::size_t failure_bytes() const noexcept {
        return vmf_allocation_fixture::failure_bytes.load();
    }

  private:
    bool previous_enabled_;
    std::size_t previous_calls_;
    std::size_t previous_failure_;
    std::size_t previous_bytes_;
    bool previous_owner_only_;
    bool previous_owner_;
};

void check(bool passed, std::string_view description) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 40) {
            std::cerr << "FAIL: " << description << '\n';
        }
    }
}

#define VMF_RVALUE_CONCEPT(name, expression)                                   \
    template <class T>                                                         \
    concept name = requires(T&& object) { expression; }
VMF_RVALUE_CONCEPT(TemporaryHeader, std::move(object).header());
VMF_RVALUE_CONCEPT(TemporarySource, std::move(object).source_bytes());
VMF_RVALUE_CONCEPT(TemporaryBytes, std::move(object).bytes(VmfByteRange{}));
VMF_RVALUE_CONCEPT(TemporaryRecords, std::move(object).records());
VMF_RVALUE_CONCEPT(TemporarySprites, std::move(object).sprites());
VMF_RVALUE_CONCEPT(TemporaryFrames, std::move(object).frames());
VMF_RVALUE_CONCEPT(TemporaryDefinitions, std::move(object).definitions());
VMF_RVALUE_CONCEPT(TemporaryScripts, std::move(object).scripts());
VMF_RVALUE_CONCEPT(TemporaryActions, std::move(object).actions());
VMF_RVALUE_CONCEPT(TemporaryButtons, std::move(object).button_visuals());
VMF_RVALUE_CONCEPT(TemporaryLabels, std::move(object).labels());
VMF_RVALUE_CONCEPT(TemporaryImages, std::move(object).images());
VMF_RVALUE_CONCEPT(TemporaryAtlas, std::move(object).atlas_entries());
#undef VMF_RVALUE_CONCEPT

static_assert(!TemporaryHeader<VmfMovie> && !TemporaryHeader<const VmfMovie>);
static_assert(!TemporarySource<VmfMovie> && !TemporarySource<const VmfMovie>);
static_assert(!TemporaryBytes<VmfMovie> && !TemporaryBytes<const VmfMovie>);
static_assert(!TemporaryRecords<VmfMovie> && !TemporarySprites<VmfMovie>);
static_assert(!TemporaryFrames<VmfMovie> && !TemporaryDefinitions<VmfMovie>);
static_assert(!TemporaryScripts<VmfMovie> && !TemporaryActions<VmfMovie>);
static_assert(!TemporaryButtons<VmfMovie> && !TemporaryLabels<VmfMovie>);
static_assert(!TemporaryImages<VmfMovie> && !TemporaryAtlas<VmfMovie>);
static_assert(!std::is_default_constructible_v<VmfMovie>);
static_assert(std::is_copy_constructible_v<VmfMovie>);
static_assert(std::is_nothrow_move_constructible_v<VmfMovie>);
static_assert(noexcept(std::declval<const VmfMovie&>().bytes({})));

constexpr std::array profiles{Profile::pc_2_8, Profile::xbox_2_5};

std::byte octet(std::uint32_t value) {
    return static_cast<std::byte>(value & 0xffU);
}

std::uint32_t narrow(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("fixture exceeds u32");
    }
    return static_cast<std::uint32_t>(value);
}

void put(Bytes& out, std::size_t at, std::uint32_t value, std::size_t width,
         Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto shift = 8U * static_cast<unsigned>(profile == Profile::pc_2_8
                                                          ? i
                                                          : width - 1 - i);
        out.at(at + i) = octet(value >> shift);
    }
}

void put16(Bytes& out, std::size_t at, std::uint16_t value, Profile profile) {
    put(out, at, value, 2, profile);
}
void put32(Bytes& out, std::size_t at, std::uint32_t value, Profile profile) {
    put(out, at, value, 4, profile);
}
void put_float(Bytes& out, std::size_t at, float value, Profile profile) {
    put32(out, at, std::bit_cast<std::uint32_t>(value), profile);
}
void append(Bytes& out, std::span<const std::byte> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
void append16(Bytes& out, std::uint16_t value, Profile profile) {
    const auto at = out.size();
    out.resize(at + 2);
    put16(out, at, value, profile);
}
void pad(Bytes& out, std::size_t alignment) {
    while (out.size() % alignment != 0) {
        out.push_back(std::byte{});
    }
}

// Bit-serial CRC oracle: no parser code, lookup table or zlib dependency.
std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto value : bytes) {
        crc ^= std::to_integer<std::uint32_t>(value);
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}

void seal_header(Bytes& bytes, Profile profile) {
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U}) {
            std::swap(canonical.at(at), canonical.at(at + 1));
        }
        for (const auto at : {44U, 48U, 52U, 56U}) {
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
        }
    }
    put32(bytes, 68, crc32(canonical), profile);
}

Bytes record(Profile profile, std::uint32_t type, Bytes payload = {}) {
    Bytes result(8);
    append(result, payload);
    pad(result, 4);
    put32(result, 0, type, profile);
    put32(result, 4, narrow(result.size()), profile);
    return result;
}

Bytes definition_record(Profile profile, std::uint32_t type, std::uint32_t id) {
    Bytes payload(4, std::byte{0xa7});
    if (type == 10) {
        put16(payload, 0, static_cast<std::uint16_t>(id), profile);
        put16(payload, 2, 0xbeefU, profile); // Must not be part of the ID.
    } else {
        put32(payload, 0, id, profile);
    }
    return record(profile, type, std::move(payload));
}

struct SpriteFixture {
    Bytes bytes;
    std::vector<std::size_t> item_offsets;
    std::vector<std::size_t> show_offsets;
    std::size_t end_offset{};
};
using FrameItems = std::vector<Bytes>;

SpriteFixture sprite(Profile profile, std::uint32_t id,
                     const std::vector<FrameItems>& frames, bool root = true) {
    if (frames.empty() || frames.size() > 65535) {
        throw std::runtime_error("invalid fixture frame count");
    }
    const auto metadata = 24 + 4 * frames.size();
    SpriteFixture out{Bytes(metadata), {}, {}, 0};
    put32(out.bytes, 0, 39, profile);
    put32(out.bytes, 4, narrow(metadata), profile);
    put32(out.bytes, 8, id, profile);
    put16(out.bytes, 12, static_cast<std::uint16_t>(frames.size()), profile);
    put16(out.bytes, 14, root ? 0x1357U : 0x8642U, profile);
    put32(out.bytes, 20, narrow(metadata), profile);
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        for (const auto& item : frames[frame]) {
            out.item_offsets.push_back(out.bytes.size());
            append(out.bytes, item);
        }
        out.show_offsets.push_back(out.bytes.size());
        append(out.bytes, record(profile, 1));
        put32(out.bytes, 24 + frame * 4, narrow(out.bytes.size()), profile);
    }
    out.end_offset = out.bytes.size();
    Bytes ending(8);
    put32(ending, 4, root ? 0U : 8U, profile);
    append(out.bytes, ending);
    put32(out.bytes, 16, narrow(out.bytes.size()), profile);
    return out;
}

struct AtlasFixture {
    std::uint32_t id{};
    std::uint32_t page{};
    std::array<std::uint32_t, 2> words{0xfeedbeefU, 0x80000000U};
    std::array<float, 4> floats{0.0F, 0.25F, 1.0F, 0.75F};
};

Bytes movie(Profile profile, const SpriteFixture& root,
            std::uint16_t definitions,
            std::span<const AtlasFixture> atlas = {}) {
    Bytes out(72);
    out[0] = std::byte{'V'};
    out[1] = std::byte{'M'};
    out[2] = std::byte{'F'};
    out[4] = std::byte{0x63}; // Opaque; deliberately unlike commercial files.
    out[5] = std::byte{2};
    put16(out, 6, profile == Profile::pc_2_8 ? 8U : 5U, profile);
    out[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    out[9] = std::byte{0xa5};
    put16(out, 10, 0x1234U, profile);
    for (std::size_t i = 0; i < 32; ++i) {
        out[12 + i] = octet(0x80U + narrow(i)); // Not a NUL-terminated author.
    }
    const std::array rectangle{1.0F, 1280.25F, -16.5F, 720.0F};
    for (std::size_t i = 0; i < rectangle.size(); ++i) {
        put_float(out, 44 + 4 * i, rectangle[i], profile);
    }
    put16(out, 60, 30, profile);
    put16(out, 62, definitions, profile);
    put16(out, 64, 0x1357U, profile);
    put16(out, 66, 0x2468U, profile);
    append(out, root.bytes);
    const auto tail = out.size();
    out.resize(tail + 8 + 32 * atlas.size());
    put32(out, tail, 0x54584958U, profile);
    put32(out, tail + 4, narrow(atlas.size()), profile);
    for (std::size_t i = 0; i < atlas.size(); ++i) {
        const auto at = tail + 8 + 32 * i;
        put32(out, at, atlas[i].id, profile);
        put32(out, at + 4, atlas[i].page, profile);
        put32(out, at + 8, atlas[i].words[0], profile);
        put32(out, at + 12, atlas[i].words[1], profile);
        for (std::size_t j = 0; j < 4; ++j) {
            put_float(out, at + 16 + 4 * j, atlas[i].floats[j], profile);
        }
    }
    seal_header(out, profile);
    return out;
}

Bytes minimal(Profile profile) {
    return movie(profile, sprite(profile, 0, {{}}), 1);
}

void short_action(Bytes& out, std::uint16_t opcode, Profile profile) {
    append16(out, opcode, profile);
}
void long_action(Bytes& out, std::uint16_t opcode,
                 std::span<const std::byte> payload, Profile profile) {
    append16(out, opcode, profile);
    append16(out, static_cast<std::uint16_t>(payload.size()), profile);
    append(out, payload);
    pad(out, 2);
}

Bytes branch_stream(Profile profile, std::int16_t delta = 2,
                    std::uint16_t branch_opcode = 0x99) {
    // Expected VMF offsets: 0,2,10,16,18,20,22.
    // Expected compact offsets: 0,1,7,12,13,14,15; compact_size=16.
    // delta=2: compact 7+5+2=14 -> VMF20. Physical16+2=18 is the WRONG
    // but aligned instruction. This oracle was fixed before the parser CPP.
    Bytes out;
    short_action(out, 6, profile);
    const std::array text{std::byte{}, std::byte{'a'}, std::byte{}};
    long_action(out, 0x96, text, profile);
    Bytes operand(2);
    put16(operand, 0, static_cast<std::uint16_t>(delta), profile);
    long_action(out, branch_opcode, operand, profile);
    for (const auto op : {7U, 6U, 4U, 0U}) {
        short_action(out, static_cast<std::uint16_t>(op), profile);
    }
    return out;
}

Bytes timeline_movie(Profile profile, Bytes actions) {
    return movie(
        profile,
        sprite(profile, 0, {{record(profile, 12, std::move(actions))}}), 1);
}

Bytes button(Profile profile, std::uint32_t id, std::uint16_t character,
             std::size_t visuals = 1) {
    const std::size_t fixed = profile == Profile::pc_2_8 ? 20 : 16;
    const auto terminator = fixed + 48 * visuals;
    Bytes out(terminator + 4);
    put32(out, 0, 7, profile);
    put32(out, 8, id, profile);
    if (profile == Profile::pc_2_8) {
        put32(out, 12, 0xdeadbeefU, profile);
    }
    put32(out, fixed - 4, narrow(terminator), profile);
    for (std::size_t i = 0; i < visuals; ++i) {
        const auto at = fixed + 48 * i;
        put32(out, at, 0x81234567U, profile); // Intentionally not a 4-bit mask.
        put16(out, at + 4, character, profile);
        put16(out, at + 6, 0xfedcU, profile);
        const std::array matrix{1.0F, -2.5F, 3.25F, 4.0F, 5.5F, -6.0F};
        for (std::size_t j = 0; j < 6; ++j) {
            put_float(out, at + 8 + 4 * j, matrix[j], profile);
        }
        for (std::size_t j = 0; j < 8; ++j) {
            put16(out, at + 32 + 2 * j, static_cast<std::uint16_t>(0x8100U + j),
                  profile);
        }
    }
    // Condition0 spans 8 physical bytes but only one compact byte (End).
    append16(out, 8, profile);
    append16(out, 0xffffU, profile);
    short_action(out, 0, profile);
    pad(out, 4);
    append16(out, 0, profile);
    append16(out, 0x8123U, profile);
    append(out, branch_stream(profile));
    pad(out, 4);
    put32(out, 4, narrow(out.size()), profile);
    return out;
}

VmfMovie valid(std::span<const std::byte> bytes, Profile profile,
               VmfLimits limits = {}) {
    auto result = VmfMovie::parse(bytes, profile, limits);
    check(result.has_value(), "synthetic fixture accepted");
    if (!result) {
        std::cerr << "  parser error=" << static_cast<int>(result.error().code)
                  << " offset=" << result.error().offset << " "
                  << result.error().detail << '\n';
        throw std::runtime_error("cannot inspect rejected valid fixture");
    }
    return std::move(*result);
}

void rejects(std::span<const std::byte> bytes, Profile profile,
             std::string_view label,
             std::optional<Error> expected = std::nullopt,
             VmfLimits limits = {}) {
    const auto result = VmfMovie::parse(bytes, profile, limits);
    check(!result, label);
    if (!result) {
        check(!result.error().detail.empty(), "error has static diagnostic");
        if (expected) {
            check(result.error().code == *expected, label);
        }
    }
}

bool equal_bytes(std::span<const std::byte> a, std::span<const std::byte> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

std::size_t record_at(const VmfMovie& parsed, std::size_t source_offset) {
    for (std::size_t i = 0; i < parsed.records().size(); ++i) {
        if (parsed.records()[i].source_offset == source_offset) {
            return i;
        }
    }
    throw std::runtime_error("expected physical record absent");
}

std::size_t logical_size(const VmfMovie& parsed) {
    return parsed.source_bytes().size() + sizeof(VmfHeader) +
           parsed.records().size() * sizeof(VmfRecord) +
           parsed.sprites().size() * sizeof(VmfSprite) +
           parsed.frames().size() * sizeof(VmfFrame) +
           parsed.definitions().size() * sizeof(VmfDefinition) +
           parsed.scripts().size() * sizeof(VmfScript) +
           parsed.actions().size() * sizeof(VmfAction) +
           parsed.button_visuals().size() * sizeof(VmfButtonVisual) +
           parsed.labels().size() * sizeof(VmfLabel) +
           parsed.images().size() * sizeof(VmfImageReference) +
           parsed.atlas_entries().size() * sizeof(VmfAtlasEntry);
}

struct MovieSnapshot {
    Bytes source;
    Bytes metadata;
};

// Canonical test-only snapshot of EVERY public decoded field. No struct
// padding is compared and no untrusted range/index is dereferenced. This
// remains safe even when inspecting a destination damaged by an assignment.
MovieSnapshot snapshot(const VmfMovie& movie) {
    MovieSnapshot out{
        Bytes(movie.source_bytes().begin(), movie.source_bytes().end()), {}};
    const auto number = [&](auto value) {
        const auto bits = static_cast<std::uint64_t>(value);
        for (unsigned i = 0; i < 8; ++i) {
            out.metadata.push_back(
                octet(static_cast<std::uint32_t>(bits >> (i * 8))));
        }
    };
    const auto real = [&](float value) {
        number(std::bit_cast<std::uint32_t>(value));
    };
    const auto range = [&](VmfByteRange value) {
        number(value.offset);
        number(value.size);
    };
    const auto optional_number = [&](const auto& value) {
        number(value.has_value());
        if (value) {
            number(*value);
        }
    };
    const auto collection = [&](auto values, const auto& fields) {
        number(values.size());
        for (const auto& value : values) {
            fields(value);
        }
    };
    const auto& h = movie.header();
    number(h.profile);
    number(h.raw_04);
    number(h.major_version);
    number(h.minor_version);
    for (const auto value : h.raw_08) {
        number(value);
    }
    number(h.raw_0a);
    for (const auto value : h.author) {
        number(value);
    }
    for (const auto value : h.rectangle) {
        real(value);
    }
    number(h.raw_3c);
    number(h.definition_count);
    number(h.raw_40);
    number(h.raw_42);
    number(h.crc32);
    collection(movie.records(), [&](const VmfRecord& r) {
        number(r.source_offset);
        number(r.type);
        number(r.declared_size);
        number(r.full_size);
        range(r.payload);
        optional_number(r.parent_sprite_index);
        optional_number(r.definition_id);
    });
    collection(movie.sprites(), [&](const VmfSprite& s) {
        number(s.record_index);
        number(s.character_id);
        number(s.raw_0e);
        number(s.first_frame);
        number(s.frame_count);
    });
    collection(movie.frames(), [&](const VmfFrame& f) {
        number(f.sprite_index);
        number(f.index);
        number(f.begin_offset);
        number(f.end_offset);
        number(f.show_frame_record_index);
    });
    collection(movie.definitions(), [&](const VmfDefinition& d) {
        number(d.character_id);
        number(d.record_index);
    });
    collection(movie.scripts(), [&](const VmfScript& s) {
        number(s.kind);
        number(s.owner_record_index);
        number(s.parent_sprite_index);
        optional_number(s.frame_index);
        number(s.button_condition.has_value());
        if (s.button_condition) {
            const auto& c = *s.button_condition;
            number(c.source_offset);
            number(c.declared_size);
            number(c.raw_flags);
            number(c.ordinal);
        }
        range(s.encoded_region);
        number(s.first_action);
        number(s.action_count);
        number(s.compact_size);
    });
    collection(movie.actions(), [&](const VmfAction& a) {
        number(a.source_offset);
        number(a.opcode);
        number(a.encoded_size);
        range(a.payload);
        number(a.compact_offset);
        number(a.branch.has_value());
        if (a.branch) {
            number(a.branch->delta);
            number(a.branch->target_action_index);
        }
        for (const auto& text : a.strings) {
            number(text.has_value());
            if (text) {
                range(*text);
            }
        }
    });
    collection(movie.button_visuals(), [&](const VmfButtonVisual& v) {
        number(v.record_index);
        number(v.source_offset);
        optional_number(v.pc_extra_word);
        number(v.states);
        number(v.character_id);
        number(v.raw_06);
        for (const auto value : v.raw_matrix) {
            real(value);
        }
        for (const auto value : v.raw_words) {
            number(value);
        }
    });
    collection(movie.labels(), [&](const VmfLabel& l) {
        number(l.record_index);
        number(l.parent_sprite_index);
        number(l.frame_index);
        range(l.text);
    });
    collection(movie.images(), [&](const VmfImageReference& i) {
        number(i.character_id);
        number(i.record_index);
        number(i.source);
        number(i.source_number);
        optional_number(i.atlas_entry_index);
    });
    collection(movie.atlas_entries(), [&](const VmfAtlasEntry& e) {
        number(e.source_offset);
        number(e.character_id);
        number(e.page_number);
        for (const auto value : e.raw_words) {
            number(value);
        }
        for (const auto value : e.raw_floats) {
            real(value);
        }
    });
    number(movie.branch_count());
    number(movie.logical_output_bytes());
    return out;
}

bool same_snapshot(const MovieSnapshot& a, const MovieSnapshot& b) {
    return equal_bytes(a.source, b.source) &&
           equal_bytes(a.metadata, b.metadata);
}

void test_profiles_and_crc() {
    const std::array crc_vector{std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
                                std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
                                std::byte{'7'}, std::byte{'8'}, std::byte{'9'}};
    check(crc32(crc_vector) == 0xcbf43926U, "independent CRC known vector");
    for (const auto profile : profiles) {
        const auto source = minimal(profile);
        const auto untouched = source;
        auto parsed = valid(source, profile);
        check(equal_bytes(source, untouched),
              "parse does not normalize caller buffer in place");
        check(equal_bytes(source, parsed.source_bytes()),
              "parse preserves input bytes");
        const auto& h = parsed.header();
        check(h.profile == profile && h.major_version == 2,
              "profile and major");
        check(h.minor_version == (profile == Profile::pc_2_8 ? 8 : 5),
              "profile minor");
        check(h.raw_04 == std::byte{0x63} && h.raw_08[1] == std::byte{0xa5},
              "raw header bytes preserved");
        check(h.raw_0a == 0x1234U && h.raw_3c == 30 && h.raw_42 == 0x2468U,
              "raw header words preserved");
        check(h.raw_40 == 0x1357U && h.author[0] == std::byte{0x80} &&
                  h.author[31] == std::byte{0x9f},
              "non-NUL author and raw40 preserved");
        check(h.rectangle == std::array{1.0F, 1280.25F, -16.5F, 720.0F},
              "rectangle not converted");
        auto changed = source;
        changed[15] ^= std::byte{1};
        rejects(changed, profile, "CRC detects opaque header mutation",
                Error::invalid_crc);
        for (const auto major : {0U, 1U, 3U, 255U}) {
            changed = source;
            changed[5] = octet(major);
            seal_header(changed, profile);
            rejects(changed, profile, "unsupported major",
                    Error::unsupported_version);
        }
        changed = source;
        put16(changed, 6, 9, profile);
        seal_header(changed, profile);
        rejects(changed, profile, "unsupported minor",
                Error::unsupported_version);
        changed = source;
        changed[8] = profile == Profile::pc_2_8 ? std::byte{1} : std::byte{2};
        seal_header(changed, profile);
        rejects(changed, profile, "wrong endian marker rejected");
        rejects(source,
                profile == Profile::pc_2_8 ? Profile::xbox_2_5
                                           : Profile::pc_2_8,
                "no fallback to another profile");
        for (const auto bits : {0x7fc12345U, 0x7f800000U, 0xff800000U}) {
            changed = source;
            put32(changed, 48, bits, profile);
            seal_header(changed, profile);
            rejects(changed, profile, "nonfinite rectangle",
                    Error::invalid_numeric);
        }
        if (profile == Profile::xbox_2_5) {
            changed = source;
            put32(changed, 68, crc32(std::span(changed).first(68)), profile);
            rejects(changed, profile, "BE raw header CRC is not canonical CRC",
                    Error::invalid_crc);
        }
        changed = source;
        changed[0] = std::byte{'X'};
        seal_header(changed, profile);
        rejects(changed, profile, "wrong magic rejected");
        rejects(source, static_cast<Profile>(255), "unknown profile enum",
                Error::unsupported_profile);
    }
}

void test_hierarchy_frames_and_eof() {
    for (const auto profile : profiles) {
        const auto child =
            sprite(profile, 1, {{record(profile, 9)}, {}}, false);
        const auto root =
            sprite(profile, 0, {{child.bytes}, {record(profile, 15)}});
        const auto source = movie(profile, root, 2);
        const auto parsed = valid(source, profile);
        check(parsed.sprites().size() == 2 && parsed.frames().size() == 4,
              "parent and child have separate frames");
        check(parsed.records().size() == 10, "physical records counted once");
        for (const auto& s : parsed.sprites()) {
            const auto& encoding = s.character_id == 0 ? root : child;
            const auto base =
                s.character_id == 0 ? 72U : 72U + narrow(root.item_offsets[0]);
            const auto& r = parsed.records()[s.record_index];
            check(r.type == 39 && r.declared_size == 32 &&
                      r.full_size == encoding.bytes.size(),
                  "metadata distinct from full extent");
            for (std::size_t i = 0; i < 2; ++i) {
                const auto& f = parsed.frames()[s.first_frame + i];
                check(f.sprite_index < parsed.sprites().size() &&
                          parsed.sprites()[f.sprite_index].character_id ==
                              s.character_id,
                      "frame belongs to its direct sprite");
                const auto begin =
                    i == 0 ? base + 32U
                           : base + narrow(encoding.show_offsets[i - 1]) + 8U;
                check(f.index == i && f.begin_offset == begin &&
                          f.end_offset == base + encoding.show_offsets[i] + 8,
                      "relative frame ends and consecutive begins");
                check(
                    parsed.records()[f.show_frame_record_index].source_offset ==
                        base + encoding.show_offsets[i],
                    "frame references its ShowFrame");
            }
            const auto end_index =
                record_at(parsed, base + encoding.end_offset);
            const auto& end = parsed.records()[end_index];
            check(end.type == 0 && end.full_size == 8 &&
                      end.declared_size == (s.character_id == 0 ? 0U : 8U),
                  "root/nested End distinction");
        }
        auto changed = source;
        const auto child_at = 72 + root.item_offsets[0];
        // Entire child extent fits the FILE, but crosses its PARENT by8.
        put32(changed, child_at + 16, narrow(source.size() - child_at),
              profile);
        rejects(changed, profile, "child must fit parent before descent");
        changed = source;
        put32(changed, child_at + 20, 28, profile);
        rejects(changed, profile, "duplicated metadata size must agree");
        changed = source;
        put32(changed, 72 + 24, narrow(root.show_offsets[0]), profile);
        rejects(changed, profile,
                "frame table points AFTER ShowFrame, not at it");
        changed = source;
        put32(changed, 72 + 28, narrow(root.show_offsets[0] + 8), profile);
        rejects(changed, profile, "duplicate frame boundaries rejected");
        changed = source;
        put32(changed, 72 + root.show_offsets[0], 9, profile);
        rejects(changed, profile, "direct ShowFrame count required");
        changed = source;
        put32(changed, 72 + root.end_offset + 4, 8, profile);
        rejects(changed, profile, "root End cannot use nested length");
        changed = source;
        put32(changed, child_at + child.end_offset + 4, 0, profile);
        rejects(changed, profile, "nested End cannot use root length");
        changed = source;
        put32(changed, 72 + 16, 0xffffffffU, profile);
        rejects(changed, profile, "u32 sprite extent cannot overflow bounds");
        changed = source;
        put16(changed, 72 + 12, 0, profile);
        rejects(changed, profile, "zero-frame sprite unsupported");
        changed = source;
        put16(changed, 64, 0, profile);
        seal_header(changed, profile);
        rejects(changed, profile, "header raw40 must match root raw0e");

        const auto small = minimal(profile);
        check(small.size() == 124, "source-owned minimal fixture exact extent");
        for (std::size_t prefix = 0; prefix < small.size(); ++prefix) {
            rejects(std::span(small).first(prefix), profile,
                    "every truncated prefix rejected");
        }
        changed = small;
        changed.push_back(std::byte{});
        rejects(changed, profile, "exact EOF required");
        changed = small;
        put32(changed, changed.size() - 4, 0xffffffffU, profile);
        rejects(changed, profile,
                "TXIX count product bounded before allocation");
        changed = small;
        put32(changed, changed.size() - 8, 0, profile);
        rejects(changed, profile, "trailer magic checked");
    }
}

void test_definitions_and_known_opaque_records() {
    for (const auto profile : profiles) {
        const auto root = sprite(profile, 0,
                                 {{definition_record(profile, 10, 3),
                                   definition_record(profile, 20, 2),
                                   definition_record(profile, 2, 1)}});
        const auto source = movie(profile, root, 4);
        const auto parsed = valid(source, profile);
        check(parsed.definitions().size() == 4, "dense definition count");
        for (std::size_t id = 0; id < 4; ++id) {
            const auto& d = parsed.definitions()[id];
            check(d.character_id == id, "definitions sorted by ID");
            check(parsed.definition(narrow(id)) ==
                      std::optional{d.record_index},
                  "lookup returns physical record index");
            check(parsed.records()[d.record_index].definition_id ==
                      std::optional{narrow(id)},
                  "definition links to right physical record");
        }
        check(parsed.definitions()[3].record_index == 1,
              "font u16 ID, adjacent BEEF not part of ID");
        check(parsed.definitions()[1].record_index == 3,
              "definition ID differs from physical index");
        check(!parsed.definition(4) && !parsed.definition(0xffffffffU),
              "unknown definition IDs refused");
        check(parsed.images().size() == 1 &&
                  parsed.images()[0].character_id == 2 &&
                  parsed.images()[0].source == VmfImageSource::direct_fla &&
                  parsed.images()[0].source_number == 2 &&
                  !parsed.images()[0].atlas_entry_index,
              "direct image ID is not physical BNK index");
        auto changed = source;
        put32(changed, 72 + root.item_offsets[2] + 8, 2, profile);
        rejects(changed, profile, "duplicate definition rejected");
        changed = source;
        put32(changed, 72 + root.item_offsets[2] + 8, 0xffffffffU, profile);
        rejects(changed, profile,
                "huge definition ID rejected without sparse allocation");
        changed = source;
        put16(changed, 62, 5, profile);
        seal_header(changed, profile);
        rejects(changed, profile, "definition holes rejected");
        changed = source;
        put32(changed, 72 + 8, 4, profile);
        rejects(changed, profile, "root must be ID0");
        for (const auto type :
             {2U, 9U, 10U, 11U, 14U, 15U, 20U, 26U, 28U, 37U, 46U}) {
            const bool defines = type == 2 || type == 10 || type == 11 ||
                                 type == 14 || type == 20 || type == 37 ||
                                 type == 46;
            const auto item =
                defines ? definition_record(profile, type, 1)
                        : record(profile, type, Bytes(8, std::byte{0xff}));
            const auto m = valid(
                movie(profile, sprite(profile, 0, {{item}}), defines ? 2U : 1U),
                profile);
            check(
                m.records()[1].type == type,
                "known opaque tag admitted without invented payload semantics");
        }
        rejects(movie(profile, sprite(profile, 0, {{record(profile, 18)}}), 1),
                profile, "unknown record unsupported",
                Error::unsupported_record);
        for (const auto length : {0U, 4U, 10U, 0xffffffffU}) {
            auto bad = record(profile, 9);
            put32(bad, 4, length, profile);
            rejects(movie(profile, sprite(profile, 0, {{bad}}), 1), profile,
                    "invalid ordinary record length");
        }
    }
}

void test_branches_and_scripts() {
    for (const auto profile : profiles) {
        const std::array compact{0U, 1U, 7U, 12U, 13U, 14U, 15U};
        const std::array physical{0U, 2U, 10U, 16U, 18U, 20U, 22U};
        const auto source = movie(
            profile,
            sprite(profile, 0,
                   {{record(profile, 12, branch_stream(profile)),
                     record(profile, 12, branch_stream(profile, 3, 0x9d))}}),
            1);
        const auto parsed = valid(source, profile);
        check(parsed.scripts().size() == 2 && parsed.actions().size() == 14 &&
                  parsed.branch_count() == 2,
              "two independent action regions");
        bool nonzero_base = false;
        for (const auto& script : parsed.scripts()) {
            const auto& owner = parsed.records()[script.owner_record_index];
            const bool second = owner.source_offset != 100;
            check(script.kind == VmfScriptKind::timeline &&
                      script.frame_index ==
                          std::optional<std::uint16_t>{std::uint16_t{0}} &&
                      !script.button_condition,
                  "timeline ownership explicit");
            check(script.encoded_region.offset == owner.source_offset + 8 &&
                      script.encoded_region.size == 24 &&
                      script.compact_size == 16 && script.action_count == 7,
                  "script physical and compact sizes differ");
            nonzero_base |= script.first_action != 0;
            for (std::size_t i = 0; i < 7; ++i) {
                const auto& action = parsed.actions()[script.first_action + i];
                check(action.source_offset ==
                              script.encoded_region.offset + physical[i] &&
                          action.compact_offset == compact[i],
                      "explicit compact-to-VMF oracle");
            }
            const auto& branch = parsed.actions()[script.first_action + 2];
            check(branch.branch && branch.branch->delta == (second ? 3 : 2),
                  "signed branch retained");
            check(branch.branch && branch.branch->target_action_index ==
                                       script.first_action + (second ? 6 : 5),
                  "global branch index resolves correct instruction or End");
            if (!second) {
                check(branch.branch && branch.branch->target_action_index !=
                                           script.first_action + 4,
                      "aligned but wrong physical destination rejected as "
                      "mapping");
            }
            const auto& push = parsed.actions()[script.first_action + 1];
            check(push.strings[0] && !push.strings[1],
                  "Push string range exposed");
            if (push.strings[0]) {
                const auto text = parsed.bytes(*push.strings[0]);
                check(text && text->size() == 1 && (*text)[0] == std::byte{'a'},
                      "Push excludes type and NUL");
            }
        }
        check(nonzero_base, "branch target is not assumed script-local index");
        for (const auto opcode : {std::uint16_t{0x99}, std::uint16_t{0x9d}}) {
            for (const auto [delta, target] :
                 std::array<std::pair<std::int16_t, std::size_t>, 4>{
                     {{std::int16_t{-12}, std::size_t{0}},
                      {std::int16_t{0}, std::size_t{3}},
                      {std::int16_t{1}, std::size_t{4}},
                      {std::int16_t{3}, std::size_t{6}}}}) {
                const auto m =
                    valid(timeline_movie(profile,
                                         branch_stream(profile, delta, opcode)),
                          profile);
                const auto& s = m.scripts()[0];
                check(m.actions()[s.first_action + 2]
                              .branch->target_action_index ==
                          s.first_action + target,
                      "forward/backward/End branch targets");
            }
            for (const auto delta : {std::int16_t{-32768}, std::int16_t{-10},
                                     std::int16_t{4}, std::int16_t{32767}}) {
                rejects(
                    timeline_movie(profile,
                                   branch_stream(profile, delta, opcode)),
                    profile,
                    "interior, out-of-range and one-past-End targets refused",
                    Error::invalid_branch);
            }
        }
        // A long instruction in the LAST2 bytes must not borrow len from
        // ShowFrame.
        Bytes short_then_incomplete_long;
        short_action(short_then_incomplete_long, 6, profile);
        short_action(short_then_incomplete_long, 0x96, profile);
        rejects(timeline_movie(profile, short_then_incomplete_long), profile,
                "long header cannot cross stream end");
        Bytes no_end;
        short_action(no_end, 6, profile);
        short_action(no_end, 6, profile);
        rejects(timeline_movie(profile, no_end), profile, "stream needs End");
        Bytes ending;
        short_action(ending, 0, profile);
        ending.insert(ending.end(), 6, std::byte{});
        rejects(timeline_movie(profile, ending), profile,
                "more than3 tail bytes refused");
        ending.resize(4);
        ending[3] = std::byte{1};
        rejects(timeline_movie(profile, ending), profile, "tail must be zero");
        Bytes unsupported;
        long_action(unsupported, 0x94, {}, profile);
        short_action(unsupported, 0, profile);
        rejects(timeline_movie(profile, unsupported), profile,
                "nested action not silently accepted",
                Error::unsupported_action);
    }
}

void test_action_forms_and_raw_strings() {
    constexpr std::array<std::uint16_t, 25> short_opcodes{
        0x04, 0x05, 0x06, 0x07, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
        0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18,
        0x1c, 0x1d, 0x20, 0x21, 0x22, 0x23, 0x26};
    for (const auto profile : profiles) {
        Bytes stream;
        for (const auto op : short_opcodes) {
            short_action(stream, op, profile);
        }
        const Bytes two{std::byte{0xff}, std::byte{0x7f}};
        const Bytes one{std::byte{1}};
        const Bytes raw_string{std::byte{0xff}, std::byte{0x80}, std::byte{'x'},
                               std::byte{}};
        const Bytes url{std::byte{'x'}, std::byte{':'}, std::byte{0xff},
                        std::byte{},    std::byte{'t'}, std::byte{}};
        Bytes push{std::byte{}};
        append(push, raw_string);
        long_action(stream, 0x81, two, profile);
        long_action(stream, 0x83, url, profile);
        long_action(stream, 0x8b, raw_string, profile);
        long_action(stream, 0x8c, raw_string, profile);
        long_action(stream, 0x96, push, profile);
        const Bytes zero_delta(2);
        long_action(stream, 0x99, zero_delta, profile);
        long_action(stream, 0x9a, one, profile);
        long_action(stream, 0x9d, zero_delta, profile);
        long_action(stream, 0x9e, {}, profile);
        long_action(stream, 0x9f, one, profile);
        short_action(stream, 0, profile);
        const auto parsed = valid(timeline_movie(profile, stream), profile);
        check(parsed.actions().size() == 36 && parsed.branch_count() == 2,
              "all36 supported opcodes framed");
        const auto& url_action = parsed.actions()[26];
        check(url_action.opcode == 0x83 && url_action.strings[0] &&
                  url_action.strings[1],
              "two GetURL string ranges");
        if (url_action.strings[0] && url_action.strings[1]) {
            check(parsed.bytes(*url_action.strings[0])->size() == 3 &&
                      parsed.bytes(*url_action.strings[1])->size() == 1,
                  "URL need not be FSCommand or empty target");
        }
        for (const auto index : {27U, 28U, 29U}) {
            const auto& a = parsed.actions()[index];
            check(a.strings[0] && parsed.bytes(*a.strings[0])->size() == 3 &&
                      (*parsed.bytes(*a.strings[0]))[0] == std::byte{0xff},
                  "strings remain raw non-UTF8 bytes");
        }
        for (const auto opcode :
             {std::uint16_t{0x81}, std::uint16_t{0x83}, std::uint16_t{0x8b},
              std::uint16_t{0x8c}, std::uint16_t{0x99}, std::uint16_t{0x9a},
              std::uint16_t{0x9d}, std::uint16_t{0x9e}}) {
            Bytes malformed;
            const Bytes wrong(opcode == 0x9e ? 1 : 3, std::byte{0xff});
            long_action(malformed, opcode, wrong, profile);
            short_action(malformed, 0, profile);
            rejects(timeline_movie(profile, malformed), profile,
                    "invalid known action payload length/string");
        }
        Bytes alternate_push;
        const Bytes typed{std::byte{1}, std::byte{}, std::byte{}, std::byte{},
                          std::byte{}};
        long_action(alternate_push, 0x96, typed, profile);
        short_action(alternate_push, 0, profile);
        rejects(timeline_movie(profile, alternate_push), profile,
                "unimplemented Push variant", Error::unsupported_layout);
        Bytes alternate_goto;
        const Bytes scene_bias{std::byte{2}, std::byte{}, std::byte{}};
        long_action(alternate_goto, 0x9f, scene_bias, profile);
        short_action(alternate_goto, 0, profile);
        rejects(timeline_movie(profile, alternate_goto), profile,
                "unimplemented GotoFrame2 form", Error::unsupported_layout);
        Bytes padded;
        long_action(padded, 0x9a, one, profile);
        short_action(padded, 0, profile);
        padded[5] = std::byte{1};
        rejects(timeline_movie(profile, padded), profile,
                "odd long payload has zero alignment byte");
        Bytes wide_opcode;
        short_action(wide_opcode, 0x100, profile);
        short_action(wide_opcode, 0, profile);
        rejects(timeline_movie(profile, wide_opcode), profile,
                "opcode does not silently narrow to byte");
    }
}

void test_buttons_and_labels() {
    for (const auto profile : profiles) {
        const auto b = button(profile, 1, 0);
        const Bytes label{std::byte{0xff}, std::byte{'L'}, std::byte{}};
        const auto root =
            sprite(profile, 0, {{b}, {record(profile, 43, label)}});
        const auto source = movie(profile, root, 2);
        const auto parsed = valid(source, profile);
        check(parsed.button_visuals().size() == 1 &&
                  parsed.scripts().size() == 2,
              "singleton button with two conditions");
        const auto& visual = parsed.button_visuals()[0];
        const auto button_at = 72 + root.item_offsets[0];
        const auto fixed = profile == Profile::pc_2_8 ? 20U : 16U;
        check(visual.source_offset == button_at + fixed &&
                  visual.character_id == 0 && visual.states == 0x81234567U &&
                  visual.raw_06 == 0xfedcU,
              "uninterpreted button state/word preserved");
        check(visual.raw_matrix ==
                  std::array{1.0F, -2.5F, 3.25F, 4.0F, 5.5F, -6.0F},
              "matrix finite values unconverted");
        check(visual.raw_words[0] == 0x8100U && visual.raw_words[7] == 0x8107U,
              "visual opaque words preserved");
        check(visual.pc_extra_word == (profile == Profile::pc_2_8
                                           ? std::optional{0xdeadbeefU}
                                           : std::nullopt),
              "PC extra word not treated as boolean");
        for (const auto& script : parsed.scripts()) {
            check(script.kind == VmfScriptKind::button_condition &&
                      !script.frame_index && script.button_condition,
                  "condition not assigned timeline focus/frame");
            if (script.button_condition) {
                const auto& c = *script.button_condition;
                check(c.ordinal < 2, "condition ordinal");
                const bool first = c.ordinal == 0;
                check(c.source_offset ==
                          button_at + fixed + 52 + (first ? 0 : 8),
                      "physical condition chain offsets");
                check(c.declared_size == (first ? 8 : 0) &&
                          c.raw_flags == (first ? 0xffffU : 0x8123U),
                      "condition flags and terminal size preserved");
                check(script.encoded_region.offset == c.source_offset + 4 &&
                          script.action_count == (first ? 1U : 7U),
                      "condition region and action count");
            }
        }
        check(parsed.labels().size() == 1 &&
                  parsed.labels()[0].frame_index == 1,
              "label belongs to second direct frame");
        const auto label_bytes = parsed.bytes(parsed.labels()[0].text);
        check(label_bytes && label_bytes->size() == 2 &&
                  (*label_bytes)[0] == std::byte{0xff},
              "label preserves raw bytes without NUL");
        auto changed = source;
        put32(changed, button_at + fixed - 4, narrow(b.size()), profile);
        rejects(changed, profile,
                "button terminator cannot borrow from following record");
        changed = source;
        put32(changed, button_at + fixed - 4, 0xffffffffU, profile);
        rejects(changed, profile, "button offset bounded before arithmetic");
        changed = source;
        put32(changed, button_at + fixed + 48, 1, profile);
        rejects(changed, profile, "button terminator must be zero");
        changed = source;
        put16(changed, button_at + fixed + 4, 2, profile);
        rejects(changed, profile, "button reference must be defined");
        changed = source;
        put32(changed, button_at + fixed + 8, 0x7fc00000U, profile);
        rejects(changed, profile, "nonfinite button matrix",
                Error::invalid_numeric);
        for (const auto length :
             {std::uint16_t{2}, std::uint16_t{7}, std::uint16_t{0xfffc}}) {
            changed = source;
            put16(changed, button_at + fixed + 52, length, profile);
            rejects(changed, profile,
                    "condition length must remain inside its tag and aligned");
        }
        rejects(
            movie(profile, sprite(profile, 0, {{button(profile, 1, 0, 2)}}), 2),
            profile, "well-bounded multivisual variant explicitly unsupported",
            Error::unsupported_layout);
        rejects(
            movie(profile, sprite(profile, 0, {{button(profile, 1, 0, 0)}}), 2),
            profile, "zero visuals not silently accepted");
        auto invalid_label = record(profile, 43, Bytes(4, std::byte{'x'}));
        rejects(movie(profile, sprite(profile, 0, {{invalid_label}}), 1),
                profile, "label needs NUL inside record");
    }
}

void test_atlas() {
    const auto profile = Profile::xbox_2_5;
    const auto root = sprite(profile, 0,
                             {{definition_record(profile, 20, 2),
                               definition_record(profile, 20, 1)}});
    const std::array atlas{AtlasFixture{1, 0xffffffffU}, AtlasFixture{2, 1234}};
    const auto source = movie(profile, root, 3, atlas);
    const auto parsed = valid(source, profile);
    check(parsed.atlas_entries().size() == 2 && parsed.images().size() == 2,
          "atlas references collected");
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& entry = parsed.atlas_entries()[i];
        check(entry.character_id == atlas[i].id &&
                  entry.page_number == atlas[i].page &&
                  entry.raw_words == atlas[i].words &&
                  entry.raw_floats == atlas[i].floats,
              "all32 source bytes represented without narrowing page");
        check(entry.source_offset == 72 + root.bytes.size() + 8 + 32 * i,
              "atlas source offset");
    }
    for (const auto& image : parsed.images()) {
        check(image.source == VmfImageSource::atlas_page &&
                  image.atlas_entry_index,
              "image has explicit atlas linkage");
        if (image.atlas_entry_index) {
            const auto& entry =
                parsed.atlas_entries()[*image.atlas_entry_index];
            check(image.character_id == entry.character_id &&
                      image.source_number == entry.page_number,
                  "image resolves by ID, not table/physical order");
        }
    }
    auto duplicate = atlas;
    duplicate[1].id = 1;
    rejects(movie(profile, root, 3, duplicate), profile, "duplicate TXIX ID",
            Error::invalid_reference);
    auto missing = atlas;
    missing[1].id = 3;
    rejects(movie(profile, root, 3, missing), profile,
            "missing TXIX definition", Error::invalid_reference);
    const auto wrong_type =
        sprite(profile, 0, {{definition_record(profile, 10, 1)}});
    const std::array one_entry{AtlasFixture{1, 0}};
    rejects(movie(profile, wrong_type, 2, one_entry), profile,
            "TXIX must reference type20", Error::invalid_reference);
    for (const auto value :
         {-0.01F, 1.01F, std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = atlas;
        bad[0].floats[2] = value;
        rejects(movie(profile, root, 3, bad), profile,
                "TXIX float must be finite in unit interval",
                Error::invalid_numeric);
    }
    const auto pc_root = sprite(Profile::pc_2_8, 0,
                                {{definition_record(Profile::pc_2_8, 20, 1)}});
    rejects(movie(Profile::pc_2_8, pc_root, 2, one_entry), Profile::pc_2_8,
            "PC nonempty TXIX not observed", Error::unsupported_layout);
    const auto empty = valid(minimal(profile), profile);
    check(empty.atlas_entries().empty(), "Xbox empty TXIX allowed");
}

void test_limits() {
    struct LimitCase {
        Bytes bytes;
        Profile profile;
        std::size_t VmfLimits::* member;
        std::size_t exact;
    };
    const auto pc = Profile::pc_2_8;
    const auto xb = Profile::xbox_2_5;
    const auto base = minimal(pc);
    const auto nested =
        movie(pc, sprite(pc, 0, {{sprite(pc, 1, {{}}, false).bytes}}), 2);
    const auto script = timeline_movie(pc, branch_stream(pc));
    const Bytes text{std::byte{'L'}, std::byte{}};
    const auto image_root = sprite(xb, 0, {{definition_record(xb, 20, 1)}});
    const std::array one_entry{AtlasFixture{1, 99}};
    const std::vector<LimitCase> cases{
        {base, pc, &VmfLimits::max_input_bytes, base.size()},
        {base, pc, &VmfLimits::max_records, 3},
        {nested, pc, &VmfLimits::max_sprites, 2},
        {nested, pc, &VmfLimits::max_frames, 2},
        {nested, pc, &VmfLimits::max_definitions, 2},
        {nested, pc, &VmfLimits::max_depth, 2},
        {script, pc, &VmfLimits::max_scripts, 1},
        {script, pc, &VmfLimits::max_actions, 7},
        {script, pc, &VmfLimits::max_branches, 1},
        {movie(pc, sprite(pc, 0, {{button(pc, 1, 0)}}), 2), pc,
         &VmfLimits::max_button_visuals, 1},
        {movie(pc, sprite(pc, 0, {{record(pc, 43, text)}}), 1), pc,
         &VmfLimits::max_labels, 1},
        {movie(xb, image_root, 2), xb, &VmfLimits::max_images, 1},
        {movie(xb, image_root, 2, one_entry), xb, &VmfLimits::max_atlas_entries,
         1}};
    for (const auto& item : cases) {
        VmfLimits limits;
        limits.*(item.member) = item.exact;
        static_cast<void>(valid(item.bytes, item.profile, limits));
        limits.*(item.member) = item.exact - 1;
        rejects(item.bytes, item.profile, "one less than exact quota",
                Error::resource_limit, limits);
    }
    const std::array all_members{
        &VmfLimits::max_input_bytes,    &VmfLimits::max_output_bytes,
        &VmfLimits::max_records,        &VmfLimits::max_sprites,
        &VmfLimits::max_frames,         &VmfLimits::max_definitions,
        &VmfLimits::max_scripts,        &VmfLimits::max_actions,
        &VmfLimits::max_branches,       &VmfLimits::max_depth,
        &VmfLimits::max_button_visuals, &VmfLimits::max_labels,
        &VmfLimits::max_images,         &VmfLimits::max_atlas_entries};
    VmfLimits very_large;
    for (const auto member : all_members) {
        very_large.*member = std::numeric_limits<std::size_t>::max();
    }
    static_cast<void>(valid(script, pc, very_large));
    VmfLimits empty_optional;
    for (const auto member :
         {&VmfLimits::max_scripts, &VmfLimits::max_actions,
          &VmfLimits::max_branches, &VmfLimits::max_button_visuals,
          &VmfLimits::max_labels, &VmfLimits::max_images,
          &VmfLimits::max_atlas_entries}) {
        empty_optional.*member = 0;
    }
    static_cast<void>(valid(base, pc, empty_optional));
    for (const auto member :
         {&VmfLimits::max_input_bytes, &VmfLimits::max_output_bytes,
          &VmfLimits::max_records, &VmfLimits::max_sprites,
          &VmfLimits::max_frames, &VmfLimits::max_definitions,
          &VmfLimits::max_depth}) {
        VmfLimits zero;
        zero.*member = 0;
        rejects(base, pc, "required zero quota effective",
                Error::resource_limit, zero);
    }
    const auto expected_base_output =
        base.size() + sizeof(VmfHeader) + 3 * sizeof(VmfRecord) +
        sizeof(VmfSprite) + sizeof(VmfFrame) + sizeof(VmfDefinition);
    VmfLimits exact;
    exact.max_output_bytes = expected_base_output;
    const auto p = valid(base, pc, exact);
    check(p.logical_output_bytes() == expected_base_output,
          "minimal logical budget from fixed oracle counts");
    check(p.logical_output_bytes() == logical_size(p),
          "public logical accounting contract");
    --exact.max_output_bytes;
    rejects(base, pc, "logical output budget one byte short",
            Error::resource_limit, exact);

    // Preflight must reject input quota without trying to copy it.
    VmfLimits too_small;
    too_small.max_input_bytes = 0;
    vmf_allocation_fixture::calls = 0;
    vmf_allocation_fixture::fail_at = 0;
    vmf_allocation_fixture::enabled = true;
    const auto refused = VmfMovie::parse(base, pc, too_small);
    vmf_allocation_fixture::enabled = false;
    check(!refused && refused.error().code == Error::resource_limit,
          "input preflight returns resource_limit");
    check(vmf_allocation_fixture::calls.load() == 0,
          "input quota before ordinary allocation");
}

void test_ownership_and_ranges() {
    for (const auto profile : profiles) {
        auto source = timeline_movie(profile, branch_stream(profile));
        const auto oracle = source;
        Bytes guarded(16, std::byte{0xc7});
        append(guarded, source);
        guarded.insert(guarded.end(), 16, std::byte{0x3a});
        const auto canaries = guarded;
        static_cast<void>(
            valid(std::span(guarded).subspan(16, source.size()), profile));
        check(equal_bytes(guarded, canaries),
              "span-offset input and both canaries unchanged");
        auto parsed = valid(source, profile);
        check(parsed.source_bytes().data() != source.data(),
              "owning input copy");
        std::fill(source.begin(), source.end(), std::byte{0xaa});
        source.clear();
        source.shrink_to_fit();
        check(equal_bytes(parsed.source_bytes(), oracle),
              "input may be destroyed after parse");
        auto copy = parsed;
        check(copy.source_bytes().data() != parsed.source_bytes().data(),
              "movie copy owns independent source");
        const auto size = narrow(oracle.size());
        const auto eof = copy.bytes({size, 0});
        check(eof && eof->empty(), "empty range exactly at EOF");
        const auto whole = copy.bytes({0, size});
        check(whole && equal_bytes(*whole, oracle), "whole owned source range");
        for (const auto range :
             {VmfByteRange{size, 1}, VmfByteRange{size + 1, 0},
              VmfByteRange{0xffffffffU, 0}, VmfByteRange{1, 0xffffffffU},
              VmfByteRange{0xffffffffU, 0xffffffffU}}) {
            const auto result = copy.bytes(range);
            check(!result && result.error().code == Error::invalid_range,
                  "range arithmetic without u32/size_t overflow");
        }
        parsed = valid(minimal(profile), profile);
        check(equal_bytes(copy.source_bytes(), oracle) &&
                  copy.branch_count() == 1,
              "copy survives reassignment of original");
        auto moved = std::move(copy);
        check(equal_bytes(moved.source_bytes(), oracle),
              "moved destination keeps owning ranges");
        // Never consult moved-from object or use a pre-move span.
        for (const auto& action : moved.actions()) {
            const auto payload = moved.bytes(action.payload);
            check(payload.has_value(), "action ranges valid after owning move");
        }
    }
}

void test_allocation_failures() {
    const auto profile = Profile::xbox_2_5;
    const auto bytes =
        movie(profile,
              sprite(profile, 0,
                     {{button(profile, 1, 0),
                       record(profile, 12, branch_stream(profile))}}),
              2);
    bool completed = false;
    for (std::size_t fail_at = 0; fail_at < 256; ++fail_at) {
        vmf_allocation_fixture::calls = 0;
        vmf_allocation_fixture::fail_at = fail_at;
        vmf_allocation_fixture::enabled = true;
        std::optional<std::expected<VmfMovie, VmfError>> result;
        try {
            result.emplace(VmfMovie::parse(bytes, profile));
        } catch (...) {
            vmf_allocation_fixture::enabled = false;
            check(false, "allocation failure escaped expected API");
            continue;
        }
        vmf_allocation_fixture::enabled = false;
        if (result->has_value()) {
            check(vmf_allocation_fixture::calls.load() <= fail_at,
                  "success only when fault ordinal not reached");
            completed = true;
            break;
        }
        check(result->error().code == Error::resource_limit,
              "every injected ordinary allocation failure reported");
        check(!result->error().detail.empty(),
              "allocation failure static detail survives catch");
    }
    check(completed, "allocation fault sweep reached successful parse");
}

void test_copy_assignment_failure() {
    // A owns a large byte buffer but few records, with EVERY public collection
    // nonempty. B has a much shorter buffer but many more physical records.
    // The old memberwise assignment could reuse A's source before throwing
    // while allocating B's records, leaving a hybrid validated movie.
    const auto xb = Profile::xbox_2_5;
    const auto pc = Profile::pc_2_8;
    const Bytes old_label{std::byte{'o'}, std::byte{'l'}, std::byte{'d'},
                          std::byte{}};
    const auto child = sprite(
        xb, 4,
        {{record(xb, 43, old_label)}, {record(xb, 12, branch_stream(xb))}},
        false);
    const auto root =
        sprite(xb, 0,
               {{record(xb, 9, Bytes(1024U * 1024U, std::byte{0x6d})),
                 definition_record(xb, 20, 1), record(xb, 43, old_label),
                 button(xb, 2, 1)},
                {definition_record(xb, 10, 3), child.bytes,
                 record(xb, 12, branch_stream(xb))}});
    const std::array atlas{AtlasFixture{1, 7}};
    const auto a_input = movie(xb, root, 5, atlas);
    FrameItems many_records;
    for (std::size_t i = 0; i < 1000; ++i) {
        many_records.push_back(record(pc, 9));
    }
    many_records.push_back(definition_record(pc, 10, 1));
    const auto b_input = movie(pc, sprite(pc, 0, {many_records}), 2);
    const auto original = valid(a_input, xb);
    const auto other = valid(b_input, pc);
    const auto expected_a = snapshot(original);
    const auto expected_b = snapshot(other);
    check(a_input.size() > b_input.size() * 10 &&
              original.records().size() < other.records().size(),
          "assignment fixture has large source versus many records");
    check(!original.sprites().empty() && !original.frames().empty() &&
              !original.definitions().empty() && !original.scripts().empty() &&
              !original.actions().empty() &&
              !original.button_visuals().empty() &&
              !original.labels().empty() && !original.images().empty() &&
              !original.atlas_entries().empty() && original.branch_count() != 0,
          "failure snapshot exercises every decoded collection");

    const auto previous_enabled = vmf_allocation_fixture::enabled.load();
    const auto previous_calls = vmf_allocation_fixture::calls.load();
    const auto previous_fail_at = vmf_allocation_fixture::fail_at.load();
    const auto previous_bytes = vmf_allocation_fixture::failure_bytes.load();
    const auto previous_owner_only = vmf_allocation_fixture::owner_only.load();
    const auto previous_owner = vmf_allocation_fixture::owner_thread;
    const auto metadata_bytes = other.records().size() * sizeof(VmfRecord);
    bool metadata_failure_seen = false;
    std::size_t observed_faults = 0;
    for (std::size_t ordinal = 0; ordinal < 16 && !metadata_failure_seen;
         ++ordinal) {
        auto destination = original;
        bool allocation_failed = false;
        bool unexpected_exception = false;
        std::size_t failed_bytes = 0;
        {
            AllocationFailureScope fault(ordinal);
            try {
                destination = other;
            } catch (const std::bad_alloc&) {
                allocation_failed = true;
            } catch (...) {
                unexpected_exception = true;
            }
            failed_bytes = fault.failure_bytes();
        }
        // All comparisons and diagnostics run AFTER RAII disarms the fault.
        check(!unexpected_exception,
              "copy assignment has no unexpected exception type");
        check(vmf_allocation_fixture::enabled.load() == previous_enabled &&
                  vmf_allocation_fixture::calls.load() == previous_calls &&
                  vmf_allocation_fixture::fail_at.load() == previous_fail_at &&
                  vmf_allocation_fixture::failure_bytes.load() ==
                      previous_bytes &&
                  vmf_allocation_fixture::owner_only.load() ==
                      previous_owner_only &&
                  vmf_allocation_fixture::owner_thread == previous_owner,
              "allocation fault RAII restored every fixture state");
        if (!allocation_failed) {
            check(same_snapshot(snapshot(destination), expected_b),
                  "unfaulted assignment succeeds wholly");
            break;
        }
        ++observed_faults;
        const auto after = snapshot(destination);
        check(equal_bytes(after.source, expected_a.source),
              "failed copy assignment preserves complete owned source");
        check(equal_bytes(after.metadata, expected_a.metadata),
              "failed copy assignment preserves ALL "
              "header,collections,IDs,ranges,counters");
        check(same_snapshot(snapshot(other), expected_b),
              "failed assignment leaves source movie unchanged");
        // Allocators may add alignment bookkeeping to the requested buffer.
        // B's records are its only allocation this large; its source is much
        // smaller. Do not require an MSVC allocator's exact byte overhead.
        metadata_failure_seen = failed_bytes >= metadata_bytes &&
                                failed_bytes > other.source_bytes().size();
        if (metadata_failure_seen) {
            std::cout << "Copy assignment metadata fault: ordinal=" << ordinal
                      << " bytes=" << failed_bytes
                      << " destination_source_unchanged="
                      << equal_bytes(after.source, expected_a.source)
                      << " all_metadata_unchanged="
                      << equal_bytes(after.metadata, expected_a.metadata)
                      << '\n';
        }
        destination = other;
        check(same_snapshot(snapshot(destination), expected_b),
              "copy assignment succeeds after fault disarmed");
    }
    check(observed_faults != 0 && metadata_failure_seen,
          "sweep really failed a metadata allocation, not only the input copy");

    auto self = original;
    const auto& self_alias = self;
    self = self_alias;
    check(same_snapshot(snapshot(self), expected_a),
          "self copy assignment preserves complete movie");
    auto destination = original;
    auto moved_destination = std::move(destination);
    // Assignment TO a moved-from object is allowed by the frozen contract;
    // it is never queried or used as the source until assignment succeeds.
    destination = other;
    check(same_snapshot(snapshot(destination), expected_b),
          "copy assign TO moved-from destination");
    check(same_snapshot(snapshot(moved_destination), expected_a),
          "moved owner unaffected by destination reassignment");
    check(same_snapshot(snapshot(original), expected_a) &&
              same_snapshot(snapshot(other), expected_b),
          "both fixture source movies remain wholly unchanged");
}

void test_private_corpus(const std::filesystem::path& directory) {
    std::size_t movies = 0, xbox = 0, pc = 0, bytes = 0, records = 0,
                sprites = 0;
    std::size_t frames = 0, scripts = 0, actions = 0, branches = 0;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file() || item.path().extension() != ".vmf") {
            continue;
        }
        if (++movies > 39 || item.file_size() > 64U * 1024U * 1024U) {
            throw std::runtime_error("private corpus exceeds frozen bounds");
        }
        const auto name = item.path().filename().string();
        const auto profile =
            name.starts_with("xbox_") ? Profile::xbox_2_5 : Profile::pc_2_8;
        check(name.starts_with("xbox_") || name.starts_with("pc_"),
              "private identity naming profile");
        Bytes input(static_cast<std::size_t>(item.file_size()));
        std::ifstream stream(item.path(), std::ios::binary);
        stream.read(reinterpret_cast<char*>(input.data()),
                    static_cast<std::streamsize>(input.size()));
        if (!stream ||
            stream.gcount() != static_cast<std::streamsize>(input.size())) {
            throw std::runtime_error("private corpus read failed");
        }
        const auto parsed = valid(input, profile);
        bytes += input.size();
        records += parsed.records().size();
        sprites += parsed.sprites().size();
        frames += parsed.frames().size();
        scripts += parsed.scripts().size();
        actions += parsed.actions().size();
        branches += parsed.branch_count();
        profile == Profile::xbox_2_5 ? ++xbox : ++pc;
        check(parsed.logical_output_bytes() == logical_size(parsed),
              "corpus logical accounting");
        for (const auto& script : parsed.scripts()) {
            check(script.action_count != 0 &&
                      script.first_action <= parsed.actions().size() &&
                      script.action_count <=
                          parsed.actions().size() - script.first_action,
                  "corpus action slice bounded");
            if (script.action_count == 0 ||
                script.first_action > parsed.actions().size() ||
                script.action_count >
                    parsed.actions().size() - script.first_action) {
                continue;
            }
            const auto end = script.first_action + script.action_count;
            check(parsed.actions()[end - 1].opcode == 0,
                  "corpus explicit End action");
            for (std::size_t i = script.first_action; i < end; ++i) {
                const auto& a = parsed.actions()[i];
                if (!a.branch) {
                    continue;
                }
                check(a.branch->target_action_index >= script.first_action &&
                          a.branch->target_action_index < end,
                      "corpus branch stays inside own script");
                if (a.branch->target_action_index < parsed.actions().size()) {
                    const auto& target =
                        parsed.actions()[a.branch->target_action_index];
                    const auto compact_target =
                        static_cast<std::int64_t>(a.compact_offset) + 5 +
                        a.branch->delta;
                    check(compact_target == target.compact_offset,
                          "corpus signed compact branch equality");
                }
            }
        }
    }
    check(movies == 39 && xbox == 20 && pc == 19 && bytes == 23'844'792,
          "frozen39 corpus totals");
    check(records == 503'752 && sprites == 5'349 && frames == 132'788,
          "independent structural corpus totals");
    check(scripts == 27'685 && actions == 520'158 && branches == 31'816,
          "independent action corpus totals");
    std::cout << "Private corpus: " << movies << " movies, " << records
              << " records, " << actions << " actions, " << branches
              << " branches\n";
}
} // namespace

int main(int argc, char** argv) {
    // The first ten source-owned oracles preceded src/assets/vmf_movie.cpp.
    // The copy-assignment regression was frozen before its ownership fix.
    // No commercial bytes embedded.
    const std::array tests{test_profiles_and_crc,
                           test_hierarchy_frames_and_eof,
                           test_definitions_and_known_opaque_records,
                           test_branches_and_scripts,
                           test_action_forms_and_raw_strings,
                           test_buttons_and_labels,
                           test_atlas,
                           test_limits,
                           test_ownership_and_ranges,
                           test_allocation_failures,
                           test_copy_assignment_failure};
    for (const auto test : tests) {
        try {
            test();
        } catch (const std::exception& error) {
            vmf_allocation_fixture::enabled = false;
            check(false, error.what());
        }
    }
    if (argc == 3 && std::string_view(argv[1]) == "--corpus") {
        try {
            test_private_corpus(std::filesystem::path(argv[2]));
        } catch (const std::exception& error) {
            check(false, error.what());
        }
    } else if (argc != 1) {
        check(false, "usage: vmf_movie_test [--corpus selected-vmf-directory]");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

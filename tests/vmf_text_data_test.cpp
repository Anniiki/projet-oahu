#include "tdur/assets/vmf_text_data.hpp"
#include "tdur/ui/vmf_display_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace Allocation {
thread_local bool enabled = false;
thread_local std::size_t calls = 0;
thread_local std::size_t fail_at = std::numeric_limits<std::size_t>::max();
void *allocate(std::size_t size) {
    if (enabled && calls++ == fail_at)
        throw std::bad_alloc();
    if (auto *result = std::malloc(size ? size : 1))
        return result;
    throw std::bad_alloc();
}
struct Scope {
    explicit Scope(std::size_t failure) {
        calls = 0;
        fail_at = failure;
        enabled = true;
    }
    ~Scope() { enabled = false; }
};
} // namespace Allocation
void *operator new(std::size_t size) { return Allocation::allocate(size); }
void *operator new[](std::size_t size) { return Allocation::allocate(size); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace tdur::assets;
using Bytes = std::vector<std::byte>;
std::size_t checks = 0, oom_cases = 0;
void Check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
template <class T> T Take(std::expected<T, VmfError> value) {
    Check(value.has_value(), value ? "ok" : value.error().detail.data());
    return std::move(*value);
}
template <class T>
concept TemporaryFields = requires(T &&value) { std::move(value).fields(); };
template <class T>
concept TemporaryText = requires(T &&value) { std::move(value).text_data(); };
static_assert(!TemporaryFields<VmfTextData>);
static_assert(!TemporaryFields<const VmfTextData>);
static_assert(!TemporaryText<tdur::ui::VmfProgram>);
static_assert(std::is_nothrow_move_constructible_v<VmfTextData>);
static_assert(!std::is_default_constructible_v<VmfTextData>);

void Put(Bytes &bytes, std::size_t at, std::uint32_t value, std::size_t width,
         bool big) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto shift = static_cast<unsigned>(big ? width - 1 - i : i) * 8;
        bytes.at(at + i) =
            std::byte{static_cast<unsigned char>(value >> shift)};
    }
}
VmfProfile Profile(bool big) {
    return big ? VmfProfile::xbox_2_5 : VmfProfile::pc_2_8;
}
Bytes Record(std::uint32_t type, std::size_t size, std::uint32_t id, bool big) {
    Bytes value(size);
    Put(value, 0, type, 4, big);
    Put(value, 4, static_cast<std::uint32_t>(size), 4, big);
    Put(value, 8, id, type == 10 ? 2 : 4, big);
    return value;
}
Bytes Image(bool big) { return Record(20, big ? 44U : 40U, 1, big); }
Bytes Font(bool big, std::uint32_t companion = 1) {
    auto value = Record(10, 16, 2, big);
    // The definition ID is u16. These nonzero neighbors must not enter it.
    Put(value, 10, 0xe1a7, 2, big);
    Put(value, 12, companion, 4, big);
    return value;
}
Bytes Text(bool big, std::string_view name = "local:field",
           std::string_view text = "Source-owned synthetic initial text",
           std::uint32_t id = 3) {
    const std::size_t name_at = big ? 0x30 : 0x32;
    const std::size_t text_at = big ? 0x74 : 0x78;
    auto value =
        Record(37, (text_at + text.size() + 1 + 3) & ~std::size_t{3}, id, big);
    Put(value, 0x1c, 2, 2, big);
    value[0x2e] = std::byte{0x18};
    value[0x2f] = std::byte{0xa5};
    if (!big)
        Put(value, 0x30, 128, 2, false);
    Check(name.size() < 60, "Fixture name fits fixed storage");
    std::fill_n(value.begin() + name_at, 60, std::byte{0x79});
    for (std::size_t i = 0; i < name.size(); ++i)
        value[name_at + i] = std::byte{static_cast<unsigned char>(name[i])};
    value[name_at + name.size()] = std::byte{};
    for (std::size_t i = 0; i < text.size(); ++i)
        value[text_at + i] = std::byte{static_cast<unsigned char>(text[i])};
    return value;
}
Bytes Movie(bool big, std::span<const Bytes> records,
            std::uint16_t definitions = 6) {
    Bytes root(28);
    Put(root, 0, 39, 4, big);
    Put(root, 4, 28, 4, big);
    Put(root, 12, 1, 2, big);
    Put(root, 14, 4, 2, big);
    Put(root, 20, 28, 4, big);
    for (const auto &record : records)
        root.insert(root.end(), record.begin(), record.end());
    // Every ID below definition_count is required by the existing reader.
    // Unused synthetic IDs are opaque shapes, not missing source slots.
    for (std::uint32_t id = 1; id < definitions; ++id) {
        const bool present = std::ranges::any_of(records, [&](const Bytes &r) {
            std::uint32_t actual = 0;
            const auto type = big ? std::to_integer<unsigned>(r[3])
                                  : std::to_integer<unsigned>(r[0]);
            const std::size_t width = type == 10 ? 2 : 4;
            for (std::size_t i = 0; i < width; ++i) {
                const auto shift =
                    static_cast<unsigned>(big ? width - 1 - i : i) * 8;
                actual |= std::to_integer<std::uint32_t>(r[8 + i]) << shift;
            }
            return actual == id;
        });
        if (!present) {
            const auto filler = Record(2, 12, id, big);
            root.insert(root.end(), filler.begin(), filler.end());
        }
    }
    const auto show = root.size();
    root.resize(show + 8);
    Put(root, show, 1, 4, big);
    Put(root, show + 4, 8, 4, big);
    Put(root, 24, static_cast<std::uint32_t>(root.size()), 4, big);
    root.resize(root.size() + 8);
    Put(root, 16, static_cast<std::uint32_t>(root.size()), 4, big);
    Bytes output(72);
    output[0] = std::byte{'V'};
    output[1] = std::byte{'M'};
    output[2] = std::byte{'F'};
    output[5] = std::byte{2};
    Put(output, 6, big ? 5U : 8U, 2, big);
    output[8] = big ? std::byte{1} : std::byte{2};
    for (std::size_t i = 0; i < 4; ++i)
        Put(output, 44 + 4 * i,
            std::bit_cast<std::uint32_t>(i % 2 ? 100.f : 0.f), 4, big);
    Put(output, 60, 60, 2, big);
    Put(output, 62, definitions, 2, big);
    Put(output, 64, 4, 2, big);
    Bytes canonical(output.begin(), output.begin() + 68);
    if (big) {
        for (auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : canonical) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    Put(output, 68, crc ^ 0xffffffffU, 4, big);
    output.insert(output.end(), root.begin(), root.end());
    const auto tail = output.size();
    output.resize(tail + 8);
    Put(output, tail, 0x54584958, 4, big);
    return output;
}
VmfMovie Parse(const Bytes &bytes, bool big) {
    return Take(VmfMovie::parse(bytes, Profile(big)));
}
VmfTextData Decode(const std::vector<Bytes> &records, bool big) {
    return Take(VmfTextData::decode(Parse(Movie(big, records), big)));
}
void Error(const std::vector<Bytes> &records, bool big, VmfErrorCode code) {
    auto movie = Parse(Movie(big, records), big);
    const auto original = movie.source_bytes();
    const Bytes before(original.begin(), original.end());
    auto result = VmfTextData::decode(movie);
    Check(!result && result.error().code == code, "Malformed text rejected");
    Check(std::ranges::equal(movie.source_bytes(), before),
          "Rejected decoding preserves source bytes");
}

void Profiles(bool big) {
    auto source = Movie(big, std::vector{Image(big), Font(big), Text(big)});
    const auto movie = Parse(source, big);
    auto data = Take(VmfTextData::decode(movie));
    const auto &field = data.fields()[0];
    Check(data.profile() == Profile(big) && data.fields().size() == 1,
          "Explicit profile and field count");
    Check(field.character_id == 3 && field.record_index == *movie.definition(3),
          "Resource identity and physical record index");
    Check(field.font_id == 2 && field.font_record_index == movie.definition(2),
          "Font ID is u16 in both profiles");
    Check(field.companion_character_id == 1U &&
              field.companion_record_index == movie.definition(1),
          "Font companion reference");
    Check(field.raw_flags_2e == 0x18 && field.raw_2f == 0xa5,
          "Uninterpreted flag bytes preserved");
    Check(field.raw_30 ==
              (big ? std::optional<std::uint16_t>{}
                   : std::optional<std::uint16_t>{std::uint16_t{128}}),
          "PC-only word never read out of Xbox name");
    Check(std::string_view(field.variable_name.data()) == "local:field" &&
              field.variable_name.back() == 0,
          "Fixed name prefix owned, unused bytes ignored");
    Check(field.initial_text == "Source-owned synthetic initial text",
          "Initial text begins at profile-specific offset");
    Check(data.field_index(3) == std::size_t{0} && !data.field_index(0) &&
              !data.field_index(9999),
          "Index zero distinct from missing");
    std::fill(source.begin(), source.end(), std::byte{0xcc});
    Check(field.initial_text == "Source-owned synthetic initial text",
          "Decoded strings do not borrow caller bytes");
    auto copy = data;
    data = Decode({Image(big), Font(big), Text(big, "", "")}, big);
    Check(copy.fields()[0].initial_text != data.fields()[0].initial_text,
          "Copied model owns independent text");
    auto sorted = Decode({Image(big), Font(big), Text(big, "five", "", 5),
                          Text(big, "three", "", 3)},
                         big);
    Check(sorted.fields()[0].character_id == 3 &&
              sorted.fields()[1].character_id == 5 &&
              sorted.field_index(5) == std::size_t{1},
          "ID lookup independent from physical record order");
    auto maximum_name = Decode(
        {Image(big), Font(big), Text(big, std::string(59, 'q'), "")}, big);
    Check(std::string_view(maximum_name.fields()[0].variable_name.data())
                  .size() == 59,
          "59-byte name accepted");
    const auto companion = Decode({Image(big), Font(big, 4), Text(big)}, big);
    Check(companion.fields()[0].companion_character_id == 4U &&
              companion.fields()[0].companion_record_index.has_value(),
          "Static companion retained without assuming type20/glyph rendering");
    auto empty = Decode({}, big);
    Check(empty.fields().empty() && !empty.field_index(0),
          "No text definitions");
}

void Malformed(bool big) {
    const auto error = [&](Bytes text, VmfErrorCode code) {
        Error({Image(big), Font(big), std::move(text)}, big, code);
    };
    auto value = Text(big);
    std::fill_n(value.begin() + (big ? 0x30 : 0x32), 60, std::byte{'x'});
    error(value, VmfErrorCode::invalid_range);
    value = Text(big);
    std::fill(value.begin() + (big ? 0x74 : 0x78), value.end(), std::byte{'x'});
    error(value, VmfErrorCode::truncated);
    for (const auto offset : {big ? 0x6cU : 0x70U, big ? 0x70U : 0x74U}) {
        value = Text(big);
        Put(value, offset, 0xffffffffU, 4, big);
        error(value, VmfErrorCode::unsupported_layout);
    }
    value = Text(big, "", "a");
    value.back() = std::byte{1};
    error(value, VmfErrorCode::unsupported_layout);
    value = Text(big, "", "");
    value.resize(value.size() + 4);
    Put(value, 4, static_cast<std::uint32_t>(value.size()), 4, big);
    error(value, VmfErrorCode::unsupported_layout);
    value = Text(big);
    value.resize(big ? 0x74 : 0x78);
    Put(value, 4, static_cast<std::uint32_t>(value.size()), 4, big);
    error(value, VmfErrorCode::truncated);
    value = Text(big);
    Put(value, 0x1c, 999, 2, big);
    error(value, VmfErrorCode::invalid_reference);
    value = Text(big);
    Put(value, 0x1c, 1, 2, big);
    error(value, VmfErrorCode::unsupported_layout);
    Error({Image(big), Font(big, 999), Text(big)}, big,
          VmfErrorCode::invalid_reference);
    auto short_font = Font(big);
    short_font.resize(12);
    Put(short_font, 4, 12, 4, big);
    Error({Image(big), short_font, Text(big)}, big, VmfErrorCode::truncated);
}

void BudgetsAndOwnership(bool big) {
    const auto movie =
        Parse(Movie(big, std::vector{Image(big), Font(big), Text(big)}), big);
    const auto baseline = Take(VmfTextData::decode(movie));
    VmfTextLimits limits;
    limits.max_output_bytes = baseline.logical_output_bytes();
    Check(VmfTextData::decode(movie, limits).has_value(),
          "Exact text storage budget");
    --limits.max_output_bytes;
    auto failed = VmfTextData::decode(movie, limits);
    Check(!failed && failed.error().code == VmfErrorCode::resource_limit,
          "One-byte-short text storage budget");
    limits = {};
    limits.max_fields = 0;
    Check(!VmfTextData::decode(movie, limits), "Field-count quota");
    limits = {};
    limits.max_initial_text_bytes =
        baseline.fields()[0].initial_text.size() + 1;
    Check(VmfTextData::decode(movie, limits).has_value(),
          "Text limit includes terminating NUL");
    --limits.max_initial_text_bytes;
    failed = VmfTextData::decode(movie, limits);
    Check(!failed && failed.error().code == VmfErrorCode::resource_limit,
          "Terminator outside text byte quota refused");
    limits.max_initial_text_bytes = 0;
    Check(!VmfTextData::decode(movie, limits), "Zero text-byte quota");

    for (std::size_t failure = 0;; ++failure) {
        auto decoded = [&] {
            Allocation::Scope allocation(failure);
            return VmfTextData::decode(movie);
        }();
        if (decoded)
            break;
        ++oom_cases;
        Check(decoded.error().code == VmfErrorCode::resource_limit,
              "Each decoding allocation failure is contained");
        Check(failure < 32, "Bounded decoding allocation attempts");
    }
    auto plain = Take(tdur::ui::VmfProgram::build(movie));
    Check(!plain->text_data(), "Existing build remains explicit without text");
    auto program = Take(tdur::ui::VmfProgram::build(
        movie, {}, 512U * 1024U * 1024U, VmfTextLimits{}));
    Check(program->text_data() &&
              program->text_data()->fields()[0].initial_text ==
                  baseline.fields()[0].initial_text,
          "Program owns matching decoded text");
    Check(program->logical_output_bytes() ==
              plain->logical_output_bytes() + baseline.logical_output_bytes(),
          "Optional text is charged against program budget");
    auto too_small = tdur::ui::VmfProgram::build(
        movie, {}, program->logical_output_bytes() - 1, VmfTextLimits{});
    Check(!too_small && too_small.error().code == VmfErrorCode::resource_limit,
          "Program cannot hide text storage outside total quota");
    // Malformed text remains opaque unless the caller requests this decoder.
    auto bad = Text(big);
    Put(bad, big ? 0x6c : 0x70, 42, 4, big);
    const auto opaque =
        Parse(Movie(big, std::vector{Image(big), Font(big), bad}), big);
    Check(tdur::ui::VmfProgram::build(opaque).has_value(),
          "Old program does not silently enable new text validation");
    Check(!tdur::ui::VmfProgram::build(opaque, {}, 512U * 1024U * 1024U,
                                       VmfTextLimits{}),
          "Opt-in validation reports malformed text");
}
} // namespace

int main() {
    try {
        for (bool big : {false, true}) {
            Profiles(big);
            Malformed(big);
            BudgetsAndOwnership(big);
        }
        Check(oom_cases >= 4,
              "Vector and owned-string OOM checked per profile");
        std::cout << "vmf_text_data: " << checks << " checks, " << oom_cases
                  << " allocation failures contained\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

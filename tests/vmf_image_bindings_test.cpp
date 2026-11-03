#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/vmf_image_bindings.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Allocation {
thread_local bool enabled{};
thread_local std::size_t calls{},
    fail_at = std::numeric_limits<std::size_t>::max();
void* allocate(std::size_t bytes) {
    if (enabled && calls++ == fail_at)
        throw std::bad_alloc();
    if (auto* pointer = std::malloc(bytes ? bytes : 1))
        return pointer;
    throw std::bad_alloc();
}
struct Scope {
    explicit Scope(
        std::size_t failure = std::numeric_limits<std::size_t>::max()) {
        calls = 0;
        fail_at = failure;
        enabled = true;
    }
    ~Scope() {
        enabled = false;
        fail_at = std::numeric_limits<std::size_t>::max();
    }
};
} // namespace Allocation
void* operator new(std::size_t bytes) { return Allocation::allocate(bytes); }
void* operator new[](std::size_t bytes) { return Allocation::allocate(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {
using namespace tdur::assets;
using Bytes = std::vector<std::byte>;
using Code = VmfImageBindingErrorCode;
using Resolution = std::expected<VmfImageBindings, VmfImageBindingError>;
constexpr auto NoFailure = std::numeric_limits<std::size_t>::max();
std::size_t checks{}, oom_resolutions{}, oom_copies{}, corpus_cases{},
    corpus_images{}, corpus_resources{}, pc_catalogues{};
void Check(bool value, std::string_view message) {
    ++checks;
    if (!value)
        throw std::runtime_error(std::string(message));
}
template <class T>
concept TemporaryResources =
    requires(T&& value) { std::move(value).resources(); };
template <class T>
concept TemporaryBindings =
    requires(T&& value) { std::move(value).bindings(); };
template <class T>
concept TemporaryLookup =
    requires(T&& value) { std::move(value).binding_index(1); };
template <class T>
concept TemporaryProfile = requires(T&& value) { std::move(value).profile(); };
template <class T>
concept TemporaryBytes =
    requires(T&& value) { std::move(value).logical_output_bytes(); };
static_assert(!TemporaryResources<VmfImageBindings> &&
              !TemporaryResources<const VmfImageBindings>);
static_assert(!TemporaryBindings<VmfImageBindings> &&
              !TemporaryBindings<const VmfImageBindings>);
static_assert(!TemporaryLookup<VmfImageBindings> &&
              !TemporaryLookup<const VmfImageBindings>);
static_assert(!TemporaryProfile<VmfImageBindings> &&
              !TemporaryProfile<const VmfImageBindings>);
static_assert(!TemporaryBytes<VmfImageBindings> &&
              !TemporaryBytes<const VmfImageBindings>);
static_assert(!std::is_default_constructible_v<VmfImageBindings>);
static_assert(std::is_copy_constructible_v<VmfImageBindings>);
static_assert(std::is_nothrow_move_constructible_v<VmfImageBindings>);
static_assert(std::is_nothrow_move_assignable_v<VmfImageBindings>);

void Put(Bytes& bytes, std::size_t offset, std::uint32_t value,
         std::size_t width, bool big) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto shift = static_cast<unsigned>(big ? width - 1 - i : i) * 8;
        bytes.at(offset + i) = std::byte((value >> shift) & 255);
    }
}
struct Atlas {
    std::uint32_t id{}, page{};
    std::array<std::uint32_t, 2> words{0xfeed1234u, 0x80000001u};
    std::array<float, 4> raw{.75f, .25f, -0.f, 1.f};
};
Bytes MovieBytes(VmfProfile profile, std::span<const std::uint32_t> images,
                 std::span<const Atlas> atlas = {}) {
    const bool big = profile == VmfProfile::xbox_2_5;
    const auto maximum =
        images.empty() ? 0u : *std::max_element(images.begin(), images.end());
    Check(maximum < 65535, "Fixture ID exceeds dense header range");
    Bytes root(28);
    Put(root, 0, 39, 4, big);
    Put(root, 4, 28, 4, big);
    Put(root, 12, 1, 2, big);
    Put(root, 14, 0x4253, 2, big);
    Put(root, 20, 28, 4, big);
    const auto definition = [&](std::uint32_t id, bool image) {
        const auto at = root.size();
        const auto size = image ? (big ? 44u : 40u) : 12u;
        root.resize(at + size, std::byte{0x6d});
        Put(root, at, image ? 20u : 2u, 4, big);
        Put(root, at + 4, size, 4, big);
        Put(root, at + 8, id, 4, big);
    };
    for (const auto id : images)
        definition(id, true);
    for (std::uint32_t id = 1; id <= maximum; ++id)
        if (std::find(images.begin(), images.end(), id) == images.end())
            definition(id, false);
    const auto show = root.size();
    root.resize(show + 8);
    Put(root, show, 1, 4, big);
    Put(root, show + 4, 8, 4, big);
    Put(root, 24, static_cast<std::uint32_t>(root.size()), 4, big);
    root.resize(root.size() + 8); // root End type0/size0
    Put(root, 16, static_cast<std::uint32_t>(root.size()), 4, big);
    Bytes result(72);
    result[0] = std::byte{'V'};
    result[1] = std::byte{'M'};
    result[2] = std::byte{'F'};
    result[4] = std::byte{0x23};
    result[5] = std::byte{2};
    Put(result, 6, big ? 5u : 8u, 2, big);
    result[8] = big ? std::byte{1} : std::byte{2};
    result[9] = std::byte{0x42};
    Put(result, 10, big ? 12u : 14u, 2, big);
    constexpr std::string_view author = "TDUR source-owned fixture";
    for (std::size_t i = 0; i < author.size(); ++i)
        result[12 + i] = std::byte(author[i]);
    for (std::size_t i = 0; i < 4; ++i)
        Put(result, 44 + 4 * i,
            std::bit_cast<std::uint32_t>(i % 2 ? 100.f : 0.f), 4, big);
    Put(result, 60, 60, 2, big);
    Put(result, 62, maximum + 1, 2, big);
    Put(result, 64, 0x4253, 2, big);
    Bytes canonical(result.begin(), result.begin() + 68);
    if (big) {
        for (const auto at : {6u, 10u, 60u, 62u, 64u, 66u})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44u, 48u, 52u, 56u})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    std::uint32_t crc = 0xffffffffu;
    for (const auto byte : canonical) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320u : 0u);
    }
    Put(result, 68, crc ^ 0xffffffffu, 4, big);
    result.insert(result.end(), root.begin(), root.end());
    const auto tail = result.size();
    result.resize(tail + 8 + 32 * atlas.size());
    Put(result, tail, 0x54584958, 4, big);
    Put(result, tail + 4, static_cast<std::uint32_t>(atlas.size()), 4, big);
    for (std::size_t i = 0; i < atlas.size(); ++i) {
        const auto at = tail + 8 + 32 * i;
        Put(result, at, atlas[i].id, 4, big);
        Put(result, at + 4, atlas[i].page, 4, big);
        for (std::size_t j = 0; j < 2; ++j)
            Put(result, at + 8 + 4 * j, atlas[i].words[j], 4, big);
        for (std::size_t j = 0; j < 4; ++j)
            Put(result, at + 16 + 4 * j,
                std::bit_cast<std::uint32_t>(atlas[i].raw[j]), 4, big);
    }
    return result;
}
VmfMovie Parse(std::span<const std::byte> bytes, VmfProfile profile) {
    auto parsed = VmfMovie::parse(bytes, profile);
    Check(parsed.has_value(), "Independent VMF fixture rejected by reader");
    return std::move(*parsed);
}
VmfImageBindings Resolve(const VmfMovie& movie,
                         std::span<const VmfImageAsset> catalogue,
                         VmfImageBindingLimits limits = {}) {
    auto result = VmfImageBindings::resolve(movie, catalogue, limits);
    if (!result)
        std::cerr << "binding error=" << static_cast<int>(result.error().code)
                  << " at=" << result.error().source_offset << ' '
                  << result.error().detail << '\n';
    Check(result.has_value(), "Expected exact bindings were refused");
    return std::move(*result);
}
Resolution Probed(const VmfMovie& movie,
                  std::span<const VmfImageAsset> catalogue,
                  VmfImageBindingLimits limits, std::size_t failure,
                  std::size_t& calls) {
    Resolution result = std::unexpected(VmfImageBindingError{
        Code::resource_limit, 0, {}, {}, "fixture initializer"});
    {
        Allocation::Scope guard(failure);
        result = VmfImageBindings::resolve(movie, catalogue, limits);
        calls = Allocation::calls;
    }
    return result;
}
void Refuses(const VmfMovie& movie, std::span<const VmfImageAsset> catalogue,
             Code code, VmfImageBindingLimits limits = {}) {
    std::size_t calls{};
    const auto result = Probed(movie, catalogue, limits, 0, calls);
    Check(!result && result.error().code == code, "Wrong refusal category");
    Check(calls == 0, "Preflight refusal allocated result storage");
    Check(!result.error().detail.empty(), "Static error diagnostic is empty");
}
std::uint64_t Fingerprint(const VmfImageBindings& result) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto byte = [&](unsigned value) {
        hash ^= value & 255u;
        hash *= 1099511628211ULL;
    };
    const auto number = [&](std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i)
            byte(static_cast<unsigned>(value >> (8 * i)));
    };
    number(static_cast<unsigned>(result.profile()));
    number(result.logical_output_bytes());
    for (const auto& resource : result.resources()) {
        number(resource.member_index);
        number(resource.exact_name.size());
        for (const unsigned char c : resource.exact_name)
            byte(c);
    }
    for (const auto& binding : result.bindings()) {
        number(binding.image.character_id);
        number(binding.image.record_index);
        number(static_cast<unsigned>(binding.image.source));
        number(binding.image.source_number);
        number(binding.image.atlas_entry_index.has_value());
        if (binding.image.atlas_entry_index)
            number(*binding.image.atlas_entry_index);
        const auto& record = binding.definition_record;
        number(record.source_offset);
        number(record.type);
        number(record.declared_size);
        number(record.full_size);
        number(record.payload.offset);
        number(record.payload.size);
        number(record.parent_sprite_index.has_value());
        if (record.parent_sprite_index)
            number(*record.parent_sprite_index);
        number(record.definition_id.has_value());
        if (record.definition_id)
            number(*record.definition_id);
        number(binding.resource_index);
        number(binding.atlas.has_value());
        if (binding.atlas) {
            const auto& atlas = *binding.atlas;
            number(atlas.entry.source_offset);
            number(atlas.entry.character_id);
            number(atlas.entry.page_number);
            for (auto word : atlas.entry.raw_words)
                number(word);
            for (auto value : atlas.entry.raw_floats)
                number(std::bit_cast<std::uint32_t>(value));
            for (auto value : atlas.source_bytes)
                byte(std::to_integer<unsigned>(value));
        }
        const auto index = result.binding_index(binding.image.character_id);
        number(index.has_value());
        if (index)
            number(*index);
    }
    return hash;
}
void TestBasic() {
    const std::array<std::uint32_t, 3> ids{3, 1, 2};
    const auto movie =
        Parse(MovieBytes(VmfProfile::pc_2_8, ids), VmfProfile::pc_2_8);
    const std::array<VmfImageAsset, 5> catalogue{{{"fla_0001.2db", 90},
                                                  {"unused", 7},
                                                  {"fla_0002.2db", 17},
                                                  {"fla_0003.2db", NoFailure},
                                                  {"unused", 7}}};
    const auto result = Resolve(movie, catalogue);
    Check(result.profile() == VmfProfile::pc_2_8 &&
              result.bindings().size() == 3 && result.resources().size() == 3,
          "PC profile/counts");
    const std::array<std::string_view, 3> names{"fla_0003.2db", "fla_0001.2db",
                                                "fla_0002.2db"};
    const std::array<std::size_t, 3> tokens{NoFailure, 90, 17};
    for (std::size_t i = 0; i < 3; ++i) {
        Check(result.resources()[i].exact_name == names[i] &&
                  result.resources()[i].member_index == tokens[i],
              "First image order, not physical token order");
        Check(result.bindings()[i].image.character_id == ids[i] &&
                  result.bindings()[i].resource_index == i,
              "Binding follows source occurrence");
        Check(result.binding_index(ids[i]) == i && !result.bindings()[i].atlas,
              "ID lookup and direct source");
        Check(
            result.bindings()[i].definition_record.payload.offset ==
                movie.records()[movie.images()[i].record_index].payload.offset,
            "Record ranges retained as source metadata");
    }
    Check(!result.binding_index(0) && !result.binding_index(0xffffffffu),
          "Nonimage and absent IDs are not resources");
    auto reversed = catalogue;
    std::reverse(reversed.begin(), reversed.end());
    Check(Fingerprint(Resolve(movie, reversed)) == Fingerprint(result),
          "Catalogue order changes binding meaning");
    auto alias = catalogue;
    alias[0].member_index = alias[2].member_index;
    const auto aliased = Resolve(movie, alias);
    Check(aliased.resources().size() == 3,
          "Different names aliasing one token were merged");
    for (const auto wrong : {"FLA_0003.2db", "folder/fla_0003.2db",
                             "fla_00003.2db", "fla_0005.2db"}) {
        auto changed = catalogue;
        changed[3].exact_name = wrong;
        Refuses(movie, changed, Code::missing_asset);
    }
    Refuses(movie, {}, Code::missing_asset);
    std::vector<VmfImageAsset> duplicate(catalogue.begin(), catalogue.end());
    duplicate.push_back({"fla_0003.2db", 42});
    Refuses(movie, duplicate, Code::ambiguous_asset);
    duplicate.back().member_index = NoFailure;
    Refuses(movie, duplicate, Code::ambiguous_asset);
}
void TestAtlasAndNames() {
    const std::array<std::uint32_t, 4> ids{4, 3, 1, 2};
    const std::array<Atlas, 3> atlas{{{2, 12345}, {1, 7}, {4, 7}}};
    const auto bytes = MovieBytes(VmfProfile::xbox_2_5, ids, atlas);
    const auto movie = Parse(bytes, VmfProfile::xbox_2_5);
    const std::array<VmfImageAsset, 4> catalogue{{{"page_12345.2db", 77},
                                                  {"fla_0004.2db", 99},
                                                  {"fla_0003.2db", 501},
                                                  {"page_007.2db", 77}}};
    const auto result = Resolve(movie, catalogue);
    Check(result.profile() == VmfProfile::xbox_2_5 &&
              result.resources().size() == 3,
          "Xbox shared pages/profile");
    const std::array<std::size_t, 4> resources{0, 1, 0, 2};
    for (std::size_t i = 0; i < ids.size(); ++i)
        Check(result.bindings()[i].resource_index == resources[i],
              "Shared page did not share one resource");
    Check(result.resources()[0].exact_name == "page_007.2db" &&
              result.resources()[1].exact_name == "fla_0003.2db" &&
              result.resources()[2].exact_name == "page_12345.2db",
          "Atlas/direct resource order");
    const auto& first = result.bindings()[0];
    Check(first.image.atlas_entry_index == std::size_t{2} &&
              first.resource_index == 0,
          "Source TXIX index was incorrectly rebased");
    Check(first.atlas && first.atlas->entry.raw_words == atlas[2].words &&
              std::bit_cast<std::uint32_t>(first.atlas->entry.raw_floats[2]) ==
                  0x80000000u,
          "Opaque TXIX fields or negative zero changed");
    for (const auto& binding : result.bindings())
        if (binding.atlas) {
            const auto offset = binding.atlas->entry.source_offset;
            Check(std::equal(binding.atlas->source_bytes.begin(),
                             binding.atlas->source_bytes.end(),
                             bytes.begin() + offset),
                  "Raw BE TXIX bytes not owned exactly");
            Check(binding.atlas->source_bytes[0] == std::byte{} &&
                      binding.atlas->source_bytes[3] ==
                          std::byte(binding.image.character_id),
                  "TXIX source byte order changed");
        }
    VmfImageBindingLimits limits;
    limits.max_resources = 3;
    const auto shared = Resolve(movie, catalogue, limits);
    Check(shared.bindings().size() == 4,
          "Shared page charged as four resources");
    limits.max_resources = 2;
    Refuses(movie, catalogue, Code::resource_limit, limits);
    const std::array<std::uint32_t, 1> wide{10000};
    const auto pc =
        Parse(MovieBytes(VmfProfile::pc_2_8, wide), VmfProfile::pc_2_8);
    const std::array<VmfImageAsset, 1> exact{{{"fla_10000.2db", 71}}};
    const auto wideResolved = Resolve(pc, exact);
    Check(wideResolved.resources()[0].exact_name == "fla_10000.2db",
          "Direct width was truncated to four digits");
    const std::array<std::uint32_t, 1> one{1};
    const std::array<Atlas, 1> huge{{{1, 0xffffffffu}}};
    const auto xbox = Parse(MovieBytes(VmfProfile::xbox_2_5, one, huge),
                            VmfProfile::xbox_2_5);
    const std::array<VmfImageAsset, 1> page{{{"page_4294967295.2db", 17}}};
    const auto hugePage = Resolve(xbox, page);
    Check(hugePage.resources()[0].exact_name == "page_4294967295.2db",
          "Full u32 page number was narrowed/truncated");
}
void TestQuotas() {
    const std::array<std::uint32_t, 3> ids{3, 1, 2};
    const auto movie =
        Parse(MovieBytes(VmfProfile::pc_2_8, ids), VmfProfile::pc_2_8);
    const std::array<VmfImageAsset, 3> catalogue{
        {{"fla_0001.2db", 1}, {"fla_0002.2db", 2}, {"fla_0003.2db", 3}}};
    const auto result = Resolve(movie, catalogue);
    for (unsigned field = 0; field < 9; ++field) {
        VmfImageBindingLimits limit;
        switch (field) {
        case 0:
            limit.max_input_bytes = movie.source_bytes().size() - 1;
            break;
        case 1:
            limit.max_catalogue_entries = 2;
            break;
        case 2:
            limit.max_catalogue_name_bytes = 35;
            break;
        case 3:
            limit.max_name_bytes = 11;
            break;
        case 4:
            limit.max_images = 2;
            break;
        case 5:
            limit.max_resources = 2;
            break;
        case 6:
            limit.max_owned_name_bytes = 35;
            break;
        case 7:
            limit.max_output_bytes = result.logical_output_bytes() - 1;
            break;
        case 8:
            limit.max_resolution_comparisons = 23;
            break;
        }
        Refuses(movie, catalogue, Code::resource_limit, limit);
    }
    VmfImageBindingLimits exact;
    exact.max_input_bytes = movie.source_bytes().size();
    exact.max_catalogue_entries = 3;
    exact.max_catalogue_name_bytes = 36;
    exact.max_name_bytes = 12;
    exact.max_images = 3;
    exact.max_resources = 3;
    exact.max_owned_name_bytes = 36;
    exact.max_output_bytes = result.logical_output_bytes();
    exact.max_resolution_comparisons = 24;
    Check(Fingerprint(Resolve(movie, catalogue, exact)) == Fingerprint(result),
          "Exact quota boundaries rejected");
    const auto empty =
        Parse(MovieBytes(VmfProfile::pc_2_8, {}), VmfProfile::pc_2_8);
    VmfImageBindingLimits zero;
    zero.max_input_bytes = empty.source_bytes().size();
    zero.max_catalogue_entries = 0;
    zero.max_catalogue_name_bytes = 0;
    zero.max_name_bytes = 0;
    zero.max_images = 0;
    zero.max_resources = 0;
    zero.max_owned_name_bytes = 0;
    zero.max_output_bytes = 0;
    zero.max_resolution_comparisons = 0;
    std::size_t calls{};
    const auto resolved = Probed(empty, {}, zero, 0, calls);
    Check(resolved && resolved->resources().empty() &&
              resolved->bindings().empty() &&
              resolved->logical_output_bytes() == 0 && calls == 0,
          "Empty output allocated under zero quotas");
}
VmfImageBindings OwnedAfterInputsDie() {
    const std::array<std::uint32_t, 2> ids{2, 1};
    const std::array<Atlas, 2> atlas{{{1, 0xffffffffu}, {2, 7}}};
    auto input = MovieBytes(VmfProfile::xbox_2_5, ids, atlas);
    auto movie = Parse(input, VmfProfile::xbox_2_5);
    std::array<std::string, 2> names{"page_007.2db", "page_4294967295.2db"};
    const std::array<VmfImageAsset, 2> catalogue{
        {{names[0], 43}, {names[1], 99}}};
    auto result = Resolve(movie, catalogue);
    names[0].assign(1024, 'x');
    names[1].assign(1024, 'y');
    std::fill(input.begin(), input.end(), std::byte{0xcc});
    return result;
}
void TestOwnershipAndOom() {
    const auto owned = OwnedAfterInputsDie();
    Check(owned.resources()[0].exact_name == "page_007.2db" &&
              owned.resources()[1].exact_name == "page_4294967295.2db" &&
              owned.bindings()[0].atlas->entry.page_number == 7,
          "Result borrowed caller strings/movie");
    const std::array<std::uint32_t, 2> ids{2, 1};
    const std::array<Atlas, 2> atlas{{{1, 0xffffffffu}, {2, 7}}};
    const auto movie = Parse(MovieBytes(VmfProfile::xbox_2_5, ids, atlas),
                             VmfProfile::xbox_2_5);
    const std::array<VmfImageAsset, 2> catalogue{
        {{"page_007.2db", 43}, {"page_4294967295.2db", 99}}};
    std::size_t count{};
    const auto success = Probed(movie, catalogue, {}, NoFailure, count);
    Check(success && count >= 4,
          "Allocation oracle did not see buffers and long owned name");
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t observed{};
        const auto failed = Probed(movie, catalogue, {}, i, observed);
        Check(!failed && failed.error().code == Code::allocation_failure,
              "Resolve OOM escaped or became another error");
        Check(observed == i + 1 && failed.error().detail ==
                                       "VMF image binding allocation failed",
              "OOM diagnostic allocated or borrowed data");
        ++oom_resolutions;
    }
    const auto source = OwnedAfterInputsDie();
    const auto sourceHash = Fingerprint(source);
    const std::array<std::uint32_t, 3> destinationIds{3, 1, 2};
    const auto destinationMovie = Parse(
        MovieBytes(VmfProfile::pc_2_8, destinationIds), VmfProfile::pc_2_8);
    const std::array<VmfImageAsset, 3> destinationCatalogue{
        {{"fla_0001.2db", 17}, {"fla_0002.2db", 27}, {"fla_0003.2db", 37}}};
    const auto destinationSeed =
        Resolve(destinationMovie, destinationCatalogue);
    Check(Fingerprint(destinationSeed) != sourceHash &&
              destinationSeed.profile() != source.profile() &&
              destinationSeed.bindings().size() != source.bindings().size(),
          "Copy OOM must distinguish complete destination A from source B");
    auto moved = VmfImageBindings(source);
    Check(Fingerprint(moved) == sourceHash &&
              moved.resources()[1].exact_name.data() !=
                  source.resources()[1].exact_name.data(),
          "Copy did not own independent long string");
    auto assigned = VmfImageBindings(destinationSeed);
    std::size_t copyCalls{};
    {
        Allocation::Scope guard;
        assigned = source;
        copyCalls = Allocation::calls;
    }
    Check(copyCalls >= 4 && Fingerprint(assigned) == sourceHash,
          "Copy assignment baseline invalid");
    for (std::size_t i = 0; i < copyCalls; ++i) {
        auto destination = VmfImageBindings(destinationSeed);
        const auto before = Fingerprint(destination);
        const auto* names = destination.resources().data();
        bool failed = false;
        {
            Allocation::Scope guard(i);
            try {
                destination = source;
            } catch (const std::bad_alloc&) {
                failed = true;
            }
        }
        Check(failed && destination.profile() == VmfProfile::pc_2_8 &&
                  destination.resources().size() == 3 &&
                  destination.bindings().size() == 3 &&
                  Fingerprint(destination) == before &&
                  destination.resources().data() == names,
              "Failed copy assignment changed validated destination");
        Check(Fingerprint(source) == sourceHash,
              "Failed copy assignment modified source");
        ++oom_copies;
    }
    {
        Allocation::Scope guard(0);
        moved = moved;
        copyCalls = Allocation::calls;
    }
    Check(copyCalls == 0 && Fingerprint(moved) == sourceHash,
          "Self assignment allocated/changed result");
    auto final = std::move(moved);
    Check(Fingerprint(final) == sourceHash, "Move lost owning bindings");
}

std::filesystem::path Path(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
Bytes ReadBytes(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > 64U * 1024U * 1024U)
        throw std::runtime_error("Corpus VMF exceeds fixture input bound");
    Bytes result(static_cast<std::size_t>(size));
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(result.data()),
                static_cast<std::streamsize>(result.size()));
    if (!stream)
        throw std::runtime_error("Cannot read private corpus VMF");
    return result;
}
std::string Line(std::istream& stream) {
    std::string result;
    if (!std::getline(stream, result))
        throw std::runtime_error("Truncated private corpus plan");
    if (!result.empty() && result.back() == '\r')
        result.pop_back();
    return result;
}
std::size_t Number(std::string_view text) {
    std::size_t result{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("Invalid corpus integer");
    return result;
}
void Corpus(const std::filesystem::path& plan) {
    std::ifstream stream(plan);
    Check(Line(stream) == "TDUR_VMF_IMAGE_CORPUS_V1", "Corpus plan version");
    const auto count = Number(Line(stream));
    Check(count == 39, "Expected 39 previously inventoried VMFs");
    for (std::size_t c = 0; c < count; ++c) {
        const auto platform = Line(stream);
        const auto profile =
            platform == "pc" ? VmfProfile::pc_2_8 : VmfProfile::xbox_2_5;
        Check(platform == "pc" || platform == "xbox",
              "Explicit corpus profile");
        const auto moviePath = Path(Line(stream));
        const auto bankPath = Path(Line(stream));
        const auto entries = Number(Line(stream));
        std::vector<std::pair<std::size_t, std::string>> names;
        names.reserve(entries);
        for (std::size_t i = 0; i < entries; ++i) {
            const auto line = Line(stream);
            const auto tab = line.find('\t');
            Check(tab != std::string::npos, "Catalogue metadata row");
            names.emplace_back(Number(std::string_view(line).substr(0, tab)),
                               line.substr(tab + 1));
        }
        const auto expectedCount = Number(Line(stream));
        struct Expected {
            std::uint32_t id;
            std::size_t member;
            std::string name;
        };
        std::vector<Expected> expected;
        expected.reserve(expectedCount);
        for (std::size_t i = 0; i < expectedCount; ++i) {
            const auto line = Line(stream);
            const auto first = line.find('\t');
            const auto second = line.find('\t', first + 1);
            Check(first != std::string::npos && second != std::string::npos,
                  "Expected binding oracle row");
            expected.push_back({static_cast<std::uint32_t>(Number(
                                    std::string_view(line).substr(0, first))),
                                Number(std::string_view(line).substr(
                                    first + 1, second - first - 1)),
                                line.substr(second + 1)});
        }
        std::optional<BankArchive> bank;
        std::vector<VmfImageAsset> catalogue;
        catalogue.reserve(entries);
        if (profile == VmfProfile::pc_2_8) {
            auto read = BankArchive::read_file(bankPath);
            Check(read.has_value(), "Explicit PC bank validates");
            bank.emplace(std::move(*read));
            Check(bank->entries().size() == entries,
                  "PC catalogue record count");
            for (std::size_t i = 0; i < entries; ++i) {
                Check(names[i].first == i &&
                          bank->entries()[i].name == names[i].second,
                      "PC native names/physical indices match inventory");
                catalogue.push_back({bank->entries()[i].name, i});
            }
            ++pc_catalogues;
        } else {
            for (const auto& [index, name] : names)
                catalogue.push_back({name, index});
        }
        const auto movie = Parse(ReadBytes(moviePath), profile);
        const auto result = Resolve(movie, catalogue);
        Check(result.bindings().size() == expected.size(),
              "Corpus image coverage");
        for (const auto& binding : result.bindings()) {
            const auto found = std::find_if(
                expected.begin(), expected.end(), [&](const auto& item) {
                    return item.id == binding.image.character_id;
                });
            Check(found != expected.end() &&
                      binding.resource_index < result.resources().size(),
                  "Corpus ID exists in independent oracle");
            const auto& resource = result.resources()[binding.resource_index];
            Check(resource.exact_name == found->name &&
                      resource.member_index == found->member,
                  "Corpus exact name/member association");
            Check(result.binding_index(binding.image.character_id).has_value(),
                  "Corpus ID lookup");
        }
        corpus_images += result.bindings().size();
        corpus_resources += result.resources().size();
        ++corpus_cases;
    }
    Check(corpus_images == 3853 && corpus_resources == 2114 &&
              pc_catalogues == 19,
          "Independent 39-movie totals");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--corpus")
            Corpus(Path(argv[2]));
        else if (argc == 1) {
            TestBasic();
            TestAtlasAndNames();
            TestQuotas();
            TestOwnershipAndOom();
        } else
            throw std::runtime_error(
                "Use no argument or --corpus <private plan>");
        std::cout << "{\"project\":\"TDU-Recompilation\",\"scope\":\"vmf-image-"
                     "bindings\",\"bits\":"
                  << sizeof(void*) * 8 << ",\"checks\":" << checks
                  << ",\"resolve_oom_cases\":" << oom_resolutions
                  << ",\"copy_oom_cases\":" << oom_copies
                  << ",\"corpus_cases\":" << corpus_cases
                  << ",\"corpus_images\":" << corpus_images
                  << ",\"corpus_resources\":" << corpus_resources
                  << ",\"pc_native_catalogues\":" << pc_catalogues
                  << ",\"game_executed\":false,\"gpu_used\":false}\n";
    } catch (const std::exception& error) {
        std::cerr << "VMF image fixture: " << error.what() << '\n';
        return 1;
    }
}

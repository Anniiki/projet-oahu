#include "fixtures/material_2dm_fixture.hpp"
#include "fixtures/scene_mesh_fixture.hpp"
#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/material_2dm.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

using namespace tdur::assets;
namespace mf = tdur::fixtures::material;

namespace {
unsigned checks{};
unsigned failures{};
bool list_bindings{};

void check(bool value, std::string_view description) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << description << '\n';
    }
}

void reject(const mf::Bytes& bytes, Material2DMErrorCode wanted,
            std::string_view description) {
    const auto result = Material2DM::parse(bytes);
    check(!result, description);
    if (!result) {
        check(result.error().code == wanted, "specific parse error category");
        check(!result.error().detail.empty(), "parse error has a diagnosis");
    }
}

template <class T>
void binding_error(const T& result, Material2DMErrorCode wanted,
                   std::string_view description) {
    check(!result && result.error().code == wanted, description);
}

// The oracle is equality over every byte in source HASH order. It does not
// share the production lookup's sorted index or DWORD comparison routine.
std::optional<std::size_t> linear_find(const Material2DM& library,
                                       const Material2DMKey& key) {
    const auto records = library.materials();
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (records[i].key == key) {
            return i;
        }
    }
    return {};
}

void relocation_list(mf::Bytes& bytes, std::span<const std::uint32_t> fields) {
    check(fields.size() <= 16, "test REAL list fits its independent fixture");
    for (std::size_t i = 0; i < 16; ++i) {
        mf::put32(bytes, mf::Real + 16 + i * 4,
                  i < fields.size() ? fields[i] : 0);
    }
}

void layout_and_ownership() {
    auto bytes = mf::fixture();
    const auto original = bytes;
    const auto library = Material2DM::parse(bytes);
    check(bool(library), "independent four-chain fixture parses");
    if (!library) {
        std::cerr << library.error().detail << '\n';
        return;
    }
    check(library->materials().size() == 4 &&
              library->parameters().size() == 4 &&
              library->layers().size() == 4,
          "declared records retained once each");
    if (library->materials().size() != 4 || library->parameters().size() != 4 ||
        library->layers().size() != 4) {
        return;
    }
    constexpr std::array<std::uint32_t, 4> targets{0x970, 0x790, 0xa60, 0x880};
    constexpr std::array<std::size_t, 4> parameter_indexes{3, 2, 1, 0};
    constexpr std::array<std::size_t, 4> layer_indexes{3, 0, 1, 2};
    constexpr std::array<std::uint16_t, 4> classes{0x1236, 0x1234, 0x1237,
                                                   0x1235};
    constexpr std::array<std::uint32_t, 4> flags{0x89123452, 0x89123450,
                                                 0x89123453, 0x89123451};
    // Mutating and releasing caller storage must not change any output.
    std::fill(bytes.begin(), bytes.end(), std::byte{0xee});
    bytes.clear();
    bytes.shrink_to_fit();
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& material = library->materials()[i];
        check(material.key == mf::Keys[i] &&
                  material.hash_entry_offset == mf::HashEntries[i] &&
                  material.source_offset == targets[i],
              "HASH source order and nonphysical target order preserved");
        check(material.parameters_index == parameter_indexes[i] &&
                  material.classification_word == classes[i] &&
                  material.render_flags == flags[i],
              "typed links and payload fields have exact byte offsets");
        check(std::ranges::equal(
                  material.opaque_payload,
                  std::span(original).subspan(targets[i] + 16, 0xe0)),
              "owned MAT payload includes its original link word");
        const auto& parameters = library->parameters()[i];
        check(parameters.source_offset == mf::Parameters[i] &&
                  parameters.layer_index == layer_indexes[i],
              "PAR declared order and backward LAY links preserved");
        check(std::ranges::equal(
                  parameters.opaque_payload,
                  std::span(original).subspan(mf::Parameters[i] + 16,
                                              mf::ParameterSizes[i] - 16)),
              "variable PAR payload remains owned and opaque");
        const auto& layer = library->layers()[i];
        check(layer.source_offset == mf::Layers[i] &&
                  std::ranges::equal(
                      layer.opaque_payload,
                      std::span(original).subspan(mf::Layers[i] + 16, 0x110)),
              "LAY payload is owned and plausible opaque pointers ignored");
        check(library->find(mf::Keys[i]) == i,
              "lookup returns HASH ordinal, not physical MAT ordinal");
    }
    for (const auto size : {0x100U, 0x180U, 0x1a0U}) {
        const auto varied = Material2DM::parse(mf::fixture(size));
        check(varied &&
                  varied->parameters()[3].opaque_payload.size() == size - 16 &&
                  varied->materials()[0].source_offset == 0x970 + size - 0x100,
              "remaining observed PAR sizes and shifted member offsets parse");
    }
    const auto empty = Material2DM::parse(mf::empty_fixture());
    check(empty && empty->materials().empty() && empty->parameters().empty() &&
              empty->layers().empty() && !empty->find(mf::Keys[0]),
          "port accepts an explicit empty library without implicit material");
    auto wrong_size = mf::fixture(0x120);
    reject(wrong_size, Material2DMErrorCode::unsupported_layout,
           "unproven PAR size is unsupported, not silently interpreted");
}

void keys() {
    const auto baseline = Material2DM::parse(mf::fixture());
    if (!baseline) {
        check(false, "key fixture parses");
        return;
    }
    check(mf::Keys[0][0] == mf::Keys[1][0] &&
              baseline->find(mf::Keys[0]) == 0U &&
              baseline->find(mf::Keys[1]) == 1U,
          "post-NUL bytes remain part of the eight-byte identity");
    // Exercise keys differing only in each byte, either DWORD and its sign
    // bit. Public lookup cannot expose the private sorting order itself.
    for (const auto& source : mf::Keys) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            for (unsigned value = 0; value < 256; ++value) {
                auto wanted = source;
                wanted[byte] = std::byte(value);
                check(
                    baseline->find(wanted) == linear_find(*baseline, wanted),
                    "raw-key lookup agrees with independent full-byte oracle");
            }
        }
    }
    // All permutations retain source HASH order, with high/low-word extreme
    // values including unsigned 0x80000000 and 0xffffffff, without C strings.
    std::array<std::size_t, 4> order{0, 1, 2, 3};
    do {
        auto bytes = mf::fixture();
        for (std::size_t i = 0; i < order.size(); ++i) {
            mf::put_key(bytes, mf::HashEntries[i], mf::Keys[order[i]]);
        }
        const auto library = Material2DM::parse(bytes);
        check(bool(library), "raw-key permutation parses");
        if (library) {
            for (std::size_t i = 0; i < order.size(); ++i) {
                check(library->find(mf::Keys[order[i]]) == i &&
                          library->materials()[i].key == mf::Keys[order[i]],
                      "all key permutations retain HASH ordinal");
            }
        }
    } while (std::next_permutation(order.begin(), order.end()));
    auto duplicate = mf::fixture();
    mf::put_key(duplicate, 0x760, mf::Keys[0]);
    reject(duplicate, Material2DMErrorCode::invalid_hash,
           "duplicate raw keys are ambiguous even with distinct MAT targets");
}

void malformed_headers() {
    const auto baseline = mf::fixture();
    for (std::size_t size = 0; size < baseline.size(); ++size) {
        check(!Material2DM::parse(std::span(baseline).first(size)),
              "every strict prefix is refused without reading beyond it");
    }
    const auto mutate_word = [&](std::size_t at, std::uint32_t value,
                                 Material2DMErrorCode code,
                                 std::string_view description) {
        auto bytes = baseline;
        mf::put32(bytes, at, value);
        reject(bytes, code, description);
    };
    mutate_word(0, 1, Material2DMErrorCode::unsupported_version,
                "wrong PC version rejected");
    mutate_word(4, 1, Material2DMErrorCode::unsupported_layout,
                "opaque file header is not guessed");
    mutate_word(8, UINT32_MAX, Material2DMErrorCode::invalid_chunk,
                "total size mismatch rejected");
    mutate_word(0x14, 1, Material2DMErrorCode::unsupported_layout,
                "nonzero opaque chunk word is unsupported");
    mutate_word(0x18, 0, Material2DMErrorCode::invalid_chunk,
                "zero root size cannot stall the chunk walk");
    mutate_word(0x18, 0x491, Material2DMErrorCode::invalid_chunk,
                "unaligned inclusive chunk size rejected");
    mutate_word(0x18, UINT32_MAX, Material2DMErrorCode::invalid_chunk,
                "root cannot exceed the parent or wrap its bounds");
    mutate_word(0x28, UINT32_MAX, Material2DMErrorCode::invalid_chunk,
                "child cannot exceed its declared container");
    mutate_word(0x1c, 0x490, Material2DMErrorCode::unsupported_layout,
                "container must use layout 16");
    mutate_word(0x2c, 16, Material2DMErrorCode::unsupported_layout,
                "LAY leaf must use its full inclusive size as layout");
    mutate_word(0x74c, 16, Material2DMErrorCode::unsupported_layout,
                "HASH leaf layout is checked");
    mutate_word(0xb7c, 16, Material2DMErrorCode::unsupported_layout,
                "REAL leaf layout is checked");
    mutate_word(0x75c, 1, Material2DMErrorCode::unsupported_layout,
                "unknown HASH tail is not a second material pointer");
    auto wrong_marker = baseline;
    mf::put_text(wrong_marker, 12, ".3DD");
    reject(wrong_marker, Material2DMErrorCode::invalid_chunk,
           "a scene marker is not accepted as a material library");
    for (const auto at : {0x10U, 0x4a0U, 0x730U, 0xb50U, 0xb60U, 0xb70U}) {
        auto bytes = baseline;
        mf::put_text(bytes, at, "XXXX");
        reject(bytes, Material2DMErrorCode::unsupported_layout,
               "all six root identities and their positions are checked");
    }
    for (const auto at : {0x20U, 0x4b0U, 0x740U, 0x790U}) {
        auto bytes = baseline;
        mf::put_text(bytes, at, "XXXX");
        reject(bytes, Material2DMErrorCode::unsupported_layout,
               "typed leaf identity is required, not scanned from payload");
    }
    for (const auto at : {0xb50U, 0xb60U}) {
        auto bytes = baseline;
        bytes.insert(bytes.begin() + at + 16, 16, std::byte{});
        mf::put32(bytes, at + 8, 32);
        mf::put32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
        reject(bytes, Material2DMErrorCode::unsupported_layout,
               "nonempty MTPA or UVAA needs an explicit format extension");
    }
    auto missing_root = baseline;
    missing_root.resize(mf::Real);
    mf::put32(missing_root, 8, static_cast<std::uint32_t>(missing_root.size()));
    reject(missing_root, Material2DMErrorCode::unsupported_layout,
           "missing REAL root cannot be ignored");
    auto unsupported_mat = baseline;
    // Keep the container walk valid: split the first MAT into two leaves.
    mf::chunk(unsupported_mat, 0x790, "MAT.", 0xe0);
    mf::chunk(unsupported_mat, 0x870, "XXXX", 0x10);
    check(!Material2DM::parse(unsupported_mat),
          "a structurally split MAT is refused rather than partially linked");
}

void relocations_and_types() {
    check(bool(Material2DM::parse(mf::fixture())),
          "unsorted REAL with four terminal zero words is valid");
    for (const auto field : mf::Relocations) {
        auto bytes = mf::fixture();
        std::vector<std::uint32_t> remainder;
        for (const auto candidate : mf::Relocations) {
            if (candidate != field) {
                remainder.push_back(candidate);
            }
        }
        relocation_list(bytes, remainder);
        const auto result = Material2DM::parse(bytes);
        check(!result &&
                  result.error().code ==
                      Material2DMErrorCode::invalid_reference &&
                  result.error().offset == field,
              "every nonzero typed field must itself appear in REAL");
    }
    for (const auto value : {0x11U, 0x04U, 0xbc0U, UINT32_MAX}) {
        auto bytes = mf::fixture();
        mf::put32(bytes, mf::Real + 16, value);
        reject(bytes, Material2DMErrorCode::invalid_reference,
               "REAL positions must be aligned, in range and beyond header");
    }
    auto duplicate = mf::fixture();
    mf::put32(duplicate, mf::Real + 16, mf::Relocations[1]);
    reject(duplicate, Material2DMErrorCode::invalid_reference,
           "duplicate REAL field rejected");
    auto nonterminal = mf::fixture();
    mf::put32(nonterminal, mf::Real + 16, 0);
    reject(nonterminal, Material2DMErrorCode::invalid_reference,
           "zero REAL padding cannot precede another nonzero field");
    auto foreign = mf::fixture();
    mf::put32(foreign, mf::Real + 16 + 12 * 4, 0x40);
    reject(foreign, Material2DMErrorCode::unsupported_layout,
           "foreign REAL field is unsupported even inside a valid LAY payload");
    auto targets_instead_of_fields = mf::fixture();
    mf::put32(targets_instead_of_fields, mf::Real + 16 + 12 * 4, 0x790);
    reject(targets_instead_of_fields, Material2DMErrorCode::unsupported_layout,
           "REAL records positions of fields, not declared MAT targets");

    for (const auto target : {0U, 0x20U, 0x4b0U, 0x730U, 0x740U, 0x750U, 0x7a0U,
                              0x791U, UINT32_MAX}) {
        auto bytes = mf::fixture();
        mf::put32(bytes, 0x758, target);
        reject(bytes, Material2DMErrorCode::invalid_hash,
               "HASH target must be an exact declared MAT chunk start");
    }
    auto duplicate_target = mf::fixture();
    mf::put32(duplicate_target, 0x768, 0x970);
    reject(duplicate_target, Material2DMErrorCode::invalid_hash,
           "two HASH rows cannot silently cover the same MAT record");
    for (const auto target :
         {0x20U, 0x4c0U, 0x4b1U, 0x730U, 0x790U, UINT32_MAX}) {
        auto bytes = mf::fixture();
        mf::put32(bytes, 0xa50, target);
        reject(bytes, Material2DMErrorCode::invalid_reference,
               "MAT link requires an exact PARA child, not any readable bytes");
    }
    for (const auto target : {0x30U, 0x21U, 0x4b0U, 0x790U, UINT32_MAX}) {
        auto bytes = mf::fixture();
        mf::put32(bytes, 0x504, target);
        reject(bytes, Material2DMErrorCode::invalid_reference,
               "PAR link requires an exact LAYA child of the right type");
    }
}

void explicit_alias_and_null_policy() {
    auto alias_parameters = mf::fixture();
    mf::put32(alias_parameters, 0xa50, 0x4b0);
    const auto shared_parameters = Material2DM::parse(alias_parameters);
    check(shared_parameters && shared_parameters->parameters().size() == 4 &&
              shared_parameters->materials()[0].parameters_index == 0U &&
              shared_parameters->materials()[3].parameters_index == 0U,
          "port permits MAT aliases to the same declared PAR without copying");
    auto alias_layers = mf::fixture();
    mf::put32(alias_layers, 0x504, 0x20);
    const auto shared_layers = Material2DM::parse(alias_layers);
    check(shared_layers && shared_layers->layers().size() == 4 &&
              shared_layers->parameters()[0].layer_index == 0U &&
              shared_layers->parameters()[1].layer_index == 0U,
          "port permits PAR aliases to the same declared LAY");
    for (const auto field : {0xa50U, 0x504U}) {
        auto bytes = mf::fixture();
        mf::put32(bytes, field, 0);
        const auto listed = Material2DM::parse(bytes);
        check(listed &&
                  (field == 0xa50 ? !listed->materials()[0].parameters_index
                                  : !listed->parameters()[0].layer_index),
              "null typed links stay absent rather than selecting record zero");
        std::vector<std::uint32_t> fields;
        for (const auto candidate : mf::Relocations) {
            if (candidate != field) {
                fields.push_back(candidate);
            }
        }
        relocation_list(bytes, fields);
        const auto unlisted = Material2DM::parse(bytes);
        check(
            unlisted && unlisted->materials().size() == 4 &&
                unlisted->parameters().size() == 4 &&
                unlisted->layers().size() == 4,
            "port allows absent links with or without their known REAL field");
    }
    // These null/alias cases are C++ policy tests, not new native-format facts.
}

void budgets() {
    const auto bytes = mf::fixture();
    Material2DMLimits exact;
    exact.max_file_bytes = bytes.size();
    exact.max_materials = exact.max_parameters = exact.max_layers = 4;
    exact.max_relocations = 16; // includes all four zero padding words
    check(bool(Material2DM::parse(bytes, exact)),
          "inclusive file, record and REAL word budgets accept the boundary");
    for (unsigned which = 0; which < 6; ++which) {
        auto limit = exact;
        switch (which) {
        case 0:
            --limit.max_file_bytes;
            break;
        case 1:
            --limit.max_materials;
            break;
        case 2:
            --limit.max_parameters;
            break;
        case 3:
            --limit.max_layers;
            break;
        case 4:
            --limit.max_relocations;
            break;
        default:
            limit.max_output_bytes = 1;
            break;
        }
        const auto result = Material2DM::parse(bytes, limit);
        check(!result &&
                  result.error().code == Material2DMErrorCode::resource_limit,
              "each independent resource budget refuses one excess");
    }
    Material2DMLimits maximum;
    maximum.max_file_bytes = maximum.max_materials = maximum.max_parameters =
        maximum.max_layers = maximum.max_relocations =
            maximum.max_output_bytes = std::numeric_limits<std::size_t>::max();
    check(bool(Material2DM::parse(bytes, maximum)),
          "SIZE_MAX material budget does not wrap its extra HASH child");
    Material2DMLimits zero;
    zero.max_materials = zero.max_parameters = zero.max_layers =
        zero.max_relocations = zero.max_output_bytes = 0;
    check(bool(Material2DM::parse(mf::empty_fixture(), zero)),
          "zero logical output fits an empty library exactly");
    const auto fail_zero = Material2DM::parse(bytes, zero);
    check(!fail_zero &&
              fail_zero.error().code == Material2DMErrorCode::resource_limit,
          "zero counts do not permit nonempty allocation");
}

void bindings() {
    const auto library = Material2DM::parse(mf::fixture());
    if (!library) {
        check(false, "binding library fixture parses");
        return;
    }
    for (std::size_t i = 0; i < mf::Keys.size(); ++i) {
        for (const auto cache : {0U, 1U, 0x790U, UINT32_MAX}) {
            auto bytes = tdur::fixtures::scene_mesh_scene();
            mf::put_key(bytes, 48, mf::Keys[i]);
            mf::put32(bytes, 56, cache);
            mf::put32(bytes, 60, ~cache);
            const auto scene = Scene3DD::parse(bytes);
            check(bool(scene),
                  "independent scene with arbitrary MBNK cache parses");
            if (!scene) {
                continue;
            }
            for (const auto low_word : {0U, 1U, 0x8000U, 0xffffU}) {
                const auto bound =
                    library->bind_primitive(*scene, 0, low_word, 3);
                check(bound && bound->material_index == i &&
                          bound->source_slot == 0 &&
                          bound->fallback_reason ==
                              MaterialFallbackReason::none,
                      "high16 slot zero uses raw MBNK key, ignores low16 and "
                      "cache");
            }
        }
    }
    auto bytes = tdur::fixtures::scene_mesh_scene();
    mf::put_key(bytes, 48, mf::Keys[0]);
    const auto scene = Scene3DD::parse(bytes);
    if (!scene) {
        check(false, "boundary binding scene parses");
        return;
    }
    for (const auto word : {0x00010000U, 0x80000000U, 0xfffe0000U}) {
        binding_error(
            library->bind_primitive(*scene, 0, word, 2),
            Material2DMErrorCode::invalid_material_slot,
            "out-of-range high16 slot is refused even with a fallback");
    }
    for (const auto word : {0xffff0000U, UINT32_MAX}) {
        binding_error(library->bind_primitive(*scene, 0, word),
                      Material2DMErrorCode::fallback_required,
                      "0xffff is a sentinel, never bank index 65535");
        const auto result = library->bind_primitive(*scene, 0, word, 2);
        check(result && result->material_index == 2 &&
                  result->source_slot == UINT16_MAX &&
                  result->fallback_reason == MaterialFallbackReason::sentinel,
              "sentinel uses only the caller's explicit known fallback");
    }
    binding_error(library->bind_primitive(*scene, std::nullopt, 0),
                  Material2DMErrorCode::fallback_required,
                  "absent bank cannot invent a native default identity");
    const auto absent =
        library->bind_primitive(*scene, std::nullopt, UINT32_MAX, 1);
    check(absent && absent->material_index == 1 && !absent->source_slot &&
              absent->fallback_reason == MaterialFallbackReason::absent_bank,
          "absent bank has an explicit distinct fallback reason and no slot");
    for (const auto bank : {std::size_t{1}, SIZE_MAX}) {
        binding_error(
            library->bind_primitive(*scene, bank, 0, 2),
            Material2DMErrorCode::invalid_material_bank,
            "invalid declared-bank index is not an absent-bank fallback");
    }
    for (const auto fallback : {std::size_t{4}, SIZE_MAX}) {
        binding_error(
            library->bind_primitive(*scene, 0, 0, fallback),
            Material2DMErrorCode::invalid_fallback,
            "unknown fallback rejected even when normal key would bind");
    }
    auto unknown_bytes = bytes;
    const Material2DMKey unknown{
        std::byte{0xee}, std::byte{0xee}, std::byte{0xee}, std::byte{0xee},
        std::byte{0xee}, std::byte{0xee}, std::byte{0xee}, std::byte{0xee}};
    mf::put_key(unknown_bytes, 48, unknown);
    mf::put32(unknown_bytes, 56,
              0x970); // a valid MAT offset is not a resolved cache
    const auto unknown_scene = Scene3DD::parse(unknown_bytes);
    check(bool(unknown_scene),
          "unknown raw material key is legal scene metadata");
    if (unknown_scene) {
        binding_error(
            library->bind_primitive(*unknown_scene, 0, 0),
            Material2DMErrorCode::fallback_required,
            "unresolved key cannot be rescued by its on-disk cache word");
        const auto result = library->bind_primitive(*unknown_scene, 0, 0, 3);
        check(result && result->material_index == 3 &&
                  result->source_slot == 0 &&
                  result->fallback_reason ==
                      MaterialFallbackReason::unresolved_key,
              "unresolved key requires explicit fallback, not material zero");
    }
    const auto empty = Material2DM::parse(mf::empty_fixture());
    if (empty) {
        binding_error(empty->bind_primitive(*scene, std::nullopt, 0, 0),
                      Material2DMErrorCode::invalid_fallback,
                      "empty library has no valid fallback index");
    }
}

struct Corpus {
    std::size_t material_member;
    std::size_t material_bytes;
    std::size_t material_count;
    std::size_t nodes;
    std::size_t empty_nodes;
    std::size_t object_references;
    std::size_t primitive_bindings;
};

void physical_bank(const std::filesystem::path& path, const Corpus& expected) {
    const auto archive = BankArchive::read_file(path);
    check(bool(archive), "physical bank validates read-only");
    if (!archive) {
        std::cerr << path << ": " << archive.error().detail << '\n';
        return;
    }
    const auto material_bytes = archive->stored_bytes(expected.material_member);
    const auto scene_bytes = archive->stored_bytes(0);
    const auto geometry_bytes = archive->stored_bytes(1);
    check(material_bytes && scene_bytes && geometry_bytes,
          "explicit physical 2DM, scene0 and geometry1 members exist");
    if (!material_bytes || !scene_bytes || !geometry_bytes) {
        return;
    }
    check(material_bytes->size() == expected.material_bytes,
          "requested physical 2DM member has the documented size");
    const auto library = Material2DM::parse(*material_bytes);
    const auto scene = Scene3DD::parse(*scene_bytes);
    check(library && scene, "physical 2DM and paired scene decode");
    if (!library || !scene) {
        if (!library) {
            std::cerr << library.error().detail << '\n';
        }
        if (!scene) {
            std::cerr << scene.error().detail << '\n';
        }
        return;
    }
    check(library->materials().size() == expected.material_count &&
              library->parameters().size() == expected.material_count &&
              library->layers().size() == expected.material_count,
          "physical typed record counts match the independently inventoried "
          "corpus");
    bool physical_inversion{};
    for (std::size_t i = 0; i < library->materials().size(); ++i) {
        const auto& material = library->materials()[i];
        check(library->find(material.key) == i && material.parameters_index &&
                  *material.parameters_index < library->parameters().size(),
              "each physical raw key resolves its HASH ordinal and typed PAR");
        if (i && material.source_offset <
                     library->materials()[i - 1].source_offset) {
            physical_inversion = true;
        }
        if (material.parameters_index) {
            const auto& parameters =
                library->parameters()[*material.parameters_index];
            check(parameters.layer_index &&
                      *parameters.layer_index < library->layers().size(),
                  "each physical PAR resolves its declared LAY");
        }
    }
    check(
        physical_inversion,
        "physical corpus HASH order demonstrably differs from MAT chunk order");
    std::size_t object_references{};
    std::size_t empty_nodes{};
    for (const auto& node : scene->nodes()) {
        object_references += node.object_entries.size();
        if (node.object_entries.empty()) {
            ++empty_nodes;
            check(!node.material_bank_index,
                  "documented empty physical node has no material bank");
        }
    }
    check(scene->nodes().size() == expected.nodes &&
              empty_nodes == expected.empty_nodes &&
              object_references == expected.object_references,
          "all node variants counted, not only object_entries.front");
    const auto geometry = scene->resolve_geometry(*geometry_bytes);
    check(geometry && geometry->size() == object_references,
          "geometry resolution includes every instance object entry");
    if (!geometry) {
        std::cerr << geometry.error().detail << '\n';
        return;
    }
    std::size_t primitive_count{};
    std::size_t nonzero_slots{};
    std::size_t secondary_variants{};
    std::uint32_t maximum_slot{};
    for (const auto& binding : *geometry) {
        if (binding.node_index >= scene->nodes().size()) {
            check(false, "paired binding points to an existing scene node");
            continue;
        }
        const auto& node = scene->nodes()[binding.node_index];
        check(
            binding.object_entry_index < node.object_entries.size() &&
                node.object_entries[binding.object_entry_index].object_index ==
                    binding.object_index,
            "paired binding preserves exact object-entry identity");
        secondary_variants += binding.object_entry_index != 0;
        std::size_t primitive_ordinal{};
        for (const auto word : binding.opaque_primitive_bindings) {
            const auto ordinal = primitive_ordinal++;
            ++primitive_count;
            const auto slot = word >> 16;
            maximum_slot = std::max(maximum_slot, slot);
            nonzero_slots += slot != 0;
            const auto result =
                library->bind_primitive(*scene, node.material_bank_index, word);
            check(
                bool(result),
                "every physical primitive binds without an invented fallback");
            if (!node.material_bank_index ||
                *node.material_bank_index >= scene->material_banks().size()) {
                check(false,
                      "documented physical binding has a valid selected MBNK");
                continue;
            }
            const auto& bank =
                scene->material_banks()[*node.material_bank_index];
            if (slot >= bank.keys.size()) {
                check(false,
                      "physical high16 slot is within its own instance bank");
                continue;
            }
            const auto oracle = linear_find(*library, bank.keys[slot].key);
            check(oracle && result && result->material_index == *oracle &&
                      result->source_slot == slot &&
                      result->fallback_reason == MaterialFallbackReason::none,
                  "physical binding equals independent raw8 equality oracle");
            if (list_bindings && result && oracle &&
                result->material_index == *oracle &&
                result->source_slot == slot &&
                result->fallback_reason == MaterialFallbackReason::none) {
                std::cout << "BIND " << path.filename().string()
                          << " node=" << binding.node_index
                          << " object_entry=" << binding.object_entry_index
                          << " object=" << binding.object_index
                          << " primitive=" << ordinal
                          << " bank=" << *node.material_bank_index
                          << " slot=" << slot << " key=";
                for (const auto byte : bank.keys[slot].key) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                              << std::to_integer<unsigned>(byte);
                }
                std::cout << std::dec << std::setfill(' ')
                          << " material=" << *oracle << " mat_offset="
                          << library->materials()[*oracle].source_offset
                          << " fallback=none\n";
            }
        }
    }
    check(primitive_count == expected.primitive_bindings &&
              secondary_variants == 2 && nonzero_slots != 0,
          "complete corpus includes secondary OBJ variants and nonzero local "
          "slots");
    std::cout << "BANK " << path.filename().string()
              << " member=" << expected.material_member
              << " materials=" << library->materials().size()
              << " references=" << object_references
              << " bindings=" << primitive_count << " max_slot=" << maximum_slot
              << " fallback=none\n";
}
} // namespace

int main(int argc, char** argv) {
    const bool listed =
        argc == 5 && std::string_view(argv[1]) == "--list-bindings";
    if (argc != 1 && argc != 4 && !listed) {
        std::cerr << "usage: material_2dm_test [--list-bindings] "
                     "[Alfa_GT.bnk 300_C.bnk A3_V6.bnk]\n";
        return 2;
    }
    list_bindings = listed;
    try {
        layout_and_ownership();
        keys();
        malformed_headers();
        relocations_and_types();
        explicit_alias_and_null_policy();
        budgets();
        bindings();
        if (argc == 4 || listed) {
            const auto first = listed ? 2 : 1;
            physical_bank(argv[first], {19, 0x7860, 34, 97, 41, 58, 95});
            physical_bank(argv[first + 1], {25, 0x83a0, 37, 101, 42, 61, 95});
            physical_bank(argv[first + 2], {24, 0x8320, 37, 103, 41, 64, 99});
        }
    } catch (const std::exception& error) {
        check(false, "unexpected exception escaped the fixture suite");
        std::cerr << error.what() << '\n';
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

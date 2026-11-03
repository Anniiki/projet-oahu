#include "fixtures/material_textures_fixture.hpp"
#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/material_textures.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace tdur::assets;
namespace mf = tdur::fixtures::material;
namespace tf = tdur::fixtures::material_textures;

namespace {
unsigned checks{};
unsigned failures{};

void check(bool value, std::string_view description) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << description << '\n';
    }
}

using BindingsResult =
    std::expected<MaterialTextureBindings, MaterialTextureError>;

BindingsResult decode(const mf::Bytes& bytes,
                      MaterialTextureLimits limits = {}) {
    const auto materials = Material2DM::parse(bytes);
    if (!materials) {
        throw std::runtime_error("invalid test 2DM: " +
                                 materials.error().detail);
    }
    // The result must outlive this Material2DM and the caller's input bytes.
    return MaterialTextureBindings::from_materials(*materials, limits);
}

bool same_sampler(const MaterialTextureSampler& a,
                  const MaterialTextureSampler& b) {
    return a.min_mag == b.min_mag && a.mip == b.mip &&
           a.address_u == b.address_u && a.address_v == b.address_v;
}

bool same_plan(const MaterialTexturePlan& a, const MaterialTexturePlan& b) {
    if (a.stage_count != b.stage_count || a.suppressed != b.suppressed) {
        return false;
    }
    for (std::size_t i = 0; i < a.stage_count; ++i) {
        if (a.stages[i].action != b.stages[i].action ||
            a.stages[i].texture_token != b.stages[i].texture_token ||
            !same_sampler(a.stages[i].sampler, b.stages[i].sampler)) {
            return false;
        }
    }
    return true;
}

MaterialTexturePlan get_plan(const MaterialTextureBindings& bindings,
                             std::size_t material, std::uint32_t flags = 0) {
    const auto plan = bindings.plan(material, flags);
    check(bool(plan), "known material yields a plan");
    if (!plan) {
        throw std::runtime_error(plan.error().detail);
    }
    return *plan;
}

MaterialTextureSampler
expected_sampler(const std::array<std::uint8_t, 4>& indices) {
    constexpr std::array min_mag{MaterialMinMagFilter::linear,
                                 MaterialMinMagFilter::point,
                                 MaterialMinMagFilter::linear};
    constexpr std::array mip{MaterialMipFilter::linear, MaterialMipFilter::none,
                             MaterialMipFilter::linear};
    constexpr std::array address{
        MaterialAddressMode::wrap, MaterialAddressMode::wrap,
        MaterialAddressMode::clamp, MaterialAddressMode::mirror,
        MaterialAddressMode::border};
    return {min_mag.at(indices[0]), mip.at(indices[1]), address.at(indices[2]),
            address.at(indices[3])};
}

void reject(const mf::Bytes& bytes, MaterialTextureErrorCode wanted,
            std::string_view description) {
    const auto bindings = decode(bytes);
    check(!bindings, description);
    if (!bindings) {
        check(bindings.error().code == wanted, "specific typed texture error");
        check(!bindings.error().detail.empty(),
              "texture error has a diagnosis");
    }
}

void layout_and_ownership() {
    auto bytes = tf::fixture();
    const auto original = bytes;
    const auto decoded = decode(bytes);
    check(bool(decoded), "independent type-2 collection parses");
    if (!decoded) {
        std::cerr << decoded.error().detail << '\n';
        return;
    }
    check(
        decoded->layouts().size() == 4 && decoded->layers().size() == 4,
        "one layout per source HASH material and one record per physical LAY");
    if (decoded->layouts().size() != 4 || decoded->layers().size() != 4) {
        return;
    }
    constexpr std::array<std::size_t, 4> parameters{3, 2, 1, 0};
    std::fill(bytes.begin(), bytes.end(), std::byte{0xee});
    bytes.clear();
    bytes.shrink_to_fit();
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& layout = decoded->layouts()[i];
        check(layout.material_index == i &&
                  layout.parameter_index == parameters[i] &&
                  layout.layer_index == tf::MaterialLayers[i],
              "MAT/PAR/LAY indices retain nonphysical HASH order");
        const auto& layer = decoded->layers()[i];
        check(layer.source_offset == mf::Layers[i] && layer.slot_count == 8,
              "LAY count is one byte despite CD in all higher bytes");
        for (std::size_t slot = 0; slot < 8; ++slot) {
            const auto& record = layer.slots[slot];
            check(record.source_offset == tf::slot_offset(i, slot) &&
                      record.key == tf::SlotKeys[slot] &&
                      record.sampler_indices == tf::SamplerIndices[slot],
                  "32-byte slot stride, raw8 key+8 and sampler+16h are exact");
            check(std::ranges::equal(record.source_bytes,
                                     std::span(original).subspan(
                                         tf::slot_offset(i, slot), 32)),
                  "all raw slot bytes owned, including ignored pointer words");
            check(
                same_sampler(record.sampler,
                             expected_sampler(tf::SamplerIndices[slot])),
                "sampler conversion matches independently transcribed tables");
        }
        const auto plan = get_plan(*decoded, i);
        check(!plan.suppressed && plan.stage_count == 8,
              "all eight active ordinals request stages");
        for (std::size_t slot = 0; slot < 8; ++slot) {
            const bool zero = slot == 4 || slot == 7;
            check(plan.stages[slot].action ==
                          (zero ? MaterialTextureAction::keep_previous
                                : MaterialTextureAction::require_fallback) &&
                      !plan.stages[slot].texture_token &&
                      same_sampler(plan.stages[slot].sampler,
                                   expected_sampler(tf::SamplerIndices[slot])),
                  "fresh typed cache ignores disk pointers and still requests "
                  "samplers");
        }
    }
    for (const auto index : {std::size_t{4}, SIZE_MAX}) {
        const auto plan = decoded->plan(index);
        check(!plan && plan.error().code ==
                           MaterialTextureErrorCode::invalid_material,
              "unknown material index is refused explicitly");
    }
}

void type_and_count_guards() {
    for (const auto count : {0U, 2U, 0x100U, 0x101U, 0xffffU}) {
        auto bytes = tf::fixture();
        mf::put32(bytes, 0x980, (count << 16) | 0xbeefU);
        reject(bytes, MaterialTextureErrorCode::unsupported_parameter_count,
               "full MAT WORD+12 must be exactly one");
    }
    for (const auto type : {0U, 1U, 3U, 0x01000002U, UINT32_MAX}) {
        auto bytes = tf::fixture();
        mf::put32(bytes, 0x640, type);
        reject(
            bytes, MaterialTextureErrorCode::unsupported_parameter_type,
            "full PAR DWORD+10 must be type two; type one is a separate path");
    }
    auto no_parameters = tf::fixture();
    mf::put32(no_parameters, 0xa50, 0);
    reject(no_parameters, MaterialTextureErrorCode::missing_parameters,
           "explicit null MAT link is not an implicit first parameter");
    auto no_layer = tf::fixture();
    mf::put32(no_layer, 0x684, 0);
    reject(no_layer, MaterialTextureErrorCode::missing_layer,
           "explicit null PAR link is not an implicit first layer");
    for (unsigned count = 0; count <= 8; ++count) {
        auto bytes = tf::fixture();
        tf::set_count(bytes, 2, static_cast<std::uint8_t>(count));
        const auto bindings = decode(bytes);
        check(bindings && bindings->layers()[2].slot_count == count,
              "all physical BYTE counts from zero to eight fit the declared "
              "layer");
        if (bindings) {
            const auto plan = get_plan(*bindings, 0);
            check(plan.stage_count == count,
                  "active plan length follows the BYTE rather than its "
                  "poisoned upper bytes");
        }
    }
    for (const auto count : {9U, 255U}) {
        auto bytes = tf::fixture();
        tf::set_count(bytes, 2, static_cast<std::uint8_t>(count));
        reject(bytes, MaterialTextureErrorCode::invalid_slot_count,
               "BYTE count exceeding the eight physical slots is refused");
    }
    auto inactive = tf::fixture();
    tf::set_count(inactive, 2, 1);
    tf::set_sampler(inactive, 2, 1, {255, 255, 255, 255});
    check(bool(decode(inactive)),
          "inactive opaque slot bytes are not interpreted as sampler indices");
    tf::set_count(inactive, 2, 0);
    tf::set_sampler(inactive, 2, 0, {255, 255, 255, 255});
    check(bool(decode(inactive)),
          "empty layer never reads an inactive sampler");
    // A strict whole-collection refusal includes an unsupported late material.
    auto late_type = tf::fixture();
    mf::put32(late_type, mf::Parameters[0] + 0x10, 1);
    reject(late_type, MaterialTextureErrorCode::unsupported_parameter_type,
           "unsupported final HASH material cannot yield a partial collection");
}

void samplers() {
    for (std::size_t field = 0; field < 4; ++field) {
        for (unsigned value = 0; value < 256; ++value) {
            auto bytes = tf::fixture();
            tf::set_count(bytes, 2, 1);
            auto indices = tf::SamplerIndices[0];
            indices[field] = static_cast<std::uint8_t>(value);
            tf::set_sampler(bytes, 2, 0, indices);
            const bool valid = value < (field < 2 ? 3U : 5U);
            const auto bindings = decode(bytes);
            if (valid) {
                check(bindings &&
                          same_sampler(bindings->layers()[2].slots[0].sampler,
                                       expected_sampler(indices)),
                      "every established sampler encoding maps to its exact "
                      "enum");
            } else {
                check(!bindings &&
                          bindings.error().code ==
                              MaterialTextureErrorCode::unsupported_sampler,
                      "all unknown sampler bytes are refused, including MIP "
                      "index3 ANISO");
            }
        }
    }
    for (std::uint8_t min = 0; min < 3; ++min) {
        for (std::uint8_t mip = 0; mip < 3; ++mip) {
            for (std::uint8_t u = 0; u < 5; ++u) {
                for (std::uint8_t v = 0; v < 5; ++v) {
                    const std::array<std::uint8_t, 4> indices{min, mip, u, v};
                    auto bytes = tf::fixture();
                    tf::set_count(bytes, 2, 1);
                    tf::set_sampler(bytes, 2, 0, indices);
                    const auto bindings = decode(bytes);
                    check(
                        bindings &&
                            same_sampler(bindings->layers()[2].slots[0].sampler,
                                         expected_sampler(indices)),
                        "all 225 supported sampler combinations remain "
                        "independent");
                }
            }
        }
    }
}

void rebind_and_copies() {
    auto bindings = decode(tf::fixture());
    check(bool(bindings), "rebinding fixture decodes");
    if (!bindings) {
        return;
    }
    constexpr std::uint64_t Large = 0xfedcba9876543210ULL;
    std::vector<MaterialTextureResource> resources{{tf::SlotKeys[0], 11},
                                                   {tf::SlotKeys[1], 44},
                                                   {tf::SlotKeys[0], 0},
                                                   {tf::Zero, UINT64_MAX},
                                                   {tf::SlotKeys[3], Large}};
    const auto stores = bindings->rebind(resources);
    // Per unique LAY: key0 twice*two occurrences + key1 + zero twice + key3 =8.
    check(stores && *stores == 32, "ordered duplicate resources count every "
                                   "matching store across four LAYs");
    resources.clear();
    resources.shrink_to_fit();
    const auto plan = get_plan(*bindings, 0);
    const std::array<std::optional<std::uint64_t>, 8> tokens{
        0, 44, std::nullopt, Large, UINT64_MAX, 0, std::nullopt, UINT64_MAX};
    for (std::size_t slot = 0; slot < 8; ++slot) {
        check(plan.stages[slot].texture_token == tokens[slot] &&
                  plan.stages[slot].action ==
                      (tokens[slot].has_value()
                           ? MaterialTextureAction::bind_cached
                           : MaterialTextureAction::require_fallback),
              "last matching token wins, full64 and token0 remain valid");
        check(
            same_sampler(plan.stages[slot].sampler,
                         expected_sampler(tf::SamplerIndices[slot])),
            "cached and missing textures retain every active sampler request");
    }
    const auto empty = bindings->rebind({});
    check(empty && *empty == 0 && same_plan(plan, get_plan(*bindings, 0)),
          "empty resource collection preserves all previous caches");
    auto unknown_key = tf::SlotKeys[6];
    unknown_key[7] = std::byte{1};
    const std::array unrelated{MaterialTextureResource{unknown_key, 123}};
    const auto absent = bindings->rebind(unrelated);
    check(absent && *absent == 0 && same_plan(plan, get_plan(*bindings, 0)),
          "missing post-NUL-distinct key leaves populated caches unchanged");

    auto copy = *bindings;
    const std::array replacement{MaterialTextureResource{tf::SlotKeys[0], 99}};
    const auto changed = copy.rebind(replacement);
    const auto copied_plan = get_plan(copy, 0);
    check(changed && *changed == 8 &&
              copied_plan.stages[0].texture_token == std::uint64_t{99} &&
              copied_plan.stages[5].texture_token == std::uint64_t{99} &&
              same_plan(plan, get_plan(*bindings, 0)),
          "copying a collection snapshots all caches without sharing their "
          "mutation");
    const std::array zero_resource{MaterialTextureResource{tf::Zero, 0}};
    const auto zeros = copy.rebind(zero_resource);
    const auto zero_plan = get_plan(copy, 0);
    check(
        zeros && *zeros == 8 &&
            zero_plan.stages[4].action == MaterialTextureAction::bind_cached &&
            zero_plan.stages[7].action == MaterialTextureAction::bind_cached &&
            zero_plan.stages[4].texture_token == std::uint64_t{0} &&
            zero_plan.stages[7].texture_token == std::uint64_t{0},
        "an explicit zero-key resource with token0 overrides keep_previous");
    // Fresh owner, resource identity differing after NUL or by a sign bit.
    for (std::size_t key = 0; key < 4; ++key) {
        auto fresh = decode(tf::fixture());
        if (!fresh) {
            check(false, "raw8 rebind fixture decodes");
            continue;
        }
        const std::array loaded{
            MaterialTextureResource{tf::SlotKeys[key], Large}};
        const auto count = fresh->rebind(loaded);
        check(count && *count == (key == 0 ? 8U : 4U),
              "exact two-DWORD raw key identity includes sign and post-NUL "
              "bytes");
        const auto actual = get_plan(*fresh, 0);
        for (std::size_t slot = 0; slot < 8; ++slot) {
            const bool match = tf::SlotKeys[slot] == tf::SlotKeys[key];
            check(actual.stages[slot].texture_token.has_value() == match &&
                      (!match || actual.stages[slot].texture_token == Large),
                  "all and only complete raw8 matches receive the token");
        }
    }
}

void alias_and_orphan_layers() {
    auto bytes = tf::fixture();
    // Material0's PAR3 now points to LAY3, also used by material3's PAR0.
    // LAY2 is still declared but orphaned. LAY3 has one active key0; the
    // other three declared LAYs each have two key0 occurrences.
    mf::put32(bytes, 0x684, 0x380);
    tf::set_count(bytes, 3, 1);
    auto bindings = decode(bytes);
    check(bindings && bindings->layouts()[0].layer_index == 3 &&
              bindings->layouts()[3].layer_index == 3 &&
              bindings->layers().size() == 4,
          "MAT aliases reference one canonical physical LAY, retaining the "
          "orphan");
    if (!bindings) {
        return;
    }
    const std::array loaded{
        MaterialTextureResource{tf::SlotKeys[0], 0x100000001ULL}};
    const auto stores = bindings->rebind(loaded);
    check(stores && *stores == 7,
          "rebinding visits each declared LAY once, including orphan, without "
          "MAT multiplication");
    const auto a = get_plan(*bindings, 0);
    const auto b = get_plan(*bindings, 3);
    check(a.stage_count == 1 && same_plan(a, b) &&
              a.stages[0].texture_token == 0x100000001ULL,
          "both MAT aliases observe the same canonical LAY cache");
    const std::array update{MaterialTextureResource{tf::SlotKeys[0], 0}};
    const auto changed = bindings->rebind(update);
    check(changed && *changed == 7 &&
              get_plan(*bindings, 0).stages[0].texture_token ==
                  std::uint64_t{0} &&
              get_plan(*bindings, 3).stages[0].texture_token ==
                  std::uint64_t{0},
          "later rebinding propagates through all material aliases");
    tf::set_sampler(bytes, 2, 0, {255, 0, 0, 0});
    reject(bytes, MaterialTextureErrorCode::unsupported_sampler,
           "declared orphan LAY remains in the strict supported collection "
           "grammar");
}

void flags() {
    auto bindings = decode(tf::fixture());
    if (!bindings) {
        check(false, "flags fixture decodes");
        return;
    }
    const std::array loaded{MaterialTextureResource{tf::SlotKeys[0], 0}};
    check(bool(bindings->rebind(loaded)),
          "flag fixture has a valid cached token0");
    const auto baseline = get_plan(*bindings, 0);
    const auto verify = [&](std::uint32_t value) {
        const auto plan = get_plan(*bindings, 0, value);
        const bool suppressed = (value & 0x42U) != 0;
        check(plan.suppressed == suppressed &&
                  (suppressed ? plan.stage_count == 0
                              : same_plan(plan, baseline)),
              "only global mask42 suppresses both texture and sampler stages");
    };
    for (std::uint32_t low = 0; low < 256; ++low) {
        verify(low);
        verify(0xffffff00U | low);
    }
    for (unsigned bit = 0; bit < 32; ++bit) {
        const std::uint32_t value = std::uint32_t{1} << bit;
        verify(value);
        verify(~value);
    }
    check(same_plan(baseline, get_plan(*bindings, 0)),
          "suppressed plans never clear or replace persistent caches");
}

void resource_budgets() {
    const auto bytes = tf::fixture();
    constexpr std::size_t Output =
        4 * sizeof(MaterialTextureLayout) + 4 * sizeof(MaterialTextureLayer) +
        4 * sizeof(std::array<std::optional<std::uint64_t>, 8>);
    MaterialTextureLimits exact;
    exact.max_materials = exact.max_layers = 4;
    exact.max_output_bytes = Output;
    check(bool(decode(bytes, exact)),
          "inclusive owned-layout/layer/cache budget fits exactly");
    for (unsigned field = 0; field < 3; ++field) {
        auto limit = exact;
        if (field == 0) {
            --limit.max_materials;
        } else if (field == 1) {
            --limit.max_layers;
        } else {
            --limit.max_output_bytes;
        }
        const auto refused = decode(bytes, limit);
        check(!refused && refused.error().code ==
                              MaterialTextureErrorCode::resource_limit,
              "each construction budget refuses an excess before returning a "
              "collection");
    }
    const std::array initial{MaterialTextureResource{tf::SlotKeys[0], 7}};
    const std::array replacements{MaterialTextureResource{tf::SlotKeys[0], 99},
                                  MaterialTextureResource{tf::SlotKeys[1], 100},
                                  MaterialTextureResource{tf::Zero, 101}};
    for (unsigned guard = 0; guard < 2; ++guard) {
        MaterialTextureLimits limit;
        if (guard == 0) {
            limit.max_loaded_resources = 2;
        } else {
            limit.max_rebind_comparisons = 63; // two inputs require 2*32
        }
        auto bindings = decode(bytes, limit);
        if (!bindings) {
            check(false, "budget fixture construction succeeds");
            continue;
        }
        const auto seeded = bindings->rebind(initial);
        check(seeded && *seeded == 8,
              "budget fixture starts with populated caches");
        const auto before = get_plan(*bindings, 0);
        const auto loaded = guard == 0 ? std::span(replacements)
                                       : std::span(replacements).first(2);
        const auto refused = bindings->rebind(loaded);
        check(!refused && refused.error().code ==
                              MaterialTextureErrorCode::resource_limit,
              "resource count/comparison budget refuses the entire oversized "
              "pass");
        for (std::size_t material = 0; material < 4; ++material) {
            check(same_plan(before, get_plan(*bindings, material)),
                  "budget refusal precedes every matching cache write");
        }
    }
    MaterialTextureLimits boundary;
    boundary.max_loaded_resources = 2;
    boundary.max_rebind_comparisons = 64;
    auto bindings = decode(bytes, boundary);
    if (bindings) {
        const auto accepted =
            bindings->rebind(std::span(replacements).first(2));
        check(accepted && *accepted == 12,
              "resource and comparison budgets accept their exact boundaries");
    } else {
        check(false, "boundary fixture decodes");
    }
    MaterialTextureLimits maximum;
    maximum.max_materials = maximum.max_layers = maximum.max_loaded_resources =
        maximum.max_rebind_comparisons = maximum.max_output_bytes = SIZE_MAX;
    check(bool(decode(bytes, maximum)),
          "SIZE_MAX limits do not overflow or force allocation");
    MaterialTextureLimits zero;
    zero.max_materials = zero.max_layers = zero.max_loaded_resources =
        zero.max_rebind_comparisons = zero.max_output_bytes = 0;
    auto empty = decode(mf::empty_fixture(), zero);
    check(empty && empty->layouts().empty() && empty->layers().empty(),
          "explicit empty collection fits zero construction budgets");
    if (empty) {
        const auto rebound = empty->rebind({});
        const auto invalid_plan = empty->plan(0);
        check(rebound && *rebound == 0 && !invalid_plan &&
                  invalid_plan.error().code ==
                      MaterialTextureErrorCode::invalid_material,
              "empty collection has no implicit material or stage");
    }
    auto inactive_bytes = bytes;
    for (std::size_t layer = 0; layer < 4; ++layer) {
        tf::set_count(inactive_bytes, layer, 0);
    }
    MaterialTextureLimits no_comparisons;
    no_comparisons.max_rebind_comparisons = 0;
    auto inactive = decode(inactive_bytes, no_comparisons);
    if (inactive) {
        const auto no_stores = inactive->rebind(initial);
        check(no_stores && *no_stores == 0,
              "zero active slots need zero comparisons even with a loaded "
              "resource");
    } else {
        check(false, "all-inactive collection decodes");
    }
}

bool extension_2db(std::string_view path) {
    if (path.size() < 4) {
        return false;
    }
    const auto suffix = path.substr(path.size() - 4);
    return suffix[0] == '.' && suffix[1] == '2' &&
           (suffix[2] == 'd' || suffix[2] == 'D') &&
           (suffix[3] == 'b' || suffix[3] == 'B');
}

struct Corpus {
    std::size_t member;
    std::size_t materials;
    std::size_t slots;
    std::size_t nonzero;
    std::size_t matched;
    std::size_t missing;
};

void physical_bank(const std::filesystem::path& path, const Corpus& expected) {
    const auto archive = BankArchive::read_file(path);
    check(bool(archive), "physical bank opens read-only");
    if (!archive) {
        std::cerr << archive.error().detail << '\n';
        return;
    }
    const auto material_bytes = archive->stored_bytes(expected.member);
    check(bool(material_bytes), "explicit physical 2DM member exists");
    if (!material_bytes) {
        return;
    }
    const auto materials = Material2DM::parse(*material_bytes);
    check(bool(materials), "physical 2DM grammar validates");
    if (!materials) {
        std::cerr << materials.error().detail << '\n';
        return;
    }
    auto bindings = MaterialTextureBindings::from_materials(*materials);
    check(bindings && bindings->layouts().size() == expected.materials &&
              bindings->layers().size() == expected.materials,
          "all physical MAT/PAR/type2/LAY records decode");
    if (!bindings) {
        std::cerr << bindings.error().detail << '\n';
        return;
    }
    std::vector<MaterialTextureResource> resources;
    for (std::size_t member = 0; member < archive->entries().size(); ++member) {
        if (!extension_2db(archive->entries()[member].path)) {
            continue;
        }
        const auto bytes = archive->stored_bytes(member);
        if (!bytes) {
            check(false, "enumerated 2DB member is in bounds");
            continue;
        }
        const auto texture = Texture2DBView::parse(*bytes);
        check(bool(texture),
              "local loaded resource is a validated Texture2DBView");
        if (texture) {
            // A caller-chosen identity, not a member offset or native address.
            resources.push_back(
                {texture->resource_key(), 0x8000000000000000ULL | member});
        } else {
            std::cerr << texture.error().detail << '\n';
        }
    }
    check(!resources.empty() && std::none_of(resources.begin(), resources.end(),
                                             [](const auto& resource) {
                                                 return resource.key ==
                                                        tf::Zero;
                                             }),
          "documented local collection has textures but no zero-key BMAP");
    std::size_t slots{};
    std::size_t nonzero{};
    std::size_t matched{};
    std::size_t missing{};
    std::size_t stores{};
    std::vector<MaterialTexturePlan> fresh;
    for (std::size_t material = 0; material < bindings->layouts().size();
         ++material) {
        const auto plan = get_plan(*bindings, material);
        const auto& layer =
            bindings->layers()[bindings->layouts()[material].layer_index];
        check(plan.stage_count == layer.slot_count,
              "physical plan enumerates every active slot of each material");
        fresh.push_back(plan);
    }
    // Count stores by unique declared LAY, independently of material aliases.
    for (const auto& resource : resources) {
        for (const auto& layer : bindings->layers()) {
            for (std::size_t slot = 0; slot < layer.slot_count; ++slot) {
                stores += layer.slots[slot].key == resource.key;
            }
        }
    }
    const auto rebound = bindings->rebind(resources);
    check(rebound && *rebound == stores,
          "physical ordered rebinding reports every matching unique-LAY store");
    for (std::size_t material = 0; material < bindings->layouts().size();
         ++material) {
        const auto& layout = bindings->layouts()[material];
        const auto& layer = bindings->layers()[layout.layer_index];
        const auto plan = get_plan(*bindings, material);
        for (std::size_t slot = 0; slot < layer.slot_count; ++slot) {
            ++slots;
            const auto& key = layer.slots[slot].key;
            const bool zero = key == tf::Zero;
            nonzero += !zero;
            std::optional<std::uint64_t> oracle;
            for (const auto& resource : resources) {
                if (resource.key == key) {
                    oracle =
                        resource.token; // explicit physical collection order
                }
            }
            const auto fresh_action =
                zero ? MaterialTextureAction::keep_previous
                     : MaterialTextureAction::require_fallback;
            check(fresh[material].stages[slot].action == fresh_action &&
                      !fresh[material].stages[slot].texture_token,
                  "fresh physical cache cannot infer tokens from file words");
            const auto action =
                oracle ? MaterialTextureAction::bind_cached : fresh_action;
            check(plan.stages[slot].action == action &&
                      plan.stages[slot].texture_token == oracle &&
                      same_sampler(plan.stages[slot].sampler,
                                   fresh[material].stages[slot].sampler),
                  "physical result matches independent raw8 ordered-resource "
                  "oracle");
            const auto& sampler = plan.stages[slot].sampler;
            check(sampler.min_mag == MaterialMinMagFilter::linear &&
                      sampler.mip == MaterialMipFilter::linear &&
                      sampler.address_u != MaterialAddressMode::border &&
                      sampler.address_v != MaterialAddressMode::border,
                  "observed physical filter/address subset matches inventory");
            matched += !zero && oracle.has_value();
            missing += !zero && !oracle.has_value();
        }
    }
    check(slots == expected.slots && nonzero == expected.nonzero &&
              matched == expected.matched && missing == expected.missing,
          "all physical material slots match independently inventoried counts");
    std::cout << "BANK " << path.filename().string()
              << " member=" << expected.member
              << " materials=" << bindings->layouts().size()
              << " slots=" << slots << " local_resources=" << resources.size()
              << " matched=" << matched << " missing=" << missing
              << " zero_keep=" << slots - nonzero << '\n';
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 4) {
        std::cerr << "usage: material_textures_test [Alfa_GT.bnk 300_C.bnk "
                     "A3_V6.bnk]\n";
        return 2;
    }
    try {
        layout_and_ownership();
        type_and_count_guards();
        samplers();
        rebind_and_copies();
        alias_and_orphan_layers();
        flags();
        resource_budgets();
        if (argc == 4) {
            physical_bank(argv[1], {19, 34, 169, 73, 27, 46});
            physical_bank(argv[2], {25, 37, 187, 81, 37, 44});
            physical_bank(argv[3], {24, 37, 187, 79, 35, 44});
        }
    } catch (const std::exception& error) {
        check(false, "unexpected fixture exception");
        std::cerr << error.what() << '\n';
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

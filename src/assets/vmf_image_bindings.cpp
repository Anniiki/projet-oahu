#include "tdur/assets/vmf_image_bindings.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace tdur::assets {
namespace {
using Error = VmfImageBindingError;
using Code = VmfImageBindingErrorCode;
template <class T> using Result = std::expected<T, Error>;

std::unexpected<Error>
reject(Code code, std::size_t offset, std::string_view detail,
       std::optional<std::uint32_t> character = {},
       std::optional<std::size_t> catalogue_index = {}) noexcept {
    return std::unexpected(
        Error{code, offset, character, catalogue_index, detail});
}

struct Name {
    // page_ + all ten u32 decimal digits + .2db. No terminating NUL needed.
    std::array<char, 19> bytes{};
    std::size_t length{};
    std::string_view view() const& noexcept { return {bytes.data(), length}; }
};

Name resource_name(const VmfImageReference& image) noexcept {
    const bool page = image.source == VmfImageSource::atlas_page;
    const auto prefix =
        page ? std::string_view("page_") : std::string_view("fla_");
    auto value = page ? image.source_number : image.character_id;
    std::array<char, 10> reversed{};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    Name result;
    for (const auto c : prefix)
        result.bytes[result.length++] = c;
    const auto width = page ? 3U : 4U;
    for (std::size_t i = count; i < width; ++i)
        result.bytes[result.length++] = '0';
    while (count)
        result.bytes[result.length++] = reversed[--count];
    for (const auto c : std::string_view(".2db"))
        result.bytes[result.length++] = c;
    return result;
}

Result<VmfImageBinding> validated_reference(const VmfMovie& movie,
                                            const VmfImageReference& image) {
    const auto records = movie.records();
    if (image.record_index >= records.size())
        return reject(Code::invalid_reference, 0,
                      "Image record index is outside the supplied movie",
                      image.character_id);
    const auto& record = records[image.record_index];
    const auto definition = movie.definition(image.character_id);
    if (record.type != 20 || !record.definition_id ||
        *record.definition_id != image.character_id || !definition ||
        *definition != image.record_index)
        return reject(Code::invalid_reference, record.source_offset,
                      "Image does not identify its type20 definition",
                      image.character_id);
    VmfImageBinding result{image, record, 0, {}};
    switch (image.source) {
    case VmfImageSource::direct_fla:
        if (image.source_number != image.character_id ||
            image.atlas_entry_index)
            return reject(Code::invalid_reference, record.source_offset,
                          "Direct image has inconsistent source metadata",
                          image.character_id);
        break;
    case VmfImageSource::atlas_page: {
        const auto atlas = movie.atlas_entries();
        if (!image.atlas_entry_index ||
            *image.atlas_entry_index >= atlas.size())
            return reject(Code::invalid_reference, record.source_offset,
                          "Atlas image lacks its source TXIX record",
                          image.character_id);
        const auto& entry = atlas[*image.atlas_entry_index];
        if (entry.character_id != image.character_id ||
            entry.page_number != image.source_number)
            return reject(Code::invalid_reference, entry.source_offset,
                          "TXIX and image source identities differ",
                          image.character_id);
        auto raw = movie.bytes({entry.source_offset, 32});
        if (!raw)
            return reject(Code::invalid_reference, entry.source_offset,
                          "TXIX source bytes are outside the movie",
                          image.character_id);
        VmfBoundImageAtlas owned{entry, {}};
        std::copy(raw->begin(), raw->end(), owned.source_bytes.begin());
        result.atlas = owned;
        break;
    }
    default:
        return reject(Code::invalid_reference, record.source_offset,
                      "Unknown VMF image source", image.character_id);
    }
    return result;
}

struct ComparisonBudget {
    std::size_t remaining{};
    bool claim() noexcept {
        if (!remaining)
            return false;
        --remaining;
        return true;
    }
};

Result<std::optional<std::size_t>>
previous_image(std::span<const VmfImageReference> images, std::size_t current,
               ComparisonBudget& budget, std::size_t offset) noexcept {
    for (std::size_t i = 0; i < current; ++i) {
        if (!budget.claim())
            return reject(Code::resource_limit, offset,
                          "Image-source comparisons exceed resolution budget",
                          images[current].character_id);
        // The source prefixes are distinct. Direct source_number was checked
        // against character_id, so equal pairs imply equal complete names.
        if (images[i].source == images[current].source &&
            images[i].source_number == images[current].source_number)
            return std::optional(i);
    }
    return std::optional<std::size_t>{};
}

Result<std::size_t> catalogue_match(std::span<const VmfImageAsset> catalogue,
                                    const Name& name, ComparisonBudget& budget,
                                    const VmfImageBinding& binding) noexcept {
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        if (!budget.claim())
            return reject(Code::resource_limit,
                          binding.definition_record.source_offset,
                          "Catalogue comparisons exceed resolution budget",
                          binding.image.character_id);
        if (catalogue[i].exact_name == name.view()) {
            if (found)
                return reject(Code::ambiguous_asset,
                              binding.definition_record.source_offset,
                              "Used image name occurs more than once in "
                              "selected catalogue",
                              binding.image.character_id, i);
            found = i;
        }
    }
    if (!found)
        return reject(Code::missing_asset,
                      binding.definition_record.source_offset,
                      "Image name is absent from selected catalogue",
                      binding.image.character_id);
    return *found;
}

struct Preflight {
    std::size_t resource_count{}, owned_name_bytes{}, logical_bytes{};
};

Result<Preflight> preflight(const VmfMovie& movie,
                            std::span<const VmfImageAsset> catalogue,
                            const VmfImageBindingLimits& limits) {
    const auto images = movie.images();
    if (movie.source_bytes().size() > limits.max_input_bytes ||
        catalogue.size() > limits.max_catalogue_entries ||
        images.size() > limits.max_images)
        return reject(Code::resource_limit, 0,
                      "Movie/catalogue/image count exceeds binding limits");
    std::size_t catalogue_bytes = 0;
    for (std::size_t i = 0; i < catalogue.size(); ++i) {
        const auto size = catalogue[i].exact_name.size();
        if (size > limits.max_name_bytes ||
            size > limits.max_catalogue_name_bytes - catalogue_bytes)
            return reject(Code::resource_limit, 0,
                          "Catalogue name bytes exceed binding limits", {}, i);
        catalogue_bytes += size;
    }
    Preflight result;
    ComparisonBudget budget{limits.max_resolution_comparisons / 2};
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto validated = validated_reference(movie, images[i]);
        if (!validated)
            return std::unexpected(validated.error());
        const auto at = validated->definition_record.source_offset;
        const auto previous = previous_image(images, i, budget, at);
        if (!previous)
            return std::unexpected(previous.error());
        if (*previous)
            continue;
        if (result.resource_count == limits.max_resources)
            return reject(Code::resource_limit, at,
                          "Unique image resources exceed binding limit",
                          images[i].character_id);
        ++result.resource_count;
        const auto name = resource_name(images[i]);
        if (name.length > limits.max_name_bytes ||
            name.length > limits.max_owned_name_bytes - result.owned_name_bytes)
            return reject(Code::resource_limit, at,
                          "Owned image name bytes exceed binding limits",
                          images[i].character_id);
        result.owned_name_bytes += name.length;
        const auto found = catalogue_match(catalogue, name, budget, *validated);
        if (!found)
            return std::unexpected(found.error());
    }
    const auto claim = [&](std::size_t count, std::size_t stride) {
        if (count > (limits.max_output_bytes - result.logical_bytes) / stride)
            return false;
        result.logical_bytes += count * stride;
        return true;
    };
    if (!claim(images.size(), sizeof(VmfImageBinding)) ||
        !claim(images.size(), sizeof(std::size_t)) ||
        !claim(result.resource_count, sizeof(VmfImageResource)) ||
        !claim(result.owned_name_bytes, 1))
        return reject(Code::resource_limit, 0,
                      "Logical image binding output exceeds byte limit");
    return result;
}
} // namespace

VmfImageBindings& VmfImageBindings::operator=(const VmfImageBindings& other) {
    if (this != &other) {
        VmfImageBindings replacement(other);
        *this = std::move(replacement);
    }
    return *this;
}

std::expected<VmfImageBindings, VmfImageBindingError>
VmfImageBindings::resolve(const VmfMovie& movie,
                          std::span<const VmfImageAsset> catalogue,
                          VmfImageBindingLimits limits) {
    try {
        const auto plan = preflight(movie, catalogue, limits);
        if (!plan)
            return std::unexpected(plan.error());
        VmfImageBindings result;
        result.profile_ = movie.header().profile;
        result.logical_output_bytes_ = plan->logical_bytes;
        const auto images = movie.images();
        result.resources_.reserve(plan->resource_count);
        result.bindings_.reserve(images.size());
        result.id_lookup_.resize(images.size());
        ComparisonBudget budget{limits.max_resolution_comparisons / 2};
        for (std::size_t i = 0; i < images.size(); ++i) {
            auto binding = validated_reference(movie, images[i]);
            if (!binding)
                return std::unexpected(binding.error());
            const auto previous = previous_image(
                images, i, budget, binding->definition_record.source_offset);
            if (!previous)
                return std::unexpected(previous.error());
            if (*previous) {
                binding->resource_index =
                    result.bindings_[**previous].resource_index;
            } else {
                const auto name = resource_name(images[i]);
                const auto found =
                    catalogue_match(catalogue, name, budget, *binding);
                if (!found)
                    return std::unexpected(found.error());
                binding->resource_index = result.resources_.size();
                result.resources_.push_back(
                    {std::string(name.view()), catalogue[*found].member_index});
            }
            result.bindings_.push_back(std::move(*binding));
            result.id_lookup_[i] = i;
        }
        std::sort(result.id_lookup_.begin(), result.id_lookup_.end(),
                  [&](std::size_t a, std::size_t b) {
                      return result.bindings_[a].image.character_id <
                             result.bindings_[b].image.character_id;
                  });
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Code::allocation_failure, 0,
                      "VMF image binding allocation failed");
    } catch (const std::length_error&) {
        return reject(Code::resource_limit, 0,
                      "VMF image binding container length exceeded");
    }
}

std::span<const VmfImageResource>
VmfImageBindings::resources() const& noexcept {
    return resources_;
}
std::span<const VmfImageBinding> VmfImageBindings::bindings() const& noexcept {
    return bindings_;
}
std::optional<std::size_t>
VmfImageBindings::binding_index(std::uint32_t character_id) const& noexcept {
    const auto found =
        std::lower_bound(id_lookup_.begin(), id_lookup_.end(), character_id,
                         [&](std::size_t index, std::uint32_t id) {
                             return bindings_[index].image.character_id < id;
                         });
    if (found == id_lookup_.end() ||
        bindings_[*found].image.character_id != character_id)
        return {};
    return *found;
}
VmfProfile VmfImageBindings::profile() const& noexcept { return profile_; }
std::size_t VmfImageBindings::logical_output_bytes() const& noexcept {
    return logical_output_bytes_;
}
} // namespace tdur::assets

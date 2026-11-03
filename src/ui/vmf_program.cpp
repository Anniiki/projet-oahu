#include "tdur/ui/vmf_program.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace tdur::ui {
namespace {
constexpr auto NoSprite = std::numeric_limits<std::size_t>::max();
assets::VmfError limit_error() noexcept {
    return {assets::VmfErrorCode::resource_limit, 0,
            "VMF program storage limit"};
}
} // namespace

VmfProgram::VmfProgram(assets::VmfMovie movie, assets::VmfDisplayData display,
                       std::optional<assets::VmfTextData> text,
                       std::vector<std::size_t> sprites, std::size_t bytes)
    : movie_(std::move(movie)), display_(std::move(display)),
      text_(std::move(text)), sprites_(std::move(sprites)),
      logical_output_bytes_(bytes) {}

std::expected<std::shared_ptr<const VmfProgram>, assets::VmfError>
VmfProgram::build(const assets::VmfMovie &movie,
                  assets::VmfDisplayLimits display_limits,
                  std::size_t max_output_bytes,
                  std::optional<assets::VmfTextLimits> text_limits) noexcept {
    auto remaining = max_output_bytes;
    const auto charge = [&remaining](std::size_t bytes) {
        if (bytes > remaining)
            return false;
        remaining -= bytes;
        return true;
    };
    const auto count = movie.definitions().size();
    if (!charge(sizeof(VmfProgram)) || !charge(movie.logical_output_bytes()) ||
        count > remaining / sizeof(std::size_t) ||
        !charge(count * sizeof(std::size_t)))
        return std::unexpected(limit_error());

    display_limits.max_output_bytes =
        std::min(display_limits.max_output_bytes, remaining);
    try {
        auto display = assets::VmfDisplayData::decode(movie, display_limits);
        if (!display)
            return std::unexpected(display.error());
        if (!charge(display->logical_output_bytes()))
            return std::unexpected(limit_error());
        std::optional<assets::VmfTextData> text;
        if (text_limits) {
            text_limits->max_output_bytes =
                std::min(text_limits->max_output_bytes, remaining);
            auto decoded = assets::VmfTextData::decode(movie, *text_limits);
            if (!decoded)
                return std::unexpected(decoded.error());
            if (!charge(decoded->logical_output_bytes()))
                return std::unexpected(limit_error());
            text.emplace(std::move(*decoded));
        }
        std::vector<std::size_t> sprites(count, NoSprite);
        for (std::size_t i = 0; i < movie.sprites().size(); ++i) {
            const auto id = movie.sprites()[i].character_id;
            if (id >= count || sprites[id] != NoSprite)
                return std::unexpected(
                    assets::VmfError{assets::VmfErrorCode::invalid_definition,
                                     0, "VMF program sprite identity"});
            sprites[id] = i;
        }
        if (sprites.empty() || sprites[0] == NoSprite)
            return std::unexpected(
                assets::VmfError{assets::VmfErrorCode::invalid_definition, 0,
                                 "VMF program root missing"});
        return std::shared_ptr<const VmfProgram>(
            new VmfProgram(movie, std::move(*display), std::move(text),
                           std::move(sprites), max_output_bytes - remaining));
    } catch (const std::bad_alloc&) {
        return std::unexpected(limit_error());
    } catch (const std::length_error&) {
        return std::unexpected(limit_error());
    }
}

const assets::VmfMovie& VmfProgram::movie() const& noexcept { return movie_; }
const assets::VmfDisplayData& VmfProgram::display() const& noexcept {
    return display_;
}
const assets::VmfTextData *VmfProgram::text_data() const & noexcept {
    return text_ ? &*text_ : nullptr;
}
std::optional<std::size_t>
VmfProgram::sprite_for_character(std::uint32_t id) const noexcept {
    if (id >= sprites_.size() || sprites_[id] == NoSprite)
        return std::nullopt;
    return sprites_[id];
}
std::size_t VmfProgram::logical_output_bytes() const noexcept {
    return logical_output_bytes_;
}

} // namespace tdur::ui

#include "tdur/assets/resource_name_hash.hpp"

#include <array>
#include <limits>

namespace tdur::assets {

std::expected<std::uint64_t, ResourceHashError>
hash_legacy_resource_bytes(std::span<const std::byte> input,
                           std::int32_t count) noexcept {
    if (count <= 0)
        return std::uint64_t{0};
    const auto size = static_cast<std::size_t>(count);
    if (size > input.size())
        return std::unexpected(ResourceHashError::count_exceeds_input);
    std::array<std::uint8_t, 8> lanes{};
    for (std::size_t i = 0; i < size; ++i) {
        auto byte = std::to_integer<std::uint8_t>(input[i]);
        if (byte >= 0x61 && byte <= 0x7a)
            byte = static_cast<std::uint8_t>(byte - 0x20);
        auto& lane = lanes[i % lanes.size()];
        lane = static_cast<std::uint8_t>(lane + byte);
    }
    std::uint64_t result{};
    for (std::size_t i = 0; i < lanes.size(); ++i)
        result |= std::uint64_t{lanes[i]} << (i * 8);
    return result;
}

std::expected<std::uint64_t, ResourceHashError>
hash_resource_name(std::string_view name, std::uint8_t mode) noexcept {
    const auto nul = name.find('\0');
    const auto size = nul == std::string_view::npos ? name.size() : nul;
    if (size >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected(ResourceHashError::input_exceeds_signed_count);
    const auto input = std::as_bytes(std::span<const char>{name.data(), size});
    const auto count = static_cast<std::int32_t>(size);
    return mode ? hash_legacy_resource_bytes(input, count)
                : hash_resource_bytes(input, count);
}

} // namespace tdur::assets

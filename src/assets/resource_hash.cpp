#include "tdur/assets/resource_hash.hpp"

#include <bit>
#include <limits>

namespace tdur::assets {
namespace {
// Derived from table entry1 at F465D0, with its two DWORDs swapped into
// logical shift-register order. All256 generated entries equal the retained
// secondary table. No commercial table blob is embedded in this source.
constexpr std::uint64_t polynomial = 0x0060034000f0d50bULL;
constexpr auto table = [] {
    std::array<std::uint64_t, 256> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto word = std::uint64_t(i) << 56;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto top = word >> 63;
            word = (word << 1) ^ (top ? polynomial : 0);
        }
        result[i] = word;
    }
    return result;
}();
} // namespace

std::expected<std::uint64_t, ResourceHashError>
hash_resource_bytes(std::span<const std::byte> input,
                    std::int32_t count) noexcept {
    if (count <= 0)
        return resource_hash_seed;
    if (static_cast<std::size_t>(count) > input.size())
        return std::unexpected(ResourceHashError::count_exceeds_input);
    // Native ESI is the logical high word, ECX the logical low word; EDX:EAX
    // returns them in the opposite DWORD order. Rotate only at the boundary.
    auto state = std::rotl(resource_hash_seed, 32);
    for (const auto byte : input.first(static_cast<std::size_t>(count)))
        state = ((state << 8) | std::to_integer<std::uint64_t>(byte)) ^
                table[static_cast<std::size_t>(state >> 56)];
    return std::rotl(state, 32);
}

std::expected<std::uint64_t, ResourceHashError>
hash_resource_bytes(std::span<const std::byte> input) noexcept {
    if (input.size() >
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected(ResourceHashError::input_exceeds_signed_count);
    return hash_resource_bytes(input, static_cast<std::int32_t>(input.size()));
}

std::array<std::byte, 8> resource_hash_key_bytes(std::uint64_t hash) noexcept {
    std::array<std::byte, 8> result{};
    for (unsigned i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::byte>((hash >> (i * 8)) & 0xff);
    return result;
}
} // namespace tdur::assets

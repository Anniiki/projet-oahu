#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tdur::ui {

class VmfDisplayRuntime;

struct VmfMenuMovieState {
    // Native +AC4 on this same movie. Unknown is not an invented zero.
    std::optional<std::uint32_t> flags_ac4;
};
struct VmfMenuMovie {
    bool active{};                // Native byte +2E0 is nonzero.
    std::uint32_t group{};        // +AD4
    std::uint32_t kind{};         // +2DC; kind1 has a VMF display.
    VmfDisplayRuntime* display{}; // Modern owned counterpart of +2A8.
    VmfMenuMovieState state{};
};
// 99A780: fresh filtered rank, with last-match fallback for an absent rank.
// Kind/display/flags do not participate in this filter. Serialize all access
// and keep the physical span and its owned movies alive through any borrow.
[[nodiscard]] const VmfMenuMovie*
find_vmf_active_menu(std::span<const VmfMenuMovie> movies, std::uint32_t group,
                     std::uint32_t ordinal) noexcept;
// 99A9A0: same filter, native unsigned32 result. The native physical count
// has32 bits; a binding must reject a span larger than UINT32_MAX entries.
[[nodiscard]] std::uint32_t
count_vmf_active_menus(std::span<const VmfMenuMovie> movies,
                       std::uint32_t group) noexcept;

} // namespace tdur::ui

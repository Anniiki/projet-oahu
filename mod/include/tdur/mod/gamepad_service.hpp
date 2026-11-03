#pragma once

#include "tdur/mod/gamepad_dinput_format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// Process-local SDL gamepad service of the Win32 diagnostic mod.
// SDL is created, pumped and destroyed only on the service thread, which is
// started lazily from TdurDirectInput8Create, never from DllMain. It opens
// no window and initialises SDL_INIT_GAMEPAD only: no keyboard/mouse hook,
// no system virtual pad, no change visible outside the process.
namespace tdur::mod::gamepad {

inline constexpr std::size_t MaxHiddenProducts = 16;

struct ServiceState {
    bool ready{};  // First enumeration published.
    bool failed{}; // SDL creation failed; the proxy then forwards unchanged.
    PadSample sample;
    std::uint64_t generation{};   // Increments on every published change.
    std::uint64_t active_token{}; // SdlGamepads connection token, 0 if none.
    int active_type{};            // SDL_GamepadType of the active pad.
    std::uint16_t active_vendor{}, active_product{};
    // VID/PID of SDL-mapped pads, hidden from the game's DirectInput
    // enumeration while the synthetic device represents them.
    std::array<std::uint32_t, MaxHiddenProducts> hidden_products{};
    std::size_t hidden_count{};
};

// TDUR_GAMEPAD=sdl in the process environment supplied by the launcher.
// Read outside DllMain; any other value forwards the native factory unchanged.
[[nodiscard]] bool ServiceRequested() noexcept;
// Takes one lease; the first lease starts the thread and waits, bounded,
// for the first enumeration. Returns false if SDL is unavailable.
[[nodiscard]] bool AcquireService() noexcept;
// Drops one lease; the last one asks the thread to close SDL itself.
// Uses a short lifecycle lock, without waiting for SDL teardown.
void ReleaseService() noexcept;
// Fixed-size copy under a shared lock; no allocation, usable per frame.
[[nodiscard]] ServiceState ReadService() noexcept;
// True on the service thread: DirectInput8Create calls made there are
// forwarded unwrapped and never re-enter the service.
[[nodiscard]] bool OnServiceThread() noexcept;

} // namespace tdur::mod::gamepad

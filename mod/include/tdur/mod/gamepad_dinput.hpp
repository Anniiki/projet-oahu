#pragma once

#include <windows.h>

// In-process DirectInput integration of the SDL gamepad service.
// Returns the factory to hand back to the game: a wrapper around `system`
// for IID_IDirectInput8A/W, or `system` itself (unchanged) for any other
// IID, an aggregated creation or an unavailable service. The wrapper takes
// over the caller's reference; on failure the original object is returned.
namespace tdur::mod::gamepad {
[[nodiscard]] void* WrapFactory(REFIID iid, void* system) noexcept;
}

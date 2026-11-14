#pragma once

// Present heartbeat read by the owned launcher. Header only on
// purpose: tdur_display_hooks is a static library also linked into its own
// test executable, where the pointer simply stays null.

#include <windows.h>

#include <atomic>

namespace tdur::mod {

inline std::atomic<volatile LONG*> present_heartbeat{nullptr};

inline void beat_present() noexcept {
    if (auto* counter = present_heartbeat.load(std::memory_order_acquire))
        InterlockedIncrement(counter);
}

} // namespace tdur::mod

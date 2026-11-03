#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

struct TdurPhysicsContextSample {
    bool ready{};
    std::uint16_t x87_control{};
    std::uint32_t mxcsr{};
    std::uint32_t timer_id{};
    std::uint32_t caller_code_address{};
    bool operator==(const TdurPhysicsContextSample&) const = default;
};

struct TdurPhysicsContextSnapshot {
    std::uint32_t reserved_slots{};
    std::array<TdurPhysicsContextSample, 32> samples;
    bool operator==(const TdurPhysicsContextSnapshot&) const = default;
};

// Startup transaction only. All code/storage remain until process exit.
// No timer, integrator, flags, FPU setting or callback arguments are changed.
LONG TdurQueueRetailPhysicsContextGate(bool& queued) noexcept;
bool TdurVerifyRetailPhysicsContextGate() noexcept;
// Outside loader lock/callback: compare the worker's fixed call cell with
// the already loaded system API; never follows or invokes that cell.
void TdurObservePhysicsTimerBinding() noexcept;
TdurPhysicsContextSnapshot TdurReadPhysicsContext() noexcept;
// Ordinary presentation thread only; NEVER from the multimedia callback.
void TdurFlushPhysicsContext() noexcept;

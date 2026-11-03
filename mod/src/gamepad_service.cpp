// SDL owner thread of the in-process gamepad device. See gamepad_service.hpp.
#include "tdur/mod/gamepad_service.hpp"

#include "tdur/input/sdl_gamepads.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>

namespace tdur::mod::gamepad {
namespace {

using tdur::input::GamepadControls;
using tdur::input::GamepadControlsSnapshot;
using tdur::input::LastInputKind;
using tdur::input::SdlGamepads;

SRWLOCK stateLock = SRWLOCK_INIT;
ServiceState published{};
SRWLOCK lifecycleLock = SRWLOCK_INIT;
CONDITION_VARIABLE lifecycleChanged = CONDITION_VARIABLE_INIT;
INIT_ONCE threadOnce = INIT_ONCE_STATIC_INIT;
thread_local bool serviceThread = false;

enum class Phase { idle, starting, ready, stopping };
struct Lifecycle {
    Phase phase = Phase::idle;
    std::uint64_t generation = 0;
    // Includes pending acquisitions. A ready session cannot close while
    // either an accepted lease or a pending acquisition still owns one.
    std::uint64_t leases = 0;
} lifecycle;

class LifecycleGuard {
  public:
    LifecycleGuard() noexcept { AcquireSRWLockExclusive(&lifecycleLock); }
    ~LifecycleGuard() { ReleaseSRWLockExclusive(&lifecycleLock); }
    LifecycleGuard(const LifecycleGuard&) = delete;
    LifecycleGuard& operator=(const LifecycleGuard&) = delete;
};

// Bounded first-enumeration wait. A caller holding the loader lock would
// otherwise deadlock with SDL's own LoadLibrary calls.
constexpr DWORD ReadyTimeoutMs = 2000;
constexpr Uint32 PumpTimeoutMs = 8;

// The caller holds lifecycleLock. All condition waits for one acquisition
// share this deadline, including waiting for a previous session to close.
bool WaitForLifecycle(ULONGLONG deadline) noexcept {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) {
        return false;
    }
    return SleepConditionVariableSRW(&lifecycleChanged, &lifecycleLock,
                                     DWORD(deadline - now), 0) != FALSE;
}

struct PadMetadata {
    std::uint64_t token{};
    int type{};
    std::uint16_t vendor{}, product{};
};

void Publish(const ServiceState& state) noexcept {
    AcquireSRWLockExclusive(&stateLock);
    const auto generation = published.generation;
    published = state;
    published.generation = generation + 1;
    ReleaseSRWLockExclusive(&stateLock);
}

// A failed startup has no accepted leases: discard only its pending
// reservations. Their waiters observe stopping/idle or a changed generation,
// and must never decrement a later session's count.
void FailStartingSession() noexcept {
    LifecycleGuard guard;
    lifecycle.phase = Phase::stopping;
    lifecycle.leases = 0;
    WakeAllConditionVariable(&lifecycleChanged);
}

// Called only after the owner has finished closing every SDL object.
void FinishSession() noexcept {
    Publish(ServiceState{});
    LifecycleGuard guard;
    lifecycle.phase = Phase::idle;
    WakeAllConditionVariable(&lifecycleChanged);
}

float Axis(const GamepadControls& c, SDL_GamepadAxis axis) noexcept {
    const float value = c.axes[std::size_t(axis)];
    return std::isfinite(value) ? value : 0.0f;
}
bool Button(const GamepadControls& c, SDL_GamepadButton button) noexcept {
    return c.buttons[std::size_t(button)];
}

// Modern layout policy (see gamepad_dinput_format.hpp). SDL physical
// positions are the same ones used by ControllerBindings::xbox360_defaults,
// so SOUTH stays button 0 on Xbox and PlayStation pads alike.
// Hypothesis to verify against the real xusb driver: LT raises Z, RT
// lowers it, both cancel.
PadSample SampleFrom(const GamepadControls& c) noexcept {
    PadSample sample;
    sample.connected = true;
    sample.axes = {
        Axis(c, SDL_GAMEPAD_AXIS_LEFTX),
        Axis(c, SDL_GAMEPAD_AXIS_LEFTY),
        std::clamp(Axis(c, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) -
                       Axis(c, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
                   -1.0f, 1.0f),
        Axis(c, SDL_GAMEPAD_AXIS_RIGHTX),
        Axis(c, SDL_GAMEPAD_AXIS_RIGHTY),
    };
    constexpr std::array<SDL_GamepadButton, ButtonCount> order{
        SDL_GAMEPAD_BUTTON_SOUTH,         SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_WEST,          SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_BACK,          SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK,    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    };
    for (std::size_t i = 0; i < order.size(); ++i) {
        sample.buttons[i] = Button(c, order[i]);
    }
    sample.pov = pov_from_dpad(Button(c, SDL_GAMEPAD_BUTTON_DPAD_UP),
                               Button(c, SDL_GAMEPAD_BUTTON_DPAD_DOWN),
                               Button(c, SDL_GAMEPAD_BUTTON_DPAD_LEFT),
                               Button(c, SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    return sample;
}

class Session {
  public:
    explicit Session(std::unique_ptr<SdlGamepads> pads)
        : pads_(std::move(pads)) {}

    void Refresh(bool devices_changed) noexcept {
        if (devices_changed) {
            RefreshMetadata();
        }
        const auto controls = pads_->controls_snapshot();
        if (!controls) {
            return;
        }
        const GamepadControlsSnapshot& snapshot = *controls;
        const auto mapped = [&](std::uint64_t token)
            -> const tdur::input::GamepadControlsDevice* {
            for (std::size_t i = 0; i < snapshot.device_count; ++i) {
                const auto& device = snapshot.devices[i];
                if (device.token == token && device.mapped) {
                    return &device;
                }
            }
            return nullptr;
        };
        const tdur::input::GamepadControlsDevice* active = nullptr;
        if (snapshot.last_input.kind == LastInputKind::gamepad &&
            snapshot.last_input.gamepad) {
            active = mapped(*snapshot.last_input.gamepad);
        }
        if (active == nullptr && active_token_ != 0) {
            active = mapped(active_token_);
        }
        for (std::size_t i = 0; active == nullptr && i < snapshot.device_count;
             ++i) {
            if (snapshot.devices[i].mapped) {
                active = &snapshot.devices[i];
            }
        }
        ServiceState state;
        state.ready = true;
        state.hidden_products = hidden_;
        state.hidden_count = hidden_count_;
        if (active != nullptr) {
            state.sample = SampleFrom(active->controls);
            state.active_token = active->token;
            for (std::size_t i = 0; i < metadata_count_; ++i) {
                if (metadata_[i].token == active->token) {
                    state.active_type = metadata_[i].type;
                    state.active_vendor = metadata_[i].vendor;
                    state.active_product = metadata_[i].product;
                }
            }
        }
        if (state.active_token != active_token_) {
            TdurObservationRecord("gamepad-active-changed",
                                  DWORD(state.active_type));
        }
        active_token_ = state.active_token;
        if (!last_ || !(last_state_.sample == state.sample) ||
            last_state_.active_token != state.active_token ||
            last_state_.active_type != state.active_type ||
            last_state_.hidden_count != state.hidden_count ||
            last_state_.hidden_products != state.hidden_products) {
            Publish(state);
            last_state_ = state;
            last_ = true;
        }
    }

    void Pump() noexcept {
        SDL_Event event;
        if (!SDL_WaitEventTimeout(&event, PumpTimeoutMs)) {
            return;
        }
        bool devices = false, changed = false;
        do {
            auto result = pads_->handle_event(event);
            if (!result) {
                TdurObservationRecord("gamepad-event-refused",
                                      DWORD(result.error().code));
                auto rescanned = pads_->rescan();
                devices = true;
                changed = true;
                (void)rescanned;
                continue;
            }
            devices |= result->devices_changed;
            changed |= result->devices_changed || result->controls_changed ||
                       result->last_input_changed;
        } while (SDL_PollEvent(&event));
        if (changed) {
            Refresh(devices);
        }
    }

  private:
    void RefreshMetadata() noexcept {
        auto snapshot = pads_->snapshot(); // Allocates; device changes only.
        if (!snapshot) {
            return;
        }
        hidden_count_ = metadata_count_ = 0;
        for (const auto& device : snapshot->devices) {
            if (!device.mapped) {
                continue;
            }
            if (metadata_count_ < metadata_.size()) {
                metadata_[metadata_count_++] = {device.token, int(device.type),
                                                device.vendor, device.product};
            }
            if (hidden_count_ < hidden_.size() && device.vendor != 0) {
                hidden_[hidden_count_++] =
                    std::uint32_t(device.vendor) |
                    (std::uint32_t(device.product) << 16);
            }
        }
        TdurObservationRecord("gamepad-devices-changed",
                              DWORD(metadata_count_));
    }

    std::unique_ptr<SdlGamepads> pads_;
    std::uint64_t active_token_{};
    std::array<PadMetadata, 32> metadata_{};
    std::size_t metadata_count_{};
    std::array<std::uint32_t, MaxHiddenProducts> hidden_{};
    std::size_t hidden_count_{};
    ServiceState last_state_{};
    bool last_{};
};

void RunSession() noexcept {
    // Process-local hints, set before SDL initialisation on this thread:
    // - no SDL DirectInput backend: it would load this very dinput8 proxy;
    // - no WGI backend: avoids a WinRT apartment on the service thread;
    // - background events allowed: SDL has no window here; foreground gating
    //   follows the cooperative level requested by the game (device side).
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    auto created = SdlGamepads::create();
    if (!created) {
        TdurObservationRecord("gamepad-sdl-unavailable",
                              DWORD(created.error().code));
        ServiceState state;
        state.failed = true;
        Publish(state);
        FailStartingSession();
        SDL_Quit();
        FinishSession();
        return;
    }
    {
        Session session(std::move(*created));
        session.Refresh(true);
        const auto initial = ReadService();
        bool ready = false;
        {
            LifecycleGuard guard;
            ready = initial.ready && !initial.failed && lifecycle.leases != 0;
            if (ready) {
                lifecycle.phase = Phase::ready;
            } else {
                lifecycle.phase = Phase::stopping;
                lifecycle.leases = 0;
            }
            WakeAllConditionVariable(&lifecycleChanged);
        }
        if (ready) {
            TdurObservationRecord("gamepad-service-started", ERROR_SUCCESS);
            for (;;) {
                {
                    LifecycleGuard guard;
                    if (lifecycle.leases == 0) {
                        // Commit closure under the same lock used to grant
                        // leases. New callers must wait for the next session.
                        lifecycle.phase = Phase::stopping;
                        WakeAllConditionVariable(&lifecycleChanged);
                        break;
                    }
                }
                session.Pump();
            }
        }
    } // Handles closed and SDL_INIT_GAMEPAD released on this same thread.
    SDL_Quit();
    FinishSession();
    TdurObservationRecord("gamepad-service-stopped", ERROR_SUCCESS);
}

DWORD WINAPI ServiceMain(void*) noexcept {
    serviceThread = true;
    for (;;) {
        {
            LifecycleGuard guard;
            while (lifecycle.phase != Phase::starting) {
                SleepConditionVariableSRW(&lifecycleChanged, &lifecycleLock,
                                          INFINITE, 0);
            }
            if (lifecycle.leases == 0) {
                // Every waiting caller timed out before startup began.
                lifecycle.phase = Phase::idle;
                WakeAllConditionVariable(&lifecycleChanged);
                continue;
            }
        }
        // Same thread for every session. No SDL call holds lifecycleLock.
        RunSession();
    }
}

BOOL CALLBACK StartThread(PINIT_ONCE, PVOID, PVOID*) noexcept {
    // The thread lives for the process; it is never terminated or joined
    // from DllMain. At process exit Windows ends it before DLL detach.
    HANDLE thread = CreateThread(nullptr, 0, ServiceMain, nullptr, 0, nullptr);
    if (thread == nullptr) {
        // FALSE leaves InitOnce retryable. Locks/condition variables need no
        // handles, so this failure has no partially allocated events to leak.
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}

} // namespace

bool ServiceRequested() noexcept {
    wchar_t value[8]{};
    const DWORD length = GetEnvironmentVariableW(L"TDUR_GAMEPAD", value,
                                                 DWORD(std::size(value)));
    return length == 3 && std::wcscmp(value, L"sdl") == 0;
}

bool AcquireService() noexcept {
    if (serviceThread) {
        return false;
    }
    if (!InitOnceExecuteOnce(&threadOnce, StartThread, nullptr, nullptr)) {
        return false;
    }
    const ULONGLONG deadline = GetTickCount64() + ReadyTimeoutMs;
    LifecycleGuard guard;
    while (lifecycle.phase == Phase::stopping) {
        if (!WaitForLifecycle(deadline)) {
            return false;
        }
    }
    if (lifecycle.phase == Phase::idle) {
        ++lifecycle.generation;
        lifecycle.phase = Phase::starting;
    }
    const auto generation = lifecycle.generation;
    ++lifecycle.leases;
    WakeAllConditionVariable(&lifecycleChanged);
    while (lifecycle.generation == generation &&
           lifecycle.phase == Phase::starting) {
        if (!WaitForLifecycle(deadline)) {
            // A failed startup may have discarded this reservation while the
            // condition wait reacquired its lock. Only our live generation
            // can lose this lease; a replacement session is independent.
            if (lifecycle.generation == generation &&
                (lifecycle.phase == Phase::starting ||
                 lifecycle.phase == Phase::ready)) {
                --lifecycle.leases;
                WakeAllConditionVariable(&lifecycleChanged);
            }
            return false;
        }
    }
    // In the ready phase our reservation becomes an accepted lease before
    // releasing the lock. Shutdown cannot commit while that lease exists.
    return lifecycle.generation == generation &&
           lifecycle.phase == Phase::ready;
}

void ReleaseService() noexcept {
    LifecycleGuard guard;
    if (lifecycle.phase == Phase::ready && lifecycle.leases != 0) {
        --lifecycle.leases;
        WakeAllConditionVariable(&lifecycleChanged);
    }
}

ServiceState ReadService() noexcept {
    AcquireSRWLockShared(&stateLock);
    const ServiceState copy = published;
    ReleaseSRWLockShared(&stateLock);
    return copy;
}

bool OnServiceThread() noexcept { return serviceThread; }

} // namespace tdur::mod::gamepad

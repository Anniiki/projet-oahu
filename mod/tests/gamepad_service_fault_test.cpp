// Private fault/race witness. Win32 wrappers are forced into this target only.
// The real SDL library and the production proxy do not receive these macros.
#include "tdur/mod/gamepad_service.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#undef CreateEventW
#undef CreateThread
#undef CloseHandle
namespace {
std::atomic<int> event_calls{}, thread_calls{}, live_handles{};
std::atomic<int> fail_event{};
std::atomic<bool> fail_thread{}, fail_cpp{}, block_cleanup{};
HANDLE stopped{}, cleanup_entered{}, cleanup_continue{}, acquire_done{};
unsigned checks{}, failures{};
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", message);
    }
}
} // namespace
HANDLE WINAPI TestServiceCreateEventW(LPSECURITY_ATTRIBUTES a, BOOL m, BOOL i,
                                      LPCWSTR n) {
    const int call = ++event_calls;
    if (call == fail_event) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    HANDLE h = CreateEventW(a, m, i, n);
    if (h)
        ++live_handles;
    return h;
}
HANDLE WINAPI TestServiceCreateThread(LPSECURITY_ATTRIBUTES a, SIZE_T s,
                                      LPTHREAD_START_ROUTINE f, LPVOID c,
                                      DWORD flags, LPDWORD id) {
    ++thread_calls;
    if (fail_thread.exchange(false)) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    HANDLE h = CreateThread(a, s, f, c, flags, id);
    if (h)
        ++live_handles;
    return h;
}
BOOL WINAPI TestServiceCloseHandle(HANDLE h) {
    const BOOL ok = CloseHandle(h);
    if (ok)
        --live_handles;
    return ok;
}
void* operator new(std::size_t size) {
    if (tdur::mod::gamepad::OnServiceThread() && fail_cpp.exchange(false))
        throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void TdurObservationRecord(const char* event, DWORD, const wchar_t*,
                           const char*, const void*) noexcept {
    if (std::strcmp(event, "gamepad-service-stopped") == 0)
        SetEvent(stopped);
}
namespace {
using namespace tdur::mod::gamepad;
void SDLCALL Cleanup(void*) {
    if (block_cleanup.exchange(false)) {
        SetEvent(cleanup_entered);
        WaitForSingleObject(cleanup_continue, 5000);
    }
}
void SDLCALL Attach(void* result) {
    auto& id = *static_cast<SDL_JoystickID*>(result);
    if (!OnServiceThread() || !SDL_IsMainThread())
        return;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "Owned shutdown barrier";
    desc.Cleanup = Cleanup;
    id = SDL_AttachVirtualJoystick(&desc);
}
void WaitReady(bool connected) {
    const auto deadline = GetTickCount64() + 3000;
    while (ReadService().sample.connected != connected &&
           GetTickCount64() < deadline)
        Sleep(2);
    Check(ReadService().sample.connected == connected,
          "expected published connection");
}
void Recover(const char* mode) {
    if (std::strcmp(mode, "event") == 0)
        fail_event = 2;
    else if (std::strcmp(mode, "thread") == 0)
        fail_thread = true;
    else
        fail_cpp = true;
    const bool first = AcquireService();
    if (std::strcmp(mode, "event") == 0 && event_calls == 0) {
        // Corrected design may remove all dynamic readiness/wake events.
        Check(first, "service needs no fallible event allocation");
        if (first)
            ReleaseService();
        Check(live_handles == 0, "no dynamic handle retained");
    } else {
        Check(!first, "injected initialization failure observed");
        if (first)
            ReleaseService();
        if (std::strcmp(mode, "cpp") != 0)
            Check(live_handles == 0, "failed initialization leaves no handles");
        const bool retry = AcquireService();
        Check(retry, "next acquisition recovers without a stale failure");
        if (retry) {
            Check(ReadService().ready && !ReadService().failed,
                  "retry is ready");
            ReleaseService();
        }
    }
    WaitForSingleObject(stopped, 3000);
}
void ShutdownRace(bool timeout) {
    const bool first = AcquireService();
    Check(first, "initial service acquired");
    if (!first)
        return;
    SDL_JoystickID id{};
    Check(SDL_RunOnMainThread(Attach, &id, true) && id != 0,
          "virtual cleanup barrier attached on owner");
    WaitReady(true);
    block_cleanup = true;
    ReleaseService();
    const bool entered =
        WaitForSingleObject(cleanup_entered, 3000) == WAIT_OBJECT_0;
    Check(entered, "teardown reaches owned cleanup barrier");
    if (!entered) {
        SetEvent(cleanup_continue);
        return;
    }
    std::atomic<bool> acquired{};
    std::thread contender([&] {
        acquired = AcquireService();
        SetEvent(acquire_done);
    });
    const bool blocked = WaitForSingleObject(acquire_done, 100) == WAIT_TIMEOUT;
    Check(blocked, "acquisition cannot succeed during an engaged teardown");
    if (timeout) {
        const bool expired =
            WaitForSingleObject(acquire_done, 3000) == WAIT_OBJECT_0;
        Check(expired, "acquisition deadline expires while cleanup is blocked");
        Check(!acquired, "timed out caller receives no lease");
        SetEvent(cleanup_continue);
        contender.join();
        Check(WaitForSingleObject(stopped, 3000) == WAIT_OBJECT_0,
              "old generation closes after timeout");
        const bool recovered = AcquireService();
        Check(recovered, "later caller recovers after a closing timeout");
        if (recovered) {
            ReleaseService();
            Check(WaitForSingleObject(stopped, 3000) == WAIT_OBJECT_0,
                  "timeout left no phantom lease in recovered session");
        }
        return;
    }
    SetEvent(cleanup_continue);
    // If the broken baseline returned early, the first auto-reset wait
    // already consumed its completion signal. Do not count that twice.
    const bool finished =
        !blocked || WaitForSingleObject(acquire_done, 4000) == WAIT_OBJECT_0;
    Check(finished, "acquisition finishes after teardown");
    contender.join();
    Check(acquired, "new generation successfully acquired");
    if (acquired) {
        Check(ReadService().ready && !ReadService().failed,
              "successful acquisition owns ready generation");
        ReleaseService();
    }
    WaitForSingleObject(stopped, 3000);
}
void SharedLease() {
    const bool held = AcquireService();
    Check(held, "long-lived lease acquired");
    if (!held)
        return;
    std::atomic<unsigned> failed{};
    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(gate != nullptr, "parallel caller gate created");
    if (!gate) {
        ReleaseService();
        return;
    }
    const auto caller = [&] {
        WaitForSingleObject(gate, 3000);
        for (unsigned i = 0; i < 20; ++i) {
            if (!AcquireService()) {
                ++failed;
                continue;
            }
            if (!ReadService().ready)
                ++failed;
            ReleaseService();
        }
    };
    std::thread a(caller), b(caller), c(caller), d(caller);
    SetEvent(gate);
    a.join();
    b.join();
    c.join();
    d.join();
    CloseHandle(gate);
    Check(failed == 0, "all 80 concurrent acquisitions own a ready session");
    Check(WaitForSingleObject(stopped, 100) == WAIT_TIMEOUT,
          "other callers cannot close the retained lease");
    Check(ReadService().ready, "retained lease still has a published service");
    ReleaseService();
    Check(WaitForSingleObject(stopped, 3000) == WAIT_OBJECT_0,
          "final release closes without leaked reservations");
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const wchar_t* hints[] = {
        L"SDL_JOYSTICK_HIDAPI",    L"SDL_JOYSTICK_RAWINPUT",
        L"SDL_JOYSTICK_GAMEINPUT", L"SDL_JOYSTICK_DIRECTINPUT",
        L"SDL_JOYSTICK_WGI",       L"SDL_XINPUT_ENABLED"};
    for (const auto* hint : hints)
        SetEnvironmentVariableW(hint, L"0");
    stopped = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    cleanup_entered = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    cleanup_continue = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    acquire_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopped || !cleanup_entered || !cleanup_continue || !acquire_done)
        return 2;
    if (std::strcmp(argv[1], "closing") == 0)
        ShutdownRace(false);
    else if (std::strcmp(argv[1], "closing-timeout") == 0)
        ShutdownRace(true);
    else if (std::strcmp(argv[1], "shared") == 0)
        SharedLease();
    else
        Recover(argv[1]);
    std::printf("mode=%s checks=%u failures=%u event_calls=%d thread_calls=%d "
                "live_handles=%d\n",
                argv[1], checks, failures, event_calls.load(),
                thread_calls.load(), live_handles.load());
    // These private handles remain valid until process exit because the
    // observed baseline may still have a delayed owner callback after failure.
    return failures ? 1 : 0;
}

// Real SDL service lifecycle; all physical joystick backends disabled.
#include "tdur/mod/gamepad_service.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_thread.h>
#include <atomic>
#include <cstdio>
#include <cstring>
namespace {
HANDLE started{}, stopped{};
std::atomic<unsigned long> owner{};
std::atomic<unsigned> bad_thread{}, failures{};
} // namespace
void TdurObservationRecord(const char* event, DWORD code, const wchar_t*,
                           const char*, const void*) noexcept {
    if (std::strcmp(event, "gamepad-service-started") == 0) {
        const auto thread = GetCurrentThreadId();
        const auto prior = owner.exchange(thread);
        if (!SDL_IsMainThread() || (prior != 0 && prior != thread))
            ++bad_thread;
        SetEvent(started);
    } else if (std::strcmp(event, "gamepad-service-stopped") == 0) {
        SetEvent(stopped);
    } else if (std::strcmp(event, "gamepad-sdl-unavailable") == 0) {
        ++failures;
        std::printf("SDL service failed code=%lu\n", code);
    }
}
int main() {
    const wchar_t* hints[] = {
        L"SDL_JOYSTICK_HIDAPI",    L"SDL_JOYSTICK_RAWINPUT",
        L"SDL_JOYSTICK_GAMEINPUT", L"SDL_JOYSTICK_DIRECTINPUT",
        L"SDL_JOYSTICK_WGI",       L"SDL_XINPUT_ENABLED"};
    for (const auto* hint : hints)
        SetEnvironmentVariableW(hint, L"0");
    started = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopped = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!started || !stopped)
        return 2;
    unsigned cycles{};
    for (; cycles != 5; ++cycles) {
        if (!tdur::mod::gamepad::AcquireService()) {
            std::puts("AcquireService failed");
            ++failures;
            break;
        }
        const bool entered =
            WaitForSingleObject(started, 3000) == WAIT_OBJECT_0;
        const auto state = tdur::mod::gamepad::ReadService();
        if (!entered || !state.ready || state.failed ||
            state.sample.connected || state.active_token != 0 ||
            state.hidden_count != 0)
            ++failures;
        // Additional COM-like reference must share the same service session.
        if (tdur::mod::gamepad::AcquireService())
            tdur::mod::gamepad::ReleaseService();
        else
            ++failures;
        tdur::mod::gamepad::ReleaseService();
        if (WaitForSingleObject(stopped, 3000) != WAIT_OBJECT_0) {
            ++failures;
            break;
        }
        if (tdur::mod::gamepad::ReadService().ready)
            ++failures;
    }
    std::printf("cycles=%u wrong_thread=%u failures=%u\n", cycles,
                bad_thread.load(), failures.load());
    CloseHandle(started);
    CloseHandle(stopped);
    return cycles == 5 && bad_thread == 0 && failures == 0 ? 0 : 1;
}

// Real SDL virtual pad -> unchanged service -> corrected DirectInput facade.
// No game, keyboard/mouse hooks, physical backends, visible window or focus.
#include "tdur/mod/gamepad_dinput.hpp"
#include "tdur/mod/gamepad_service.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
using namespace tdur::mod::gamepad;
unsigned checks{};
HANDLE service_stopped{};
std::atomic<unsigned> bad_owner{}, service_failures{};
SDL_JoystickID virtual_id{};
SDL_Joystick* virtual_pad{};
enum class Operation { attach, press, east, neutral, detach };
struct Command {
    Operation operation;
    bool ok{};
    char error[256]{};
};

void Check(bool ok, const char* text) {
    ++checks;
    if (!ok)
        throw std::runtime_error(text);
}
template <class Predicate> void Await(Predicate predicate, const char* text) {
    const auto deadline = GetTickCount64() + 3000;
    while (!predicate() && GetTickCount64() < deadline)
        Sleep(2);
    Check(predicate(), text);
}
void SDLCALL Inject(void* opaque) {
    auto& command = *static_cast<Command*>(opaque);
    if (!OnServiceThread() || !SDL_IsMainThread()) {
        ++bad_owner;
        return;
    }
    bool ok = true;
    if (command.operation == Operation::attach) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0xf17e;
        desc.product_id = 0x0137;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.name = "Oahu owned virtual fixture";
        virtual_id = SDL_AttachVirtualJoystick(&desc);
        if (virtual_id)
            virtual_pad = SDL_OpenJoystick(virtual_id);
        ok = virtual_id != 0 && virtual_pad != nullptr;
    }
    if (command.operation == Operation::detach) {
        if (virtual_pad) {
            SDL_CloseJoystick(virtual_pad);
            virtual_pad = nullptr;
        }
        if (virtual_id)
            ok = SDL_DetachVirtualJoystick(virtual_id);
        virtual_id = 0;
    } else if (virtual_pad) {
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i)
            ok = SDL_SetJoystickVirtualAxis(
                     virtual_pad, i,
                     i >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0) &&
                 ok;
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
            ok = SDL_SetJoystickVirtualButton(virtual_pad, i, false) && ok;
        if (command.operation == Operation::press) {
            ok = SDL_SetJoystickVirtualButton(virtual_pad,
                                              SDL_GAMEPAD_BUTTON_SOUTH, true) &&
                 ok;
            ok = SDL_SetJoystickVirtualButton(
                     virtual_pad, SDL_GAMEPAD_BUTTON_DPAD_UP, true) &&
                 ok;
            ok = SDL_SetJoystickVirtualButton(
                     virtual_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true) &&
                 ok;
            ok = SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_LEFTX,
                                            32767) &&
                 ok;
            ok = SDL_SetJoystickVirtualAxis(virtual_pad,
                                            SDL_GAMEPAD_AXIS_RIGHTY, -32768) &&
                 ok;
            ok = SDL_SetJoystickVirtualAxis(
                     virtual_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767) &&
                 ok;
        } else if (command.operation == Operation::east) {
            ok = SDL_SetJoystickVirtualButton(virtual_pad,
                                              SDL_GAMEPAD_BUTTON_EAST, true) &&
                 ok;
        }
    } else
        ok = false;
    command.ok = ok;
    if (!ok)
        std::snprintf(command.error, sizeof(command.error), "%s",
                      SDL_GetError());
}
void Apply(Operation op) {
    Check((SDL_WasInit(SDL_INIT_EVENTS) & SDL_INIT_EVENTS) != 0,
          "event owner active before injection");
    Command command{op};
    Check(SDL_RunOnMainThread(Inject, &command, true),
          "queue callback to SDL owner");
    if (!command.ok)
        std::printf("injection error: %s\n", command.error);
    Check(command.ok, "virtual operation succeeds on owner thread");
}
BOOL CALLBACK FirstPad(const DIDEVICEINSTANCEW* instance, void* output) {
    *static_cast<GUID*>(output) = instance->guidInstance;
    return DIENUM_STOP; // Stop on the synthetic first entry; no physical
                        // enumeration.
}
DIJOYSTATE2 State(IDirectInputDevice8W* device) {
    DIJOYSTATE2 state{};
    Check(device->GetDeviceState(sizeof(state), &state) == DI_OK,
          "read DirectInput state");
    return state;
}
struct Events {
    std::array<DIDEVICEOBJECTDATA, 32> data{};
    DWORD count = 32;
};
Events Buffered(IDirectInputDevice8W* device, DWORD flags = 0) {
    Events events;
    Check(device->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), events.data.data(),
                                &events.count, flags) == DI_OK,
          "read buffered transitions");
    return events;
}
bool Contains(const Events& events, DWORD offset, DWORD value) {
    for (DWORD i = 0; i < events.count; ++i)
        if (events.data[i].dwOfs == offset && events.data[i].dwData == value)
            return true;
    return false;
}
void Neutral(const DIJOYSTATE2& state) {
    Check(state.lX == 0 && state.lY == 0 && state.lZ == 0 && state.lRx == 0 &&
              state.lRy == 0,
          "bound axes neutral");
    Check(state.rgdwPOV[0] == 0xffffffffu, "bound POV centered");
    for (unsigned i = 0; i < 10; ++i)
        Check(state.rgbButtons[i] == 0, "bound button released");
}
} // namespace

void TdurObservationRecord(const char* event, DWORD, const wchar_t*,
                           const char*, const void*) noexcept {
    if (std::strcmp(event, "gamepad-service-stopped") == 0)
        SetEvent(service_stopped);
    if (std::strcmp(event, "gamepad-sdl-unavailable") == 0)
        ++service_failures;
}
int main() {
    using namespace tdur::mod::gamepad;
    const wchar_t* hints[] = {
        L"SDL_JOYSTICK_HIDAPI",    L"SDL_JOYSTICK_RAWINPUT",
        L"SDL_JOYSTICK_GAMEINPUT", L"SDL_JOYSTICK_DIRECTINPUT",
        L"SDL_JOYSTICK_WGI",       L"SDL_XINPUT_ENABLED"};
    for (const auto* hint : hints)
        SetEnvironmentVariableW(hint, L"0");
    SetEnvironmentVariableW(L"TDUR_GAMEPAD", L"sdl");
    service_stopped = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HMODULE system = nullptr;
    IDirectInput8W* factory = nullptr;
    IDirectInputDevice8W* device = nullptr;
    HWND window = nullptr;
    int result = 1;
    try {
        Check(service_stopped && changed, "private events");
        system = LoadLibraryExW(L"dinput8.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
        Check(system != nullptr, "load system DirectInput");
        using Create =
            HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
        const FARPROC symbol = GetProcAddress(system, "DirectInput8Create");
        Create create{};
        static_assert(sizeof(create) == sizeof(symbol));
        std::memcpy(&create, &symbol, sizeof(create));
        Check(create != nullptr, "system factory export");
        void* raw = nullptr;
        Check(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                     IID_IDirectInput8W, &raw, nullptr) == DI_OK,
              "create native factory");
        factory =
            static_cast<IDirectInput8W*>(WrapFactory(IID_IDirectInput8W, raw));
        Check(factory != raw && ReadService().ready,
              "factory wraps real SDL service");
        GUID instance{};
        Check(factory->EnumDevices(DI8DEVCLASS_GAMECTRL, FirstPad, &instance,
                                   DIEDFL_ATTACHEDONLY) == DI_OK,
              "enumerate synthetic device before attachment");
        Check(factory->CreateDevice(instance, &device, nullptr) == DI_OK,
              "create synthetic device");
        factory->Release();
        factory = nullptr; // Device keeps the real SDL service alive.
        window = CreateWindowExW(0, L"STATIC", L"Private input test", WS_POPUP,
                                 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "owned hidden window");
        Check(device->SetCooperativeLevel(
                  window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE) == DI_OK,
              "cooperative level without focus");
        Check(device->SetDataFormat(&c_dfDIJoystick2) == DI_OK,
              "SDK joystick format");
        DIPROPRANGE range{
            {sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), 0, DIPH_DEVICE},
            -1000,
            1000};
        Check(device->SetProperty(DIPROP_RANGE, &range.diph) == DI_OK,
              "explicit axis range");
        DIPROPDWORD buffer{
            {sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, 32};
        Check(device->SetProperty(DIPROP_BUFFERSIZE, &buffer.diph) == DI_OK,
              "bounded event buffer");
        Check(device->SetEventNotification(changed) == DI_POLLEDDEVICE,
              "explicit polled notification contract");
        Check(device->Acquire() == DI_OK, "acquire synthetic device");
        Neutral(State(device));
        Apply(Operation::attach);
        Await([] { return ReadService().sample.connected; },
              "virtual device reaches service");
        const auto first_token = ReadService().active_token;
        Check(first_token != 0, "first connection token");
        Neutral(State(device));
        (void)Buffered(device);
        ResetEvent(changed);
        Apply(Operation::press);
        Await(
            [] {
                const auto s = ReadService().sample;
                return s.connected && s.buttons[0] && s.axes[0] == 1.0f &&
                       s.axes[2] == 1.0f && s.axes[4] == -1.0f && s.pov == 4500;
            },
            "button axes triggers and diagonal reach service");
        Check(device->Poll() == DI_OK, "poll synthetic device");
        Check(WaitForSingleObject(changed, 0) == WAIT_OBJECT_0,
              "poll signals caller event");
        const auto pressed = State(device);
        Check(pressed.lX == 1000 && pressed.lZ == 1000 && pressed.lRy == -1000,
              "axes cross full transport");
        Check(pressed.rgbButtons[0] == 0x80 && pressed.rgdwPOV[0] == 4500,
              "button and POV cross full transport");
        const auto peek = Buffered(device, DIGDD_PEEK);
        Check(Contains(peek, DIJOFS_X, 1000) &&
                  Contains(peek, DIJOFS_Z, 1000) &&
                  Contains(peek, DIJOFS_RY, DWORD(LONG(-1000))) &&
                  Contains(peek, DIJOFS_BUTTON(0), 0x80) &&
                  Contains(peek, DIJOFS_POV(0), 4500),
              "buffer contains five physical transitions");
        const auto drain = Buffered(device);
        Check(peek.count == drain.count, "peek does not consume events");
        for (DWORD i = 0; i < drain.count; ++i) {
            Check(drain.data[i].dwOfs == peek.data[i].dwOfs &&
                      drain.data[i].dwData == peek.data[i].dwData &&
                      drain.data[i].dwSequence == peek.data[i].dwSequence,
                  "peek retains event identity");
            if (i)
                Check(drain.data[i].dwSequence > drain.data[i - 1].dwSequence,
                      "event sequence increases");
        }
        Check(Buffered(device).count == 0, "consumed buffer empty");
        Apply(Operation::detach);
        Await([] { return !ReadService().sample.connected; },
              "detach neutralizes service");
        Neutral(State(device));
        const auto release = Buffered(device);
        Check(Contains(release, DIJOFS_BUTTON(0), 0) &&
                  Contains(release, DIJOFS_POV(0), 0xffffffffu),
              "detach releases held button and POV");
        Apply(Operation::attach);
        Await(
            [&] {
                return ReadService().sample.connected &&
                       ReadService().active_token != first_token;
            },
            "reattach has a fresh token");
        Neutral(State(device));
        (void)Buffered(device);
        Apply(Operation::east);
        Await([] { return ReadService().sample.buttons[1]; },
              "new connection accepts input");
        Check(State(device).rgbButtons[1] == 0x80,
              "same DirectInput device serves reattached pad");
        Apply(Operation::neutral);
        Await([] { return !ReadService().sample.buttons[1]; },
              "release reaches service");
        Neutral(State(device));
        Check(service_failures == 0 && bad_owner == 0,
              "all injection on owner without SDL failure");
        result = 0;
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
    }
    if (virtual_id && ReadService().ready) {
        try {
            Apply(Operation::detach);
        } catch (...) {
            result = 1;
        }
    }
    if (device) {
        device->Unacquire();
        device->SetEventNotification(nullptr);
        device->Release();
    }
    if (factory)
        factory->Release();
    const bool stopped =
        WaitForSingleObject(service_stopped, 3000) == WAIT_OBJECT_0;
    if (!stopped) {
        std::puts("FAIL: service did not stop");
        result = 1;
    }
    if (window)
        DestroyWindow(window);
    if (system)
        FreeLibrary(system);
    CloseHandle(changed);
    CloseHandle(service_stopped);
    std::printf("checks=%u result=%d owner_errors=%u service_errors=%u\n",
                checks, result, bad_owner.load(), service_failures.load());
    return result;
}

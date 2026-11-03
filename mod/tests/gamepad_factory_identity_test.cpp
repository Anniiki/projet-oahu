// Independent COM identity witness. No game, device enumeration, input,
// window, real SDL service, or personal data is accessed.
#include "tdur/mod/gamepad_dinput.hpp"
#include "tdur/mod/gamepad_service.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include <cstdio>
#include <cstring>

namespace tdur::mod::gamepad {
bool ServiceRequested() noexcept { return true; }
bool AcquireService() noexcept { return true; }
void ReleaseService() noexcept {}
ServiceState ReadService() noexcept { return {}; }
bool OnServiceThread() noexcept { return false; }
} // namespace tdur::mod::gamepad
void TdurObservationRecord(const char*, DWORD, const wchar_t*, const char*,
                           const void*) noexcept {}

using CreateInput = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*,
                                     LPUNKNOWN);

template <class First, class Second>
bool check(CreateInput create, REFIID first_iid, REFIID second_iid,
           bool wrapped, const char* name) {
    void* raw = nullptr;
    auto hr = create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, first_iid,
                     &raw, nullptr);
    if (FAILED(hr) || !raw) {
        std::printf("%s creation failed %08lx\n", name,
                    static_cast<unsigned long>(hr));
        return false;
    }
    if (wrapped)
        raw = tdur::mod::gamepad::WrapFactory(first_iid, raw);
    auto* first = static_cast<First*>(raw);
    IUnknown* first_identity = nullptr;
    hr = first->QueryInterface(IID_IUnknown,
                               reinterpret_cast<void**>(&first_identity));
    Second* second = nullptr;
    const auto other_hr =
        first->QueryInterface(second_iid, reinterpret_cast<void**>(&second));
    IUnknown* second_identity = nullptr;
    HRESULT identity_hr = E_NOINTERFACE;
    if (second)
        identity_hr = second->QueryInterface(
            IID_IUnknown, reinterpret_cast<void**>(&second_identity));
    const bool same = SUCCEEDED(hr) && SUCCEEDED(other_hr) &&
                      SUCCEEDED(identity_hr) &&
                      first_identity == second_identity;
    std::printf("%s other_hr=%08lx same_iunknown=%d\n", name,
                static_cast<unsigned long>(other_hr), int(same));
    if (second_identity)
        second_identity->Release();
    if (second)
        second->Release();
    if (first_identity)
        first_identity->Release();
    first->Release();
    return same;
}

int main() {
    const HMODULE system =
        LoadLibraryExW(L"dinput8.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system)
        return 2;
    // memcpy avoids MSVC's incompatible-function-cast warning.
    const FARPROC symbol = GetProcAddress(system, "DirectInput8Create");
    CreateInput create{};
    static_assert(sizeof(create) == sizeof(symbol));
    std::memcpy(&create, &symbol, sizeof(create));
    if (!create) {
        FreeLibrary(system);
        return 2;
    }
    const bool native_a = check<IDirectInput8A, IDirectInput8W>(
        create, IID_IDirectInput8A, IID_IDirectInput8W, false, "native_A_W");
    const bool native_w = check<IDirectInput8W, IDirectInput8A>(
        create, IID_IDirectInput8W, IID_IDirectInput8A, false, "native_W_A");
    const bool wrapped_a = check<IDirectInput8A, IDirectInput8W>(
        create, IID_IDirectInput8A, IID_IDirectInput8W, true, "wrapped_A_W");
    const bool wrapped_w = check<IDirectInput8W, IDirectInput8A>(
        create, IID_IDirectInput8W, IID_IDirectInput8A, true, "wrapped_W_A");
    FreeLibrary(system);
    if (!native_a || !native_w)
        return 2;
    return wrapped_a && wrapped_w ? 0 : 1;
}

// Independent COM contract witness using only a fake A/W factory and service.
// No native factory creation, SDL, physical device, input, or window access.
#include "tdur/mod/gamepad_dinput.hpp"
#include "tdur/mod/gamepad_service.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

int checks = 0;
int failures = 0;
void Expect(bool result, const char* label) {
    ++checks;
    if (!result) {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
}

struct ServiceCounters {
    bool requested = true;
    bool on_thread = false;
    bool available = true;
    int attempts = 0;
    int acquired = 0;
    int released = 0;
    int active = 0;
} service;
tdur::mod::gamepad::ServiceState service_state;

constexpr IID ExtraInterface{0x231ea4bc,
                             0x146e,
                             0x493e,
                             {0xa4, 0x25, 0x1b, 0x1a, 0x54, 0xaf, 0x1f, 0x55}};

template <class T> class Ref {
  public:
    explicit Ref(T* value = nullptr) noexcept : value_(value) {}
    ~Ref() { Reset(); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    T* Get() const noexcept { return value_; }
    T* operator->() const noexcept { return value_; }
    T** Put() noexcept {
        Reset();
        return &value_;
    }
    void Reset() noexcept {
        if (value_ != nullptr) {
            value_->Release();
            value_ = nullptr;
        }
    }

  private:
    T* value_;
};

template <class T> bool Query(IUnknown* source, REFIID iid, Ref<T>& output) {
    const HRESULT result =
        source->QueryInterface(iid, reinterpret_cast<void**>(output.Put()));
    Expect(result == S_OK && output.Get() != nullptr, "QI succeeds");
    return result == S_OK && output.Get() != nullptr;
}

struct NativeCounters {
    ULONG references = 1;
    int destroyed = 0;
    int queries = 0;
    int enum_a = 0;
    int enum_w = 0;
    int create_a = 0;
    int create_w = 0;
    HRESULT ansi_result = S_OK;
    HRESULT wide_result = S_OK;
    bool null_success = false;
};

// Multiple inheritance intentionally gives the fake native object two
// different interface addresses but a single canonical IUnknown.
class NativeFactory final : public IDirectInput8A, public IDirectInput8W {
  public:
    explicit NativeFactory(NativeCounters& counters) : counters_(counters) {}
    STDMETHODIMP QueryInterface(REFIID iid, LPVOID* output) override {
        if (output == nullptr) {
            return E_POINTER;
        }
        *output = nullptr;
        ++counters_.queries;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, ExtraInterface)) {
            *output =
                counters_.ansi_result == E_NOINTERFACE
                    ? static_cast<IUnknown*>(static_cast<IDirectInput8W*>(this))
                    : static_cast<IUnknown*>(
                          static_cast<IDirectInput8A*>(this));
        } else if (IsEqualIID(iid, IID_IDirectInput8A)) {
            if (counters_.ansi_result != S_OK || counters_.null_success) {
                return counters_.ansi_result;
            }
            *output = static_cast<IDirectInput8A*>(this);
        } else if (IsEqualIID(iid, IID_IDirectInput8W)) {
            if (counters_.wide_result != S_OK || counters_.null_success) {
                return counters_.wide_result;
            }
            *output = static_cast<IDirectInput8W*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++counters_.references; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --counters_.references;
        if (remaining == 0) {
            ++counters_.destroyed;
            delete this;
        }
        return remaining;
    }
    STDMETHODIMP CreateDevice(REFGUID, IDirectInputDevice8A** output,
                              LPUNKNOWN) override {
        ++counters_.create_a;
        if (output != nullptr) {
            *output = nullptr;
        }
        return DIERR_DEVICENOTREG;
    }
    STDMETHODIMP CreateDevice(REFGUID, IDirectInputDevice8W** output,
                              LPUNKNOWN) override {
        ++counters_.create_w;
        if (output != nullptr) {
            *output = nullptr;
        }
        return DIERR_DEVICENOTREG;
    }
    STDMETHODIMP EnumDevices(DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID,
                             DWORD) override {
        ++counters_.enum_a;
        return DI_OK;
    }
    STDMETHODIMP EnumDevices(DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID,
                             DWORD) override {
        ++counters_.enum_w;
        return DI_OK;
    }
    STDMETHODIMP GetDeviceStatus(REFGUID) override {
        return DIERR_DEVICENOTREG;
    }
    STDMETHODIMP RunControlPanel(HWND, DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP Initialize(HINSTANCE, DWORD) override { return S_FALSE; }
    STDMETHODIMP FindDevice(REFGUID, LPCSTR, LPGUID) override {
        return DIERR_DEVICENOTREG;
    }
    STDMETHODIMP FindDevice(REFGUID, LPCWSTR, LPGUID) override {
        return DIERR_DEVICENOTREG;
    }
    STDMETHODIMP EnumDevicesBySemantics(LPCSTR, LPDIACTIONFORMATA,
                                        LPDIENUMDEVICESBYSEMANTICSCBA, LPVOID,
                                        DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP EnumDevicesBySemantics(LPCWSTR, LPDIACTIONFORMATW,
                                        LPDIENUMDEVICESBYSEMANTICSCBW, LPVOID,
                                        DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,
                                  LPDICONFIGUREDEVICESPARAMSA, DWORD,
                                  LPVOID) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK,
                                  LPDICONFIGUREDEVICESPARAMSW, DWORD,
                                  LPVOID) override {
        return DIERR_UNSUPPORTED;
    }

  private:
    ~NativeFactory() = default;
    NativeCounters& counters_;
};

struct Capture {
    int count = 0;
    GUID instance{};
    GUID product{};
    DWORD type = 0;
};

template <class Instance> BOOL Collect(const Instance* info, LPVOID context) {
    auto& capture = *static_cast<Capture*>(context);
    ++capture.count;
    capture.instance = info->guidInstance;
    capture.product = info->guidProduct;
    capture.type = info->dwDevType;
    return DIENUM_CONTINUE;
}
BOOL CALLBACK CollectA(const DIDEVICEINSTANCEA* info, LPVOID context) {
    return Collect(info, context);
}
BOOL CALLBACK CollectW(const DIDEVICEINSTANCEW* info, LPVOID context) {
    return Collect(info, context);
}

template <class Factory> struct Traits;
template <> struct Traits<IDirectInput8A> {
    using Device = IDirectInputDevice8A;
    using Instance = DIDEVICEINSTANCEA;
    static const IID& Iid() { return IID_IDirectInput8A; }
    static constexpr auto Callback = &CollectA;
    static bool Name(const Instance& value) {
        return std::strcmp(value.tszProductName,
                           "Controller (XBOX 360 For Windows)") == 0;
    }
};
template <> struct Traits<IDirectInput8W> {
    using Device = IDirectInputDevice8W;
    using Instance = DIDEVICEINSTANCEW;
    static const IID& Iid() { return IID_IDirectInput8W; }
    static constexpr auto Callback = &CollectW;
    static bool Name(const Instance& value) {
        return std::wcscmp(value.tszProductName,
                           L"Controller (XBOX 360 For Windows)") == 0;
    }
};

template <class Factory>
void ObserveFactory(Factory* factory, Capture& capture,
                    Ref<typename Traits<Factory>::Device>& device) {
    using T = Traits<Factory>;
    Expect(factory->EnumDevices(DI8DEVCLASS_GAMECTRL, T::Callback, &capture,
                                DIEDFL_ATTACHEDONLY) == DI_OK,
           "enumeration succeeds");
    Expect(capture.count == 1, "one synthetic device without native devices");
    Expect(factory->GetDeviceStatus(capture.instance) == DI_OK,
           "synthetic device status");
    Expect(factory->CreateDevice(capture.instance, device.Put(), nullptr) ==
                   DI_OK &&
               device.Get() != nullptr,
           "create synthetic device through each facade");
    if (device.Get() != nullptr) {
        typename T::Instance info{};
        info.dwSize = sizeof(info);
        Expect(device->GetDeviceInfo(&info) == DI_OK,
               "synthetic device info succeeds");
        Expect(IsEqualGUID(info.guidInstance, capture.instance) &&
                   IsEqualGUID(info.guidProduct, capture.product) &&
                   info.dwDevType == capture.type && T::Name(info),
               "created device matches enumerated identity and product");
    }
}

void RefuseExtra(IUnknown* source) {
    void* output = reinterpret_cast<void*>(std::uintptr_t{1});
    const HRESULT result = source->QueryInterface(ExtraInterface, &output);
    Expect(result == E_NOINTERFACE, "unwrapped native-only IID refused");
    Expect(output == nullptr, "refused QI clears output");
    if (SUCCEEDED(result) && output != nullptr &&
        output != reinterpret_cast<void*>(std::uintptr_t{1})) {
        // The fake extra IID has an IUnknown vtable. Keep the red baseline's
        // lifetime balanced too when its unwrapped QI unexpectedly succeeds.
        static_cast<IUnknown*>(output)->Release();
    }
    Expect(source->QueryInterface(IID_IUnknown, nullptr) == E_POINTER,
           "null QI output refused");
}

template <class First, class Second> void RoundTrip(const char* name) {
    std::printf("scenario: %s\n", name);
    service = {};
    NativeCounters native;
    auto* object = new NativeFactory(native);
    auto* raw = static_cast<First*>(object);
    Ref<First> first(static_cast<First*>(
        tdur::mod::gamepad::WrapFactory(Traits<First>::Iid(), raw)));
    Expect(first.Get() != raw, "native factory is wrapped");
    Expect(native.references == 2, "wrapper owns two native references");
    Expect(service.active == 1 && service.acquired == 1,
           "both facades share one service lease");

    Ref<First> reflexive;
    Ref<Second> second;
    Ref<Second> second_self;
    Ref<First> roundtrip;
    Ref<IUnknown> first_identity;
    Ref<IUnknown> second_identity;
    Ref<IUnknown> identity_self;
    Ref<First> from_identity_first;
    Ref<Second> from_identity_second;
    if (!Query(first.Get(), Traits<First>::Iid(), reflexive) ||
        !Query(first.Get(), Traits<Second>::Iid(), second) ||
        !Query(second.Get(), Traits<Second>::Iid(), second_self) ||
        !Query(second.Get(), Traits<First>::Iid(), roundtrip) ||
        !Query(first.Get(), IID_IUnknown, first_identity) ||
        !Query(second.Get(), IID_IUnknown, second_identity) ||
        !Query(first_identity.Get(), IID_IUnknown, identity_self) ||
        !Query(first_identity.Get(), Traits<First>::Iid(),
               from_identity_first) ||
        !Query(first_identity.Get(), Traits<Second>::Iid(),
               from_identity_second)) {
        return;
    }
    Expect(first.Get() == reflexive.Get() && first.Get() == roundtrip.Get() &&
               first.Get() == from_identity_first.Get(),
           "first interface is reflexive and stable after roundtrip");
    Expect(second.Get() == second_self.Get() &&
               second.Get() == from_identity_second.Get(),
           "second interface is reflexive and reachable from IUnknown");
    Expect(first_identity.Get() == second_identity.Get() &&
               first_identity.Get() == identity_self.Get(),
           "QI IUnknown is canonical, symmetric and transitive");
    Ref<IDirectInput8A> canonical_ansi;
    if (Query(first_identity.Get(), IID_IDirectInput8A, canonical_ansi)) {
        Expect(first_identity.Get() ==
                   static_cast<IUnknown*>(canonical_ansi.Get()),
               "IUnknown is the A facade even when entry was W");
    }
    canonical_ansi.Reset();
    RefuseExtra(first.Get());
    RefuseExtra(second.Get());
    RefuseExtra(first_identity.Get());
    Expect(native.queries == 1, "public QI never reaches native factory");
    Expect(native.references == 2 && service.active == 1,
           "public references do not duplicate native refs or leases");

    Capture first_capture;
    Capture second_capture;
    Ref<typename Traits<First>::Device> first_device;
    Ref<typename Traits<Second>::Device> second_device;
    ObserveFactory(first.Get(), first_capture, first_device);
    ObserveFactory(second.Get(), second_capture, second_device);
    Expect(IsEqualGUID(first_capture.instance, second_capture.instance) &&
               IsEqualGUID(first_capture.product, second_capture.product) &&
               first_capture.type == second_capture.type,
           "A/W enumerate the same synthetic pad identity");
    Expect(native.enum_a == 1 && native.enum_w == 1,
           "A/W forward remaining enumeration to matching native interface");
    Expect(native.create_a == 0 && native.create_w == 0,
           "synthetic creation does not reach native device creation");
    Expect(service.active == 3, "factory and devices own independent leases");

    first.Reset();
    reflexive.Reset();
    second_self.Reset();
    roundtrip.Reset();
    first_identity.Reset();
    second_identity.Reset();
    identity_self.Reset();
    from_identity_first.Reset();
    from_identity_second.Reset();
    Expect(native.destroyed == 0 && native.references == 2,
           "opposite facade survives release of every initial facade ref");
    if (Query(second.Get(), Traits<First>::Iid(), roundtrip)) {
        Expect(roundtrip.Get() != raw, "late roundtrip remains wrapped");
    }
    roundtrip.Reset();
    second.Reset();
    Expect(native.references == 0 && native.destroyed == 1,
           "last factory reference releases both native refs exactly once");
    Expect(service.active == 2 && service.released == 1,
           "last factory release keeps independent devices alive");
    if (first_device.Get() != nullptr) {
        typename Traits<First>::Instance info{};
        info.dwSize = sizeof(info);
        Expect(first_device->GetDeviceInfo(&info) == DI_OK,
               "synthetic device survives factory destruction");
    }
    first_device.Reset();
    second_device.Reset();
    Expect(service.active == 0 && service.acquired == service.released,
           "all service leases balanced at scenario end");
}

template <class First, class Second> void OneInterfaceOnly() {
    service = {};
    NativeCounters native;
    if (IsEqualIID(Traits<Second>::Iid(), IID_IDirectInput8A)) {
        native.ansi_result = E_NOINTERFACE;
    } else {
        native.wide_result = E_NOINTERFACE;
    }
    auto* raw = static_cast<First*>(new NativeFactory(native));
    Ref<First> wrapped(static_cast<First*>(
        tdur::mod::gamepad::WrapFactory(Traits<First>::Iid(), raw)));
    Expect(wrapped.Get() != raw, "single-interface factory still wrapped");
    Ref<IUnknown> identity;
    Ref<First> from_identity;
    if (Query(wrapped.Get(), IID_IUnknown, identity) &&
        Query(identity.Get(), Traits<First>::Iid(), from_identity)) {
        Expect(from_identity.Get() == wrapped.Get(),
               "single-interface canonical identity roundtrip");
    }
    void* output = reinterpret_cast<void*>(std::uintptr_t{1});
    Expect(wrapped->QueryInterface(Traits<Second>::Iid(), &output) ==
                   E_NOINTERFACE &&
               output == nullptr,
           "native unsupported character variant stays unsupported");
    Capture capture;
    Ref<typename Traits<First>::Device> device;
    ObserveFactory(wrapped.Get(), capture, device);
    device.Reset();
    from_identity.Reset();
    identity.Reset();
    wrapped.Reset();
    Expect(native.references == 0 && native.destroyed == 1,
           "single-interface native ownership balanced");
    Expect(service.active == 0 && service.acquired == service.released,
           "single-interface service ownership balanced");
}

template <class First, class Second> void QueryFailure(bool null_success) {
    service = {};
    NativeCounters native;
    native.null_success = null_success;
    if (!null_success) {
        if (IsEqualIID(Traits<Second>::Iid(), IID_IDirectInput8A)) {
            native.ansi_result = E_FAIL;
        } else {
            native.wide_result = E_FAIL;
        }
    }
    auto* raw = static_cast<First*>(new NativeFactory(native));
    auto* result = tdur::mod::gamepad::WrapFactory(Traits<First>::Iid(), raw);
    Expect(result == raw && native.references == 1,
           "failed opposite QI preserves caller's native reference");
    Expect(service.acquired == 1 && service.released == 1 &&
               service.active == 0,
           "failed wrapping releases its service lease");
    static_cast<First*>(result)->Release();
    Expect(native.destroyed == 1 && native.references == 0,
           "failed wrapping remains releasable exactly once");
}

void BypassGuards() {
    for (int mode = 0; mode != 3; ++mode) {
        service = {};
        service.available = mode != 0;
        service.requested = mode != 1;
        service.on_thread = mode == 2;
        NativeCounters native;
        auto* raw = static_cast<IDirectInput8A*>(new NativeFactory(native));
        void* result = tdur::mod::gamepad::WrapFactory(IID_IDirectInput8A, raw);
        Expect(result == raw && native.references == 1 && native.queries == 0,
               "service guards preserve untouched native reference");
        Expect(service.active == 0 && service.released == 0 &&
                   service.attempts == (mode == 0 ? 1 : 0),
               "service guards do not leak or release an unowned lease");
        static_cast<IDirectInput8A*>(result)->Release();
        Expect(native.destroyed == 1, "guarded native object remains owned");
    }
    service = {};
    Expect(tdur::mod::gamepad::WrapFactory(IID_IDirectInput8A, nullptr) ==
                   nullptr &&
               service.attempts == 0,
           "null factory does not acquire service");
}

// The call sequence and [-1000,+1000] per-axis range are supported by the
// SECONDARY f452... binary only (4354F0, 435C80, 4365F0). This is a COM
// witness, not a validation of MC1.66A or of its gameplay/controller binding.
// No HWND exists here: BACKGROUND|NONEXCLUSIVE deliberately replaces native
// FOREGROUND|EXCLUSIVE. This test does not validate native flags 5 or focus.
struct SecondaryObjectCapture {
    IDirectInputDevice8A* device{};
    std::array<DWORD, 5> axis_ids{};
    std::array<bool, 5> axis_seen{};
    int axes{};
    int buttons{};
    int povs{};
};

BOOL CALLBACK ConfigureSecondaryObject(const DIDEVICEOBJECTINSTANCEA* info,
                                       LPVOID context) {
    auto& capture = *static_cast<SecondaryObjectCapture*>(context);
    Expect(info != nullptr && info->dwSize == sizeof(*info),
           "secondary enumeration supplies full ANSI object info");
    if (info == nullptr) {
        return DIENUM_STOP;
    }
    if ((info->dwType & DIDFT_AXIS) != 0) {
        const std::array<const GUID*, 5> guids{
            &GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis};
        const std::array<DWORD, 5> offsets{DIJOFS_X, DIJOFS_Y, DIJOFS_Z,
                                           DIJOFS_RX, DIJOFS_RY};
        std::size_t slot = guids.size();
        for (std::size_t i = 0; i < guids.size(); ++i) {
            if (IsEqualGUID(info->guidType, *guids[i])) {
                slot = i;
                break;
            }
        }
        Expect(slot < guids.size(), "secondary enumeration axis GUID");
        if (slot == guids.size()) {
            return DIENUM_STOP;
        }
        Expect(!capture.axis_seen[slot], "secondary axis enumerated once");
        Expect(info->dwOfs == offsets[slot],
               "secondary axis offset follows actual SDK format");
        Expect((info->dwFlags & DIDOI_ASPECTMASK) == DIDOI_ASPECTPOSITION,
               "secondary axes expose position aspect only");
        for (std::size_t i = 0; i < capture.axis_seen.size(); ++i) {
            if (capture.axis_seen[i]) {
                Expect(capture.axis_ids[i] != info->dwType,
                       "secondary per-axis identifiers are distinct");
            }
        }
        capture.axis_ids[slot] = info->dwType;
        capture.axis_seen[slot] = true;
        ++capture.axes;

        DIPROPRANGE range{};
        range.diph = {sizeof(range), sizeof(DIPROPHEADER), info->dwType,
                      DIPH_BYID};
        Expect(capture.device->GetProperty(DIPROP_RANGE, &range.diph) ==
                       DI_OK &&
                   range.lMin == 0 && range.lMax == 65535,
               "range BYID leaves each not-yet-configured axis unchanged");
        range.lMin = -1000;
        range.lMax = 1000;
        // 43616B..4361BA uses the enumerated dwType, DIPH_BYID and this
        // range. The game ignores HRESULT; this witness checks it.
        Expect(capture.device->SetProperty(DIPROP_RANGE, &range.diph) == DI_OK,
               "secondary range BYID succeeds from EnumObjects callback");
        range.lMin = 0;
        range.lMax = 0;
        Expect(capture.device->GetProperty(DIPROP_RANGE, &range.diph) ==
                       DI_OK &&
                   range.lMin == -1000 && range.lMax == 1000,
               "secondary range is stored on enumerated axis");
    } else if ((info->dwType & DIDFT_BUTTON) != 0) {
        const DWORD instance = DIDFT_GETINSTANCE(info->dwType);
        ++capture.buttons;
        Expect(instance < 10 && info->dwOfs == DIJOFS_BUTTON(instance),
               "secondary buttons use SDK button offsets");
    } else if ((info->dwType & DIDFT_POV) != 0) {
        ++capture.povs;
        Expect(IsEqualGUID(info->guidType, GUID_POV) &&
                   info->dwOfs == DIJOFS_POV(0),
               "secondary POV uses first SDK POV offset");
    } else {
        Expect(false, "secondary enumeration has no unexpected object class");
    }
    return DIENUM_CONTINUE;
}

void SecondaryJoystickStateWithoutWindow() {
    using tdur::mod::gamepad::WrapFactory;
    std::printf("scenario: secondary joystick transport, background mock\n");
    service = {};
    service_state = {};
    service_state.ready = true;
    service_state.sample.connected = true;
    service_state.sample.axes = {-1.0f, 1.0f, 0.0f, 0.5f, -0.5f};
    service_state.sample.buttons[0] = true;
    service_state.sample.buttons[3] = true;
    service_state.sample.buttons[9] = true;
    service_state.sample.pov = 4500;
    service_state.generation = 1;

    NativeCounters native;
    auto* raw = static_cast<IDirectInput8A*>(new NativeFactory(native));
    Ref<IDirectInput8A> factory(
        static_cast<IDirectInput8A*>(WrapFactory(IID_IDirectInput8A, raw)));
    Expect(factory.Get() != raw, "secondary ANSI factory is wrapped");
    Capture devices;
    Expect(factory->EnumDevices(DI8DEVCLASS_GAMECTRL, CollectA, &devices, 0) ==
                   DI_OK &&
               devices.count == 1,
           "secondary enumeration uses game-controller class and flags zero");
    Ref<IDirectInputDevice8A> device;
    Expect(factory->CreateDevice(devices.instance, device.Put(), nullptr) ==
                   DI_OK &&
               device.Get() != nullptr,
           "secondary ANSI factory creates synthetic device");
    if (device.Get() == nullptr) {
        return;
    }

    static_assert(sizeof(DIJOYSTATE2) == 272);
    Expect(c_dfDIJoystick2.dwDataSize == sizeof(DIJOYSTATE2) &&
               c_dfDIJoystick2.dwNumObjs == 164 &&
               c_dfDIJoystick2.dwFlags == DIDF_ABSAXIS,
           "real SDK format has observed secondary size/count/axis mode");
    Expect(device->SetDataFormat(&c_dfDIJoystick2) == DI_OK,
           "secondary-sized real SDK format accepted through COM device");
    DIDEVCAPS caps{};
    caps.dwSize = sizeof(caps);
    Expect(device->GetCapabilities(&caps) == DI_OK && caps.dwAxes == 5 &&
               caps.dwButtons == 10 && caps.dwPOVs == 1 &&
               (caps.dwFlags & DIDC_ATTACHED) != 0 &&
               (caps.dwFlags & DIDC_FORCEFEEDBACK) == 0,
           "secondary mock capabilities match enumerated supported objects");

    SecondaryObjectCapture objects;
    objects.device = device.Get();
    Expect(device->EnumObjects(ConfigureSecondaryObject, &objects, 0) == DI_OK,
           "secondary EnumObjects and per-axis property callbacks complete");
    Expect(objects.axes == 5 && objects.buttons == 10 && objects.povs == 1 &&
               std::all_of(objects.axis_seen.begin(), objects.axis_seen.end(),
                           [](bool seen) { return seen; }),
           "secondary format binds five axes, ten buttons and one POV");
    for (DWORD id : objects.axis_ids) {
        DIPROPRANGE range{};
        range.diph = {sizeof(range), sizeof(DIPROPHEADER), id, DIPH_BYID};
        Expect(device->GetProperty(DIPROP_RANGE, &range.diph) == DI_OK &&
                   range.lMin == -1000 && range.lMax == 1000,
               "all per-object ranges persist after complete enumeration");
    }

    // No real window validation call is reached for either null HWND.
    Expect(device->SetCooperativeLevel(nullptr,
                                       DISCL_EXCLUSIVE | DISCL_FOREGROUND) ==
               DIERR_INVALIDPARAM,
           "native foreground flags need a window, deliberately absent");
    Expect(device->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE |
                                                    DISCL_BACKGROUND) == DI_OK,
           "background-only mock does not access any window");
    Expect(device->Acquire() == DI_OK, "secondary mock acquires");
    Expect(device->Poll() == DI_OK, "secondary mock polls");

    struct GuardedState {
        std::array<unsigned char, 16> before;
        DIJOYSTATE2 state;
        std::array<unsigned char, 16> after;
    } guarded;
    static_assert(offsetof(GuardedState, state) == 16);
    static_assert(offsetof(GuardedState, after) == 16 + sizeof(DIJOYSTATE2));
    static_assert(sizeof(GuardedState) == 32 + sizeof(DIJOYSTATE2));
    std::memset(&guarded, 0xA5, sizeof(guarded));
    GuardedState unchanged = guarded;
    Expect(device->GetDeviceState(sizeof(guarded.state) - 1, &guarded.state) ==
                   DIERR_INVALIDPARAM &&
               std::memcmp(&guarded, &unchanged, sizeof(guarded)) == 0,
           "wrong state size preserves every destination and sentinel byte");
    Expect(device->GetDeviceState(sizeof(guarded.state), &guarded.state) ==
               DI_OK,
           "secondary mock reads full 272-byte state");
    const auto sentinel = [](unsigned char value) { return value == 0xA5; };
    Expect(
        std::all_of(guarded.before.begin(), guarded.before.end(), sentinel) &&
            std::all_of(guarded.after.begin(), guarded.after.end(), sentinel),
        "272-byte state read preserves both adjacent memory sentinels");

    DIJOYSTATE2 expected{};
    expected.lX = -1000;
    expected.lY = 1000;
    expected.lZ = 0;
    expected.lRx = 500;
    expected.lRy = -500;
    expected.rgdwPOV[0] = 4500;
    expected.rgbButtons[0] = 0x80;
    expected.rgbButtons[3] = 0x80;
    expected.rgbButtons[9] = 0x80;
    // Unbound fields are zero by this proxy's policy, not a claim about
    // unavailable native objects. This also checks velocity/force tails.
    Expect(std::memcmp(&guarded.state, &expected, sizeof(expected)) == 0,
           "secondary state has exact axes, buttons, POV and zero unused tail");
    const auto normalized = [](LONG value, float expected_value) {
        return std::abs(value * 0.001f - expected_value) < 0.000001f;
    };
    Expect(normalized(guarded.state.lX, -1.0f) &&
               normalized(guarded.state.lY, 1.0f) &&
               normalized(guarded.state.lZ, 0.0f) &&
               normalized(guarded.state.lRx, 0.5f) &&
               normalized(guarded.state.lRy, -0.5f),
           "secondary 0.001 scale maps native range to normalized values");

    Expect(device->Unacquire() == DI_OK, "secondary mock unacquires");
    unchanged = guarded;
    Expect(device->GetDeviceState(sizeof(guarded.state), &guarded.state) ==
                   DIERR_NOTACQUIRED &&
               std::memcmp(&guarded, &unchanged, sizeof(guarded)) == 0,
           "unacquired read preserves state and sentinels");
    Expect(native.create_a == 0 && native.create_w == 0,
           "secondary witness never creates any native device");
    device.Reset();
    factory.Reset();
    Expect(native.references == 0 && native.destroyed == 1 &&
               service.active == 0 && service.acquired == service.released,
           "secondary scenario releases all fake factory and service owners");
    service_state = {};
}

} // namespace

namespace tdur::mod::gamepad {
bool ServiceRequested() noexcept { return service.requested; }
bool AcquireService() noexcept {
    ++service.attempts;
    if (!service.available) {
        return false;
    }
    ++service.acquired;
    ++service.active;
    return true;
}
void ReleaseService() noexcept {
    ++service.released;
    --service.active;
}
ServiceState ReadService() noexcept { return service_state; }
bool OnServiceThread() noexcept { return service.on_thread; }
} // namespace tdur::mod::gamepad

void TdurObservationRecord(const char*, DWORD, const wchar_t*, const char*,
                           const void*) noexcept {}

int main() {
    RoundTrip<IDirectInput8A, IDirectInput8W>("A -> W -> A");
    RoundTrip<IDirectInput8W, IDirectInput8A>("W -> A -> W");
    OneInterfaceOnly<IDirectInput8A, IDirectInput8W>();
    OneInterfaceOnly<IDirectInput8W, IDirectInput8A>();
    QueryFailure<IDirectInput8A, IDirectInput8W>(false);
    QueryFailure<IDirectInput8W, IDirectInput8A>(false);
    QueryFailure<IDirectInput8A, IDirectInput8W>(true);
    QueryFailure<IDirectInput8W, IDirectInput8A>(true);
    BypassGuards();
    SecondaryJoystickStateWithoutWindow();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

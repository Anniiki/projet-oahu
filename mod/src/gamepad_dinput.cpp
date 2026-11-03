// In-process DirectInput 8 integration of the SDL gamepad service.
// Every interface derives from the SDK C++ declarations, so the vtable order
// and STDMETHODCALLTYPE ABI are those of dinput.h. System objects that are
// not the synthetic pad are returned unwrapped and unchanged.
#define DIRECTINPUT_VERSION 0x0800
#include "tdur/mod/gamepad_dinput.hpp"

#include "tdur/mod/gamepad_dinput_format.hpp"
#include "tdur/mod/gamepad_service.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <dinput.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace tdur::mod::gamepad {
namespace {

// Mod-owned instance identity {6A3D1C70-5B2E-4F1A-9C77-0E1D2C3B4A50}.
constexpr GUID SyntheticInstance{
    0x6a3d1c70,
    0x5b2e,
    0x4f1a,
    {0x9c, 0x77, 0x0e, 0x1d, 0x2c, 0x3b, 0x4a, 0x50}};
// Compatibility policy: public 'PIDVID' identity of the Xbox 360 HID pad
// (045E:028E), the PC controller a DirectInput game of 2006 encounters.
constexpr WORD SyntheticVendor = 0x045E;
constexpr WORD SyntheticProductId = 0x028E;
constexpr GUID SyntheticProduct{DWORD(SyntheticVendor) |
                                    (DWORD(SyntheticProductId) << 16),
                                0x0000,
                                0x0000,
                                {0x00, 0x00, 'P', 'I', 'D', 'V', 'I', 'D'}};
constexpr wchar_t ProductName[] = L"Controller (XBOX 360 For Windows)";
constexpr DWORD SyntheticDevType =
    DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
constexpr DWORD MaxBufferedEvents = 1024; // Modern bound.

struct Ansi {
    using Factory = IDirectInput8A;
    using Device = IDirectInputDevice8A;
    using DeviceInstance = DIDEVICEINSTANCEA;
    using DeviceInstanceDx3 = DIDEVICEINSTANCE_DX3A;
    using ObjectInstance = DIDEVICEOBJECTINSTANCEA;
    using ObjectInstanceDx3 = DIDEVICEOBJECTINSTANCE_DX3A;
    using EnumDevicesCallback = LPDIENUMDEVICESCALLBACKA;
    using EnumObjectsCallback = LPDIENUMDEVICEOBJECTSCALLBACKA;
    using EnumEffectsCallback = LPDIENUMEFFECTSCALLBACKA;
    using SemanticsCallback = LPDIENUMDEVICESBYSEMANTICSCBA;
    using EffectInfo = DIEFFECTINFOA;
    using ActionFormat = DIACTIONFORMATA;
    using ImageInfoHeader = DIDEVICEIMAGEINFOHEADERA;
    using ConfigureParams = DICONFIGUREDEVICESPARAMSA;
    using Text = LPCSTR;
    static const IID& FactoryIid() noexcept { return IID_IDirectInput8A; }
    static const IID& DeviceIid() noexcept { return IID_IDirectInputDevice8A; }
};
struct Wide {
    using Factory = IDirectInput8W;
    using Device = IDirectInputDevice8W;
    using DeviceInstance = DIDEVICEINSTANCEW;
    using DeviceInstanceDx3 = DIDEVICEINSTANCE_DX3W;
    using ObjectInstance = DIDEVICEOBJECTINSTANCEW;
    using ObjectInstanceDx3 = DIDEVICEOBJECTINSTANCE_DX3W;
    using EnumDevicesCallback = LPDIENUMDEVICESCALLBACKW;
    using EnumObjectsCallback = LPDIENUMDEVICEOBJECTSCALLBACKW;
    using EnumEffectsCallback = LPDIENUMEFFECTSCALLBACKW;
    using SemanticsCallback = LPDIENUMDEVICESBYSEMANTICSCBW;
    using EffectInfo = DIEFFECTINFOW;
    using ActionFormat = DIACTIONFORMATW;
    using ImageInfoHeader = DIDEVICEIMAGEINFOHEADERW;
    using ConfigureParams = DICONFIGUREDEVICESPARAMSW;
    using Text = LPCWSTR;
    static const IID& FactoryIid() noexcept { return IID_IDirectInput8W; }
    static const IID& DeviceIid() noexcept { return IID_IDirectInputDevice8W; }
};

// Layout names and the product name are ASCII.
void CopyName(char (&target)[MAX_PATH], const wchar_t* source) noexcept {
    std::size_t i = 0;
    for (; source[i] != L'\0' && i + 1 < MAX_PATH; ++i) {
        target[i] = char(source[i]);
    }
    target[i] = '\0';
}
void CopyName(wchar_t (&target)[MAX_PATH], const wchar_t* source) noexcept {
    std::size_t i = 0;
    for (; source[i] != L'\0' && i + 1 < MAX_PATH; ++i) {
        target[i] = source[i];
    }
    target[i] = L'\0';
}

// DIPROP_* are MAKEDIPROP(n): the GUID reference is the integer n itself.
std::uintptr_t PropertyId(REFGUID property) noexcept {
    return reinterpret_cast<std::uintptr_t>(&property);
}

template <class T> void FillInstance(typename T::DeviceInstance& out) {
    const DWORD size = sizeof(out);
    std::memset(&out, 0, sizeof(out));
    out.dwSize = size;
    out.guidInstance = SyntheticInstance;
    out.guidProduct = SyntheticProduct;
    out.dwDevType = SyntheticDevType;
    CopyName(out.tszInstanceName, ProductName);
    CopyName(out.tszProductName, ProductName);
    out.wUsagePage = 0x01; // HID Generic Desktop
    out.wUsage = 0x05;     // Game Pad
}

template <class Instance>
bool IsHidden(const Instance& instance, const ServiceState& state) noexcept {
    const DWORD type = GET_DIDEVICE_TYPE(instance.dwDevType);
    if (type < DI8DEVTYPE_JOYSTICK || type > DI8DEVTYPE_1STPERSON) {
        return false;
    }
    const auto& product = instance.guidProduct;
    constexpr unsigned char tail[8] = {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'};
    if (product.Data2 != 0 || product.Data3 != 0 ||
        std::memcmp(product.Data4, tail, sizeof(tail)) != 0) {
        return false;
    }
    for (std::size_t i = 0; i < state.hidden_count; ++i) {
        if (state.hidden_products[i] == product.Data1) {
            return true;
        }
    }
    return false;
}

class Lock {
  public:
    explicit Lock(SRWLOCK& lock) noexcept : lock_(lock) {
        AcquireSRWLockExclusive(&lock_);
    }
    ~Lock() { ReleaseSRWLockExclusive(&lock_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

  private:
    SRWLOCK& lock_;
};

template <class T> class SyntheticDevice final : public T::Device {
  public:
    // Takes its own service lease; DIERR_DEVICENOTREG without the service.
    static HRESULT Create(typename T::Device** output) noexcept {
        if (!AcquireService()) {
            return DIERR_DEVICENOTREG;
        }
        auto* device = new (std::nothrow) SyntheticDevice();
        if (device == nullptr) {
            ReleaseService();
            return E_OUTOFMEMORY;
        }
        *output = device;
        TdurObservationRecord("gamepad-device-created", ERROR_SUCCESS);
        return DI_OK;
    }

    STDMETHODIMP QueryInterface(REFIID iid, LPVOID* output) override {
        if (output == nullptr) {
            return E_POINTER;
        }
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, T::DeviceIid())) {
            *output = static_cast<typename T::Device*>(this);
            AddRef();
            return S_OK;
        }
        *output = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0) {
            delete this;
            ReleaseService();
        }
        return remaining;
    }

    STDMETHODIMP GetCapabilities(LPDIDEVCAPS caps) override {
        if (caps == nullptr) {
            return E_POINTER;
        }
        if (caps->dwSize != sizeof(DIDEVCAPS) &&
            caps->dwSize != sizeof(DIDEVCAPS_DX3)) {
            return DIERR_INVALIDPARAM;
        }
        DIDEVCAPS full{};
        full.dwSize = caps->dwSize;
        // Always attached: the game may enumerate once, so a pad plugged in
        // later must reach the same DirectInput device (neutral meanwhile).
        full.dwFlags =
            DIDC_ATTACHED | DIDC_POLLEDDEVICE | DIDC_POLLEDDATAFORMAT;
        full.dwDevType = SyntheticDevType;
        full.dwAxes = AxisCount;
        full.dwButtons = ButtonCount;
        full.dwPOVs = PovCount;
        std::memcpy(caps, &full, caps->dwSize);
        return DI_OK;
    }

    STDMETHODIMP EnumObjects(typename T::EnumObjectsCallback callback,
                             LPVOID context, DWORD flags) override {
        if (callback == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        FormatBinding binding;
        {
            Lock lock(lock_);
            binding = binding_;
        }
        const DWORD types = flags & (DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV);
        for (std::size_t i = 0; i < ObjectCount; ++i) {
            if (types != 0 && (object_type_id(i) & types) == 0) {
                continue;
            }
            typename T::ObjectInstance object;
            FillObject(i, binding, object);
            if (callback(&object, context) == DIENUM_STOP) {
                break;
            }
        }
        return DI_OK;
    }

    STDMETHODIMP GetProperty(REFGUID property, LPDIPROPHEADER header) override {
        if (header == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        Lock lock(lock_);
        std::size_t object{};
        bool device{};
        if (const HRESULT r = Resolve(*header, object, device); FAILED(r)) {
            return r;
        }
        const bool axis =
            !device && layout_object(object).kind == ObjectKind::axis;
        auto* dword = reinterpret_cast<DIPROPDWORD*>(header);
        const bool dword_size = header->dwSize == sizeof(DIPROPDWORD);
        switch (PropertyId(property)) {
        case 1: // DIPROP_BUFFERSIZE
            if (!device || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            dword->dwData = capacity_;
            return DI_OK;
        case 2: // DIPROP_AXISMODE
            if (!device || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            dword->dwData = DIPROPAXISMODE_ABS;
            return DI_OK;
        case 3: // DIPROP_GRANULARITY
            if (device || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            dword->dwData = 1;
            return DI_OK;
        case 4: { // DIPROP_RANGE
            if (!axis || header->dwSize != sizeof(DIPROPRANGE)) {
                return DIERR_INVALIDPARAM;
            }
            auto* range = reinterpret_cast<DIPROPRANGE*>(header);
            range->lMin = AxisOf(object).min;
            range->lMax = AxisOf(object).max;
            return DI_OK;
        }
        case 5: // DIPROP_DEADZONE
        case 6: // DIPROP_SATURATION
            if (!axis || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            dword->dwData = PropertyId(property) == 5
                                ? AxisOf(object).deadzone
                                : AxisOf(object).saturation;
            return DI_OK;
        case 13:   // DIPROP_INSTANCENAME
        case 14: { // DIPROP_PRODUCTNAME
            if (!device || header->dwSize != sizeof(DIPROPSTRING)) {
                return DIERR_INVALIDPARAM;
            }
            auto* text = reinterpret_cast<DIPROPSTRING*>(header);
            CopyName(text->wsz, ProductName);
            return DI_OK;
        }
        case 24: // DIPROP_VIDPID
            if (!device || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            dword->dwData =
                DWORD(SyntheticVendor) | (DWORD(SyntheticProductId) << 16);
            return DI_OK;
        default:
            // GUIDANDPATH/JOYSTICKID: no real HID interface backs this pad.
            return DIERR_UNSUPPORTED;
        }
    }

    STDMETHODIMP SetProperty(REFGUID property,
                             LPCDIPROPHEADER header) override {
        if (header == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        Lock lock(lock_);
        std::size_t object{};
        bool device{};
        if (const HRESULT r = Resolve(*header, object, device); FAILED(r)) {
            return r;
        }
        const bool axis =
            !device && layout_object(object).kind == ObjectKind::axis;
        const auto* dword = reinterpret_cast<const DIPROPDWORD*>(header);
        const bool dword_size = header->dwSize == sizeof(DIPROPDWORD);
        const auto apply = [&](auto&& change) {
            if (device) {
                for (auto& item : axes_) {
                    change(item);
                }
            } else {
                change(AxisOf(object));
            }
        };
        switch (PropertyId(property)) {
        case 1: // DIPROP_BUFFERSIZE
            if (!device || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            if (acquired_) {
                return DIERR_ACQUIRED;
            }
            try {
                capacity_ = std::min(dword->dwData, MaxBufferedEvents);
                buffer_.assign(capacity_, DIDEVICEOBJECTDATA{});
            } catch (const std::bad_alloc&) {
                capacity_ = 0;
                buffer_.clear();
                return E_OUTOFMEMORY;
            }
            head_ = count_ = 0;
            overflow_ = false;
            return DI_OK;
        case 2: // DIPROP_AXISMODE
            if (!device || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            if (acquired_) {
                return DIERR_ACQUIRED;
            }
            return dword->dwData == DIPROPAXISMODE_ABS ? DI_OK
                                                       : DIERR_UNSUPPORTED;
        case 4: { // DIPROP_RANGE
            if ((!axis && !device) || header->dwSize != sizeof(DIPROPRANGE)) {
                return DIERR_INVALIDPARAM;
            }
            if (acquired_) {
                return DIERR_ACQUIRED;
            }
            const auto* range = reinterpret_cast<const DIPROPRANGE*>(header);
            if (range->lMin >= range->lMax) {
                return DIERR_INVALIDPARAM;
            }
            apply([&](AxisProperties& item) {
                item.min = range->lMin;
                item.max = range->lMax;
            });
            return DI_OK;
        }
        case 5:   // DIPROP_DEADZONE
        case 6: { // DIPROP_SATURATION
            if ((!axis && !device) || !dword_size) {
                return DIERR_INVALIDPARAM;
            }
            if (dword->dwData > 10000) {
                return DIERR_INVALIDPARAM;
            }
            const bool dead = PropertyId(property) == 5;
            apply([&](AxisProperties& item) {
                (dead ? item.deadzone : item.saturation) = dword->dwData;
            });
            return DI_OK;
        }
        case 10: // DIPROP_CALIBRATIONMODE: no calibration is applied.
            return dword_size ? DI_OK : DIERR_INVALIDPARAM;
        default:
            // Force feedback, autocenter and action-map properties: none.
            return DIERR_UNSUPPORTED;
        }
    }

    STDMETHODIMP Acquire() override {
        Lock lock(lock_);
        if (!has_format_) {
            return DIERR_INVALIDPARAM;
        }
        if (acquired_) {
            return S_FALSE;
        }
        if (!ForegroundAllowed()) {
            return DIERR_OTHERAPPHASPRIO;
        }
        acquired_ = true;
        Sample(false);
        head_ = count_ = 0;
        overflow_ = false;
        return DI_OK;
    }
    STDMETHODIMP Unacquire() override {
        Lock lock(lock_);
        if (!acquired_) {
            return DI_NOEFFECT;
        }
        acquired_ = false;
        return DI_OK;
    }

    STDMETHODIMP GetDeviceState(DWORD size, LPVOID data) override {
        if (data == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        Lock lock(lock_);
        if (const HRESULT r = ReadyToRead(); FAILED(r)) {
            return r;
        }
        if (size != binding_.data_size) {
            return DIERR_INVALIDPARAM;
        }
        Sample(true);
        std::memcpy(data, current_.data(), current_.size());
        return DI_OK;
    }

    STDMETHODIMP GetDeviceData(DWORD object_size, LPDIDEVICEOBJECTDATA data,
                               LPDWORD in_out, DWORD flags) override {
        if (in_out == nullptr ||
            (object_size != sizeof(DIDEVICEOBJECTDATA) &&
             object_size != sizeof(DIDEVICEOBJECTDATA_DX3)) ||
            (flags & ~DWORD(DIGDD_PEEK)) != 0) {
            return DIERR_INVALIDPARAM;
        }
        Lock lock(lock_);
        if (capacity_ == 0) {
            return DIERR_NOTBUFFERED;
        }
        if (const HRESULT r = ReadyToRead(); FAILED(r)) {
            return r;
        }
        Sample(true);
        const bool peek = (flags & DIGDD_PEEK) != 0;
        const DWORD count = DWORD(std::min<std::size_t>(*in_out, count_));
        if (data != nullptr) {
            auto* target = reinterpret_cast<std::byte*>(data);
            for (DWORD i = 0; i < count; ++i) {
                const auto& item = buffer_[(head_ + i) % capacity_];
                std::memcpy(target + std::size_t(i) * object_size, &item,
                            object_size);
            }
        }
        const HRESULT result = overflow_ ? DI_BUFFEROVERFLOW : DI_OK;
        if (!peek) {
            head_ = (head_ + count) % capacity_;
            count_ -= count;
            overflow_ = false;
        }
        *in_out = count;
        return result;
    }

    STDMETHODIMP SetDataFormat(LPCDIDATAFORMAT format) override {
        if (format == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        Lock lock(lock_);
        if (acquired_) {
            return DIERR_ACQUIRED;
        }
        FormatBinding staged;
        if (const HRESULT r = bind_format(*format, staged); FAILED(r)) {
            TdurObservationRecord("gamepad-format-refused", DWORD(r));
            return r;
        }
        try {
            current_.assign(staged.data_size, std::byte{});
            previous_.assign(staged.data_size, std::byte{});
        } catch (const std::bad_alloc&) {
            has_format_ = false;
            return E_OUTOFMEMORY;
        }
        binding_ = staged;
        has_format_ = true;
        head_ = count_ = 0;
        // Records the application's layout once per format change: the
        // exact TDU format is not established yet (validation witness).
        TdurObservationRecord("gamepad-format-bound",
                              DWORD(binding_.count) |
                                  (binding_.data_size << 8));
        return DI_OK;
    }

    STDMETHODIMP SetEventNotification(HANDLE event) override {
        Lock lock(lock_);
        if (acquired_) {
            return DIERR_ACQUIRED;
        }
        event_ = event;
        return DI_POLLEDDEVICE;
    }

    STDMETHODIMP SetCooperativeLevel(HWND window, DWORD flags) override {
        const DWORD exclusive = flags & (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE);
        const DWORD focus = flags & (DISCL_FOREGROUND | DISCL_BACKGROUND);
        if ((exclusive != DISCL_EXCLUSIVE && exclusive != DISCL_NONEXCLUSIVE) ||
            (focus != DISCL_FOREGROUND && focus != DISCL_BACKGROUND)) {
            return DIERR_INVALIDPARAM;
        }
        if (window != nullptr && !IsWindow(window)) {
            return E_HANDLE;
        }
        if (focus == DISCL_FOREGROUND && window == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        Lock lock(lock_);
        if (acquired_) {
            return DIERR_ACQUIRED;
        }
        window_ = window;
        cooperative_ = flags;
        TdurObservationRecord("gamepad-cooperative-level", flags);
        return DI_OK;
    }

    STDMETHODIMP GetObjectInfo(typename T::ObjectInstance* info, DWORD id,
                               DWORD how) override {
        if (info == nullptr) {
            return E_POINTER;
        }
        if (info->dwSize != sizeof(typename T::ObjectInstance) &&
            info->dwSize != sizeof(typename T::ObjectInstanceDx3)) {
            return DIERR_INVALIDPARAM;
        }
        FormatBinding binding;
        {
            Lock lock(lock_);
            binding = binding_;
        }
        std::size_t object = ObjectCount;
        if (how == DIPH_BYOFFSET) {
            object = binding.object_at(id);
        } else if (how == DIPH_BYID) {
            object = find_object_by_id(id);
        } else {
            return DIERR_INVALIDPARAM;
        }
        if (object == ObjectCount) {
            return DIERR_OBJECTNOTFOUND;
        }
        const DWORD size = info->dwSize;
        typename T::ObjectInstance full;
        FillObject(object, binding, full);
        full.dwSize = size;
        std::memcpy(info, &full, size);
        return DI_OK;
    }

    STDMETHODIMP GetDeviceInfo(typename T::DeviceInstance* info) override {
        if (info == nullptr) {
            return E_POINTER;
        }
        if (info->dwSize != sizeof(typename T::DeviceInstance) &&
            info->dwSize != sizeof(typename T::DeviceInstanceDx3)) {
            return DIERR_INVALIDPARAM;
        }
        const DWORD size = info->dwSize;
        typename T::DeviceInstance full;
        FillInstance<T>(full);
        full.dwSize = size;
        std::memcpy(info, &full, size);
        return DI_OK;
    }

    STDMETHODIMP RunControlPanel(HWND, DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP Initialize(HINSTANCE, DWORD, REFGUID guid) override {
        return IsEqualGUID(guid, SyntheticInstance) ? S_FALSE
                                                    : DIERR_DEVICENOTREG;
    }
    STDMETHODIMP CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT* effect,
                              LPUNKNOWN) override {
        if (effect != nullptr) {
            *effect = nullptr;
        }
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP EnumEffects(typename T::EnumEffectsCallback, LPVOID,
                             DWORD) override {
        return DI_OK; // No effect exists.
    }
    STDMETHODIMP GetEffectInfo(typename T::EffectInfo*, REFGUID) override {
        return DIERR_DEVICENOTREG;
    }
    STDMETHODIMP GetForceFeedbackState(LPDWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP SendForceFeedbackCommand(DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP
    EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID,
                             DWORD) override {
        return DI_OK;
    }
    STDMETHODIMP Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }
    STDMETHODIMP Poll() override {
        Lock lock(lock_);
        if (const HRESULT r = ReadyToRead(); FAILED(r)) {
            return r;
        }
        Sample(true);
        return DI_OK;
    }
    STDMETHODIMP SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD,
                                DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP EnumEffectsInFile(typename T::Text,
                                   LPDIENUMEFFECTSINFILECALLBACK, LPVOID,
                                   DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP WriteEffectToFile(typename T::Text, DWORD, LPDIFILEEFFECT,
                                   DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    // Action mapping would need the TDU action table, not established.
    STDMETHODIMP BuildActionMap(typename T::ActionFormat*, typename T::Text,
                                DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP SetActionMap(typename T::ActionFormat*, typename T::Text,
                              DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    STDMETHODIMP GetImageInfo(typename T::ImageInfoHeader*) override {
        return DIERR_UNSUPPORTED;
    }

  private:
    SyntheticDevice() = default;
    ~SyntheticDevice() = default;

    AxisProperties& AxisOf(std::size_t object) noexcept {
        return axes_[layout_object(object).instance];
    }

    HRESULT Resolve(const DIPROPHEADER& header, std::size_t& object,
                    bool& device) const noexcept {
        if (header.dwHeaderSize != sizeof(DIPROPHEADER) ||
            header.dwSize < sizeof(DIPROPHEADER)) {
            return DIERR_INVALIDPARAM;
        }
        device = false;
        switch (header.dwHow) {
        case DIPH_DEVICE:
            if (header.dwObj != 0) {
                return DIERR_INVALIDPARAM;
            }
            device = true;
            object = ObjectCount;
            return DI_OK;
        case DIPH_BYOFFSET:
            object =
                has_format_ ? binding_.object_at(header.dwObj) : ObjectCount;
            break;
        case DIPH_BYID:
            object = find_object_by_id(header.dwObj);
            break;
        default:
            return DIERR_UNSUPPORTED; // DIPH_BYUSAGE
        }
        return object == ObjectCount ? DIERR_OBJECTNOTFOUND : DI_OK;
    }

    template <class Object>
    static void FillObject(std::size_t index, const FormatBinding& binding,
                           Object& out) noexcept {
        std::memset(&out, 0, sizeof(out));
        out.dwSize = sizeof(out);
        const auto& object = layout_object(index);
        out.guidType = *object.type;
        out.dwOfs = DWORD(index) * 4; // Placeholder without a bound format.
        for (const auto& item : binding.objects()) {
            if (item.object == index) {
                out.dwOfs = item.offset;
            }
        }
        out.dwType = object_type_id(index);
        out.dwFlags =
            object.kind == ObjectKind::axis ? DIDOI_ASPECTPOSITION : 0;
        CopyName(out.tszName, object.name);
        out.wUsagePage = object.kind == ObjectKind::button ? 0x09 : 0x01;
        out.wUsage =
            object.kind == ObjectKind::axis     ? WORD(0x30 + object.instance)
            : object.kind == ObjectKind::button ? WORD(object.instance + 1)
                                                : WORD(0x39);
    }

    bool ForegroundAllowed() const noexcept {
        if ((cooperative_ & DISCL_FOREGROUND) == 0) {
            return true;
        }
        const HWND foreground = GetForegroundWindow();
        return foreground != nullptr && window_ != nullptr &&
               GetAncestor(foreground, GA_ROOTOWNER) ==
                   GetAncestor(window_, GA_ROOTOWNER);
    }

    // DirectInput foreground contract requested by the game itself; the
    // window focus/cursor policy of the mod belongs to other modules.
    HRESULT ReadyToRead() noexcept {
        if (!acquired_) {
            return DIERR_NOTACQUIRED;
        }
        if (!ForegroundAllowed()) {
            acquired_ = false;
            return DIERR_INPUTLOST;
        }
        return DI_OK;
    }

    void Push(DWORD offset, DWORD value) noexcept {
        DIDEVICEOBJECTDATA item{};
        item.dwOfs = offset;
        item.dwData = value;
        item.dwTimeStamp = GetTickCount();
        item.dwSequence = ++sequence_;
        if (count_ == capacity_) {
            head_ = (head_ + 1) % capacity_; // Policy: oldest event dropped.
            --count_;
            overflow_ = true;
        }
        buffer_[(head_ + count_) % capacity_] = item;
        ++count_;
    }

    void Sample(bool record) noexcept {
        const auto state = ReadService();
        write_state(binding_, state.sample, axes_, current_);
        if (record && capacity_ != 0) {
            for (const auto& item : binding_.objects()) {
                const DWORD now = read_object(item, current_);
                if (now != read_object(item, previous_)) {
                    Push(item.offset, now);
                }
            }
        }
        if (record && event_ != nullptr && state.generation != generation_) {
            SetEvent(event_);
        }
        generation_ = state.generation;
        std::memcpy(previous_.data(), current_.data(), current_.size());
    }

    std::atomic<ULONG> references_{1};
    SRWLOCK lock_ = SRWLOCK_INIT;
    FormatBinding binding_{};
    bool has_format_{};
    std::array<AxisProperties, AxisCount> axes_{};
    bool acquired_{};
    HWND window_{};
    DWORD cooperative_ = DISCL_NONEXCLUSIVE | DISCL_BACKGROUND;
    HANDLE event_{};
    std::vector<std::byte> current_, previous_;
    std::vector<DIDEVICEOBJECTDATA> buffer_;
    std::size_t head_{}, count_{};
    DWORD capacity_{};
    DWORD sequence_{};
    bool overflow_{};
    std::uint64_t generation_{};
};

class FactoryIdentity;

template <class T> class FactoryWrapper final : public T::Factory {
  public:
    STDMETHODIMP QueryInterface(REFIID iid, LPVOID* output) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP CreateDevice(REFGUID guid, typename T::Device** device,
                              LPUNKNOWN outer) override {
        if (device == nullptr) {
            return E_POINTER;
        }
        if (!IsEqualGUID(guid, SyntheticInstance)) {
            return inner_->CreateDevice(guid, device, outer);
        }
        *device = nullptr;
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        return SyntheticDevice<T>::Create(device);
    }

    STDMETHODIMP EnumDevices(DWORD type,
                             typename T::EnumDevicesCallback callback,
                             LPVOID context, DWORD flags) override {
        if (callback == nullptr) {
            return DIERR_INVALIDPARAM;
        }
        const bool listed =
            (type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL ||
             type == DI8DEVTYPE_GAMEPAD) &&
            (flags & DIEDFL_FORCEFEEDBACK) == 0;
        if (listed) {
            // Listed first: a game taking the first controller gets the
            // SDL pad; it stays listed even before any pad is plugged in.
            typename T::DeviceInstance instance;
            FillInstance<T>(instance);
            if (callback(&instance, context) == DIENUM_STOP) {
                return DI_OK;
            }
        }
        Filter filter{callback, context, ReadService(), 0};
        const HRESULT result =
            inner_->EnumDevices(type, &FilterCallback, &filter, flags);
        if (filter.hidden != 0) {
            TdurObservationRecord("gamepad-system-pad-hidden", filter.hidden);
        }
        return result;
    }

    STDMETHODIMP GetDeviceStatus(REFGUID guid) override {
        return IsEqualGUID(guid, SyntheticInstance)
                   ? DI_OK
                   : inner_->GetDeviceStatus(guid);
    }
    STDMETHODIMP RunControlPanel(HWND window, DWORD flags) override {
        return inner_->RunControlPanel(window, flags);
    }
    STDMETHODIMP Initialize(HINSTANCE instance, DWORD version) override {
        return inner_->Initialize(instance, version);
    }
    STDMETHODIMP FindDevice(REFGUID guid, typename T::Text name,
                            LPGUID output) override {
        return inner_->FindDevice(guid, name, output);
    }
    // Semantic mapping and the system configuration UI stay the system's;
    // the synthetic pad does not take part in them (limit, see report).
    STDMETHODIMP EnumDevicesBySemantics(typename T::Text user,
                                        typename T::ActionFormat* format,
                                        typename T::SemanticsCallback callback,
                                        LPVOID context, DWORD flags) override {
        return inner_->EnumDevicesBySemantics(user, format, callback, context,
                                              flags);
    }
    STDMETHODIMP ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK callback,
                                  typename T::ConfigureParams* params,
                                  DWORD flags, LPVOID data) override {
        return inner_->ConfigureDevices(callback, params, flags, data);
    }

  private:
    friend class FactoryIdentity;

    FactoryWrapper(FactoryIdentity& identity,
                   typename T::Factory* inner) noexcept
        : identity_(identity), inner_(inner) {}
    FactoryWrapper(const FactoryWrapper&) = delete;
    FactoryWrapper& operator=(const FactoryWrapper&) = delete;
    ~FactoryWrapper() {
        if (inner_ != nullptr) {
            inner_->Release();
        }
    }

    struct Filter {
        typename T::EnumDevicesCallback callback;
        LPVOID context;
        ServiceState state;
        DWORD hidden;
    };
    static BOOL CALLBACK
    FilterCallback(const typename T::DeviceInstance* instance, LPVOID context) {
        auto& filter = *static_cast<Filter*>(context);
        if (instance != nullptr && IsHidden(*instance, filter.state)) {
            ++filter.hidden; // Same pad already served by the SDL device.
            return DIENUM_CONTINUE;
        }
        return filter.callback(instance, filter.context);
    }

    FactoryIdentity& identity_;
    typename T::Factory* inner_;
};

// Both character variants are facets of one COM object, with one service
// lease and one reference count. Never expose an unwrapped interface from QI:
// its IUnknown would be the system object's identity instead of ours.
class FactoryIdentity final {
  public:
    FactoryIdentity(IDirectInput8A* ansi, IDirectInput8W* wide) noexcept
        : ansi_(*this, ansi), wide_(*this, wide) {}
    FactoryIdentity(IDirectInput8W* wide, IDirectInput8A* ansi) noexcept
        : FactoryIdentity(ansi, wide) {}
    FactoryIdentity(const FactoryIdentity&) = delete;
    FactoryIdentity& operator=(const FactoryIdentity&) = delete;

    HRESULT QueryInterface(REFIID iid, LPVOID* output) noexcept {
        if (output == nullptr) {
            return E_POINTER;
        }
        *output = nullptr;
        if (IsEqualIID(iid, IID_IUnknown)) {
            // A is canonical whenever supported, including W-first creation.
            // A factory exposing only W still gets one stable IUnknown.
            *output = ansi_.inner_ != nullptr
                          ? static_cast<IUnknown*>(
                                static_cast<IDirectInput8A*>(&ansi_))
                          : static_cast<IUnknown*>(
                                static_cast<IDirectInput8W*>(&wide_));
        } else if (IsEqualIID(iid, IID_IDirectInput8A) &&
                   ansi_.inner_ != nullptr) {
            *output = static_cast<IDirectInput8A*>(&ansi_);
        } else if (IsEqualIID(iid, IID_IDirectInput8W) &&
                   wide_.inner_ != nullptr) {
            *output = static_cast<IDirectInput8W*>(&wide_);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG AddRef() noexcept { return ++references_; }
    ULONG Release() noexcept {
        const ULONG remaining = --references_;
        if (remaining == 0) {
            // Destroy both facets and release their native refs before the
            // single factory lease. Device leases are independent.
            delete this;
            ReleaseService();
        }
        return remaining;
    }

  private:
    ~FactoryIdentity() = default;

    FactoryWrapper<Ansi> ansi_;
    FactoryWrapper<Wide> wide_;
    std::atomic<ULONG> references_{1};
};

template <class T>
STDMETHODIMP FactoryWrapper<T>::QueryInterface(REFIID iid, LPVOID* output) {
    return identity_.QueryInterface(iid, output);
}
template <class T> STDMETHODIMP_(ULONG) FactoryWrapper<T>::AddRef() {
    return identity_.AddRef();
}
template <class T> STDMETHODIMP_(ULONG) FactoryWrapper<T>::Release() {
    return identity_.Release();
}

template <class T, class Other> void* Wrap(void* system) noexcept {
    if (!AcquireService()) {
        return system;
    }
    auto* first = static_cast<typename T::Factory*>(system);
    typename Other::Factory* other = nullptr;
    const HRESULT result = first->QueryInterface(
        Other::FactoryIid(), reinterpret_cast<void**>(&other));
    if ((FAILED(result) && result != E_NOINTERFACE) ||
        (SUCCEEDED(result) && other == nullptr)) {
        ReleaseService();
        return system;
    }
    // E_NOINTERFACE preserves the native object's fixed supported IID set;
    // normal DirectInput 8 exposes both A and W, so both get wrapped.
    auto* identity = new (std::nothrow) FactoryIdentity(first, other);
    if (identity == nullptr) {
        if (other != nullptr) {
            other->Release();
        }
        ReleaseService();
        return system;
    }
    TdurObservationRecord("gamepad-factory-wrapped", ERROR_SUCCESS);
    void* output = nullptr;
    identity->QueryInterface(T::FactoryIid(), &output);
    identity->Release(); // Transfer the initial reference to the requested IID.
    return output;
}

} // namespace

void* WrapFactory(REFIID iid, void* system) noexcept {
    if (system == nullptr || OnServiceThread() || !ServiceRequested()) {
        return system;
    }
    if (IsEqualIID(iid, IID_IDirectInput8A)) {
        return Wrap<Ansi, Wide>(system);
    }
    if (IsEqualIID(iid, IID_IDirectInput8W)) {
        return Wrap<Wide, Ansi>(system);
    }
    return system;
}

} // namespace tdur::mod::gamepad

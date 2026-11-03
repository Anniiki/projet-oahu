#pragma once

// Pure DirectInput data-format core of the in-process SDL gamepad device.
// Contracts used here are the public DirectInput 8 SDK ones (dinput.h), not
// routines of the TDU binary. No SDL type, window or device is touched, so
// the core is testable without hardware.
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>

#include <dinput.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tdur::mod::gamepad {

// Modern compatibility policy: the object layout that the Microsoft Xbox 360
// HID driver publicly exposes through DirectInput (X/Y left stick, Z both
// triggers, Rx/Ry right stick, 10 buttons, 1 POV). It is not a TDU binding.
enum class ObjectKind : std::uint8_t { axis, button, pov };
inline constexpr std::size_t AxisCount = 5;    // X, Y, Z, Rx, Ry
inline constexpr std::size_t ButtonCount = 10; // A B X Y LB RB Back Start LS RS
inline constexpr std::size_t PovCount = 1;
inline constexpr std::size_t ObjectCount = AxisCount + ButtonCount + PovCount;
// Optional-object bit observed in every entry of the Windows SDK
// c_dfDIJoystick2 table (164 entries). No DIDFT_OPTIONAL macro is
// declared by Windows SDK 10.0.26100 dinput.h.
inline constexpr DWORD OptionalObject = 0x80000000u;
inline constexpr DWORD PovCentered = 0xFFFFFFFFu;
inline constexpr DWORD MaxDataSize = 64 * 1024;

struct LayoutObject {
    ObjectKind kind;
    std::uint8_t instance; // Per-kind DIDFT instance number.
    const GUID* type;      // GUID_XAxis..., GUID_Button, GUID_POV.
    const wchar_t* name;
};
[[nodiscard]] const LayoutObject& layout_object(std::size_t index) noexcept;
[[nodiscard]] DWORD object_type_id(std::size_t index) noexcept;
// DIPH_BYID lookup; ObjectCount when absent.
[[nodiscard]] std::size_t find_object_by_id(DWORD id) noexcept;

// Physical sample already arranged in layout order. Axes are in [-1,1];
// Y/Ry follow the HID convention (negative = up), as SDL already does.
struct PadSample {
    bool connected{};
    std::array<float, AxisCount> axes{};
    std::array<bool, ButtonCount> buttons{};
    DWORD pov = PovCentered;
    bool operator==(const PadSample&) const = default;
};
// Dpad to POV hundredths of degrees; opposite directions cancel.
[[nodiscard]] DWORD pov_from_dpad(bool up, bool down, bool left,
                                  bool right) noexcept;

struct AxisProperties {
    LONG min = 0;
    LONG max = 65535;
    DWORD deadzone = 0; // DirectInput units, 0..10000 of half range.
    DWORD saturation = 10000;
};
// DirectInput dead zone/saturation semantics applied to a centred value.
[[nodiscard]] LONG scale_axis(float value,
                              const AxisProperties& properties) noexcept;

struct BoundObject {
    std::uint8_t object{}; // Layout index.
    DWORD offset{};        // Offset inside the application data format.
};
struct FormatBinding {
    DWORD data_size{};
    std::array<BoundObject, ObjectCount> bound{};
    std::size_t count{};
    [[nodiscard]] std::span<const BoundObject> objects() const noexcept {
        return {bound.data(), count};
    }
    // ObjectCount when the offset is not bound in this format.
    [[nodiscard]] std::size_t object_at(DWORD offset) const noexcept;
    [[nodiscard]] bool has(std::size_t object) const noexcept;
};

// SetDataFormat contract: each DIOBJECTDATAFORMAT takes the first free
// layout object of a compatible class, type GUID and instance. A missing
// non-optional object refuses the whole format; output is then unchanged.
[[nodiscard]] HRESULT bind_format(const DIDATAFORMAT& format,
                                  FormatBinding& output) noexcept;

// Writes every bound object; unbound bytes are zeroed. output.size() must
// equal binding.data_size, otherwise nothing is written.
bool write_state(const FormatBinding& binding, const PadSample& sample,
                 std::span<const AxisProperties, AxisCount> axes,
                 std::span<std::byte> output) noexcept;

// Value of one bound object inside a state buffer, as DIDEVICEOBJECTDATA
// reports it (axis LONG, button 0x80/0, POV DWORD).
[[nodiscard]] DWORD read_object(const BoundObject& object,
                                std::span<const std::byte> state) noexcept;

} // namespace tdur::mod::gamepad

// Oracles from the public DirectInput 8 contract (dinput.h, DIJOYSTATE),
// written before any run; no SDL, device, window or game is involved.
#include "tdur/mod/gamepad_dinput_format.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
using namespace tdur::mod::gamepad;

int failures = 0;
int checks = 0;
void Check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

constexpr DWORD Any = DIDFT_ANYINSTANCE;
constexpr DWORD Optional = OptionalObject;

std::vector<DIOBJECTDATAFORMAT> JoystickObjects(bool rz_required) {
    std::vector<DIOBJECTDATAFORMAT> objects{
        {&GUID_XAxis, DIJOFS_X, Optional | DIDFT_AXIS | Any, 0},
        {&GUID_YAxis, DIJOFS_Y, Optional | DIDFT_AXIS | Any, 0},
        {&GUID_ZAxis, DIJOFS_Z, Optional | DIDFT_AXIS | Any, 0},
        {&GUID_RxAxis, DIJOFS_RX, Optional | DIDFT_AXIS | Any, 0},
        {&GUID_RyAxis, DIJOFS_RY, Optional | DIDFT_AXIS | Any, 0},
        {&GUID_RzAxis, DIJOFS_RZ,
         (rz_required ? 0 : Optional) | DIDFT_AXIS | Any, 0},
        {&GUID_Slider, DIJOFS_SLIDER(0), Optional | DIDFT_AXIS | Any, 0},
        {&GUID_POV, DIJOFS_POV(0), Optional | DIDFT_POV | Any, 0},
    };
    for (DWORD i = 0; i < 32; ++i) {
        objects.push_back(
            {nullptr, DIJOFS_BUTTON(i), Optional | DIDFT_BUTTON | Any, 0});
    }
    return objects;
}

DIDATAFORMAT Format(std::vector<DIOBJECTDATAFORMAT>& objects, DWORD size) {
    return {sizeof(DIDATAFORMAT),  sizeof(DIOBJECTDATAFORMAT),
            DIDF_ABSAXIS,          size,
            DWORD(objects.size()), objects.data()};
}

LONG AxisAt(const std::vector<std::byte>& state, DWORD offset) {
    LONG value{};
    std::memcpy(&value, state.data() + offset, sizeof(value));
    return value;
}

void BindsJoystickState() {
    auto objects = JoystickObjects(false);
    const auto format = Format(objects, sizeof(DIJOYSTATE));
    FormatBinding binding;
    Check(bind_format(format, binding) == DI_OK, "DIJOYSTATE accepted");
    Check(binding.count == ObjectCount, "16 layout objects bound");
    Check(binding.object_at(DIJOFS_RZ) == ObjectCount, "Rz stays unbound");
    Check(binding.object_at(DIJOFS_SLIDER(0)) == ObjectCount,
          "slider stays unbound");
    Check(binding.object_at(DIJOFS_BUTTON(10)) == ObjectCount,
          "button 10 stays unbound");
    Check(binding.object_at(DIJOFS_BUTTON(0)) == AxisCount, "button 0 bound");
    Check(binding.object_at(DIJOFS_POV(0)) == ObjectCount - 1, "POV bound");

    std::array<AxisProperties, AxisCount> axes{};
    std::vector<std::byte> state(sizeof(DIJOYSTATE), std::byte{0x55});
    PadSample neutral;
    neutral.connected = true;
    Check(write_state(binding, neutral, axes, state), "neutral written");
    Check(AxisAt(state, DIJOFS_X) == 32767, "X centre 32767");
    Check(AxisAt(state, DIJOFS_RZ) == 0, "unbound bytes zeroed");
    DWORD pov{};
    std::memcpy(&pov, state.data() + DIJOFS_POV(0), sizeof(pov));
    Check(pov == PovCentered, "POV centred");

    PadSample moved = neutral;
    moved.axes = {-1.0f, 1.0f, 0.5f, 0.0f, -0.5f};
    moved.buttons[0] = true;
    moved.pov = pov_from_dpad(true, false, false, true);
    Check(write_state(binding, moved, axes, state), "moved written");
    Check(AxisAt(state, DIJOFS_X) == 0, "X full left 0");
    Check(AxisAt(state, DIJOFS_Y) == 65535, "Y full down 65535");
    Check(AxisAt(state, DIJOFS_Z) == 32767 + 16384, "Z half positive");
    Check(AxisAt(state, DIJOFS_RY) == 32767 - 16384, "Ry half negative");
    Check(std::to_integer<int>(state[DIJOFS_BUTTON(0)]) == 0x80,
          "button 0 pressed 0x80");
    std::memcpy(&pov, state.data() + DIJOFS_POV(0), sizeof(pov));
    Check(pov == 4500, "POV up-right 4500");
    const BoundObject button0{std::uint8_t(AxisCount), DIJOFS_BUTTON(0)};
    Check(read_object(button0, state) == 0x80, "read_object button");

    PadSample unplugged = moved;
    unplugged.connected = false;
    Check(write_state(binding, unplugged, axes, state), "unplugged written");
    Check(AxisAt(state, DIJOFS_X) == 32767 &&
              std::to_integer<int>(state[DIJOFS_BUTTON(0)]) == 0,
          "disconnected pad is neutral");

    std::vector<std::byte> wrong(sizeof(DIJOYSTATE) + 4);
    Check(!write_state(binding, neutral, axes, wrong), "size mismatch refused");
}

void RefusesInvalidFormats() {
    FormatBinding previous;
    auto good = JoystickObjects(false);
    const auto good_format = Format(good, sizeof(DIJOYSTATE));
    Check(bind_format(good_format, previous) == DI_OK, "baseline bound");
    const auto baseline = previous.count;

    auto required = JoystickObjects(true);
    auto format = Format(required, sizeof(DIJOYSTATE));
    Check(bind_format(format, previous) == DIERR_INVALIDPARAM,
          "required Rz refused");
    Check(previous.count == baseline, "refusal keeps previous binding");

    std::vector<DIOBJECTDATAFORMAT> misaligned{
        {&GUID_XAxis, 2, DIDFT_AXIS | Any, 0}};
    format = Format(misaligned, 16);
    Check(bind_format(format, previous) == DIERR_INVALIDPARAM,
          "misaligned axis refused");

    std::vector<DIOBJECTDATAFORMAT> overlap{
        {&GUID_XAxis, 0, DIDFT_AXIS | Any, 0},
        {nullptr, 2, DIDFT_BUTTON | Any, 0}};
    format = Format(overlap, 16);
    Check(bind_format(format, previous) == DIERR_INVALIDPARAM,
          "overlapping fields refused");

    std::vector<DIOBJECTDATAFORMAT> outside{
        {&GUID_XAxis, 16, DIDFT_AXIS | Any, 0}};
    format = Format(outside, 16);
    Check(bind_format(format, previous) == DIERR_INVALIDPARAM,
          "field beyond data size refused");

    format = Format(good, sizeof(DIJOYSTATE) + 2);
    Check(bind_format(format, previous) == DIERR_INVALIDPARAM,
          "data size not multiple of 4 refused");

    std::vector<DIOBJECTDATAFORMAT> velocity{
        {&GUID_XAxis, 0, DIDFT_AXIS | Any, DIDOI_ASPECTVELOCITY}};
    format = Format(velocity, 4);
    Check(bind_format(format, previous) == DIERR_INVALIDPARAM,
          "required velocity aspect has no object");

    std::vector<DIOBJECTDATAFORMAT> instance{
        {nullptr, 0, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(9), 0},
        {nullptr, 1, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(10) | Optional, 0}};
    format = Format(instance, 4);
    FormatBinding by_instance;
    Check(bind_format(format, by_instance) == DI_OK && by_instance.count == 1 &&
              by_instance.object_at(0) == AxisCount + 9,
          "explicit button instance 9 bound, 10 absent");
}

void ScalesLikeDirectInput() {
    AxisProperties p{-1000, 1000, 2000, 5000};
    Check(scale_axis(0.1f, p) == 0, "inside dead zone");
    Check(scale_axis(1.0f, p) == 1000, "full positive");
    Check(scale_axis(0.6f, p) == 1000, "beyond saturation");
    Check(scale_axis(-0.35f, p) == -500, "half of live band");
    Check(scale_axis(std::numeric_limits<float>::quiet_NaN(), p) == 0,
          "NaN is neutral");
    AxisProperties defaults;
    Check(scale_axis(-1.0f, defaults) == 0 &&
              scale_axis(1.0f, defaults) == 65535,
          "default range 0..65535");
}

void ObjectIdentities() {
    for (std::size_t i = 0; i < ObjectCount; ++i) {
        Check(find_object_by_id(object_type_id(i)) == i, "type id round trip");
    }
    Check(find_object_by_id(DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(5)) ==
              ObjectCount,
          "Rz id absent");
    Check(pov_from_dpad(true, true, false, false) == PovCentered,
          "opposite dpad cancels");
    Check(pov_from_dpad(false, true, true, false) == 22500, "down-left");
}

} // namespace

int main() {
    FormatBinding sdk_binding{};
    Check(bind_format(c_dfDIJoystick2, sdk_binding) == DI_OK,
          "accept actual SDK c_dfDIJoystick2 optional objects");
    Check(sdk_binding.count == ObjectCount,
          "SDK format binds exactly the supported physical objects");
    BindsJoystickState();
    RefusesInvalidFormats();
    ScalesLikeDirectInput();
    ObjectIdentities();
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

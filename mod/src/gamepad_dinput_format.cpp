#include "tdur/mod/gamepad_dinput_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tdur::mod::gamepad {
namespace {

constexpr std::array<LayoutObject, ObjectCount> Layout{{
    {ObjectKind::axis, 0, &GUID_XAxis, L"X Axis"},
    {ObjectKind::axis, 1, &GUID_YAxis, L"Y Axis"},
    {ObjectKind::axis, 2, &GUID_ZAxis, L"Z Axis"},
    {ObjectKind::axis, 3, &GUID_RxAxis, L"X Rotation"},
    {ObjectKind::axis, 4, &GUID_RyAxis, L"Y Rotation"},
    {ObjectKind::button, 0, &GUID_Button, L"Button 0"},
    {ObjectKind::button, 1, &GUID_Button, L"Button 1"},
    {ObjectKind::button, 2, &GUID_Button, L"Button 2"},
    {ObjectKind::button, 3, &GUID_Button, L"Button 3"},
    {ObjectKind::button, 4, &GUID_Button, L"Button 4"},
    {ObjectKind::button, 5, &GUID_Button, L"Button 5"},
    {ObjectKind::button, 6, &GUID_Button, L"Button 6"},
    {ObjectKind::button, 7, &GUID_Button, L"Button 7"},
    {ObjectKind::button, 8, &GUID_Button, L"Button 8"},
    {ObjectKind::button, 9, &GUID_Button, L"Button 9"},
    {ObjectKind::pov, 0, &GUID_POV, L"Hat Switch"},
}};

constexpr DWORD KindMask(ObjectKind kind) noexcept {
    switch (kind) {
    case ObjectKind::axis:
        return DIDFT_AXIS;
    case ObjectKind::button:
        return DIDFT_BUTTON;
    case ObjectKind::pov:
        return DIDFT_POV;
    }
    return 0;
}

constexpr DWORD Width(ObjectKind kind) noexcept {
    return kind == ObjectKind::button ? 1 : 4;
}

bool Compatible(const DIOBJECTDATAFORMAT& wanted, const LayoutObject& object) {
    if ((wanted.dwType & KindMask(object.kind)) == 0) {
        return false;
    }
    // Force-feedback, collection and output objects do not exist here.
    if ((wanted.dwType & (DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER |
                          DIDFT_OUTPUT | DIDFT_COLLECTION)) != 0) {
        return false;
    }
    // Only position aspects exist (c_dfDIJoystick2 velocity/force fields
    // also name GUID_XAxis and must stay unbound).
    const DWORD aspect = wanted.dwFlags & DIDOI_ASPECTMASK;
    if (aspect != 0 && aspect != DIDOI_ASPECTPOSITION) {
        return false;
    }
    if (wanted.pguid != nullptr && !IsEqualGUID(*wanted.pguid, *object.type)) {
        return false;
    }
    const bool any = (wanted.dwType & DIDFT_ANYINSTANCE) == DIDFT_ANYINSTANCE;
    return any || DIDFT_GETINSTANCE(wanted.dwType) == object.instance;
}

} // namespace

const LayoutObject& layout_object(std::size_t index) noexcept {
    return Layout[std::min(index, ObjectCount - 1)];
}

DWORD object_type_id(std::size_t index) noexcept {
    const auto& object = layout_object(index);
    const DWORD type = object.kind == ObjectKind::axis     ? DIDFT_ABSAXIS
                       : object.kind == ObjectKind::button ? DIDFT_PSHBUTTON
                                                           : DIDFT_POV;
    return type | DIDFT_MAKEINSTANCE(object.instance);
}

std::size_t find_object_by_id(DWORD id) noexcept {
    for (std::size_t i = 0; i < ObjectCount; ++i) {
        const auto& object = Layout[i];
        if ((id & KindMask(object.kind)) != 0 &&
            DIDFT_GETINSTANCE(id) == object.instance) {
            return i;
        }
    }
    return ObjectCount;
}

DWORD pov_from_dpad(bool up, bool down, bool left, bool right) noexcept {
    const int y = (up ? 1 : 0) - (down ? 1 : 0);
    const int x = (right ? 1 : 0) - (left ? 1 : 0);
    if (x == 0 && y == 0) {
        return PovCentered;
    }
    // Clockwise from north, in hundredths of degrees.
    constexpr std::array<std::array<DWORD, 3>, 3> table{{
        {22500, 18000, 13500}, // y = -1 : x = -1, 0, 1
        {27000, PovCentered, 9000},
        {31500, 0, 4500},
    }};
    return table[std::size_t(y + 1)][std::size_t(x + 1)];
}

LONG scale_axis(float value, const AxisProperties& p) noexcept {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }
    const double v = std::clamp(double(value), -1.0, 1.0);
    const double magnitude = std::abs(v);
    const double dead = double(std::min<DWORD>(p.deadzone, 10000)) / 10000.0;
    const double saturation =
        double(std::min<DWORD>(p.saturation, 10000)) / 10000.0;
    double scaled{};
    if (magnitude <= dead) {
        scaled = 0.0;
    } else if (magnitude >= saturation || saturation <= dead) {
        scaled = 1.0;
    } else {
        scaled = (magnitude - dead) / (saturation - dead);
    }
    const std::int64_t min = p.min;
    const std::int64_t max = p.max;
    const std::int64_t center = min + (max - min) / 2;
    const double span = v < 0 ? double(center - min) : double(max - center);
    const double signed_scaled = v < 0 ? -scaled : scaled;
    const auto result =
        center + std::int64_t(std::llround(signed_scaled * span));
    return LONG(std::clamp(result, min, max));
}

std::size_t FormatBinding::object_at(DWORD offset) const noexcept {
    for (const auto& item : objects()) {
        if (item.offset == offset) {
            return item.object;
        }
    }
    return ObjectCount;
}

bool FormatBinding::has(std::size_t object) const noexcept {
    for (const auto& item : objects()) {
        if (item.object == object) {
            return true;
        }
    }
    return false;
}

HRESULT bind_format(const DIDATAFORMAT& format,
                    FormatBinding& output) noexcept {
    if (format.dwSize != sizeof(DIDATAFORMAT) ||
        format.dwObjSize != sizeof(DIOBJECTDATAFORMAT) ||
        format.dwDataSize == 0 || format.dwDataSize > MaxDataSize ||
        format.dwDataSize % 4 != 0 ||
        (format.dwNumObjs != 0 && format.rgodf == nullptr) ||
        format.dwNumObjs > 4096) {
        return DIERR_INVALIDPARAM;
    }
    FormatBinding staged{};
    staged.data_size = format.dwDataSize;
    std::array<bool, ObjectCount> used{};
    for (DWORD i = 0; i < format.dwNumObjs; ++i) {
        const auto& wanted = format.rgodf[i];
        std::size_t match = ObjectCount;
        for (std::size_t j = 0; j < ObjectCount; ++j) {
            if (!used[j] && Compatible(wanted, Layout[j])) {
                match = j;
                break;
            }
        }
        if (match == ObjectCount) {
            if ((wanted.dwType & OptionalObject) != 0) {
                continue;
            }
            return DIERR_INVALIDPARAM;
        }
        const DWORD width = Width(Layout[match].kind);
        if (wanted.dwOfs > format.dwDataSize - width ||
            (width == 4 && wanted.dwOfs % 4 != 0)) {
            return DIERR_INVALIDPARAM;
        }
        for (const auto& previous : staged.objects()) {
            const DWORD previous_width = Width(Layout[previous.object].kind);
            if (wanted.dwOfs < previous.offset + previous_width &&
                previous.offset < wanted.dwOfs + width) {
                return DIERR_INVALIDPARAM; // Overlapping application fields.
            }
        }
        used[match] = true;
        staged.bound[staged.count++] = {std::uint8_t(match), wanted.dwOfs};
    }
    output = staged;
    return DI_OK;
}

bool write_state(const FormatBinding& binding, const PadSample& sample,
                 std::span<const AxisProperties, AxisCount> axes,
                 std::span<std::byte> output) noexcept {
    if (binding.data_size == 0 || output.size() != binding.data_size) {
        return false;
    }
    std::memset(output.data(), 0, output.size());
    for (const auto& item : binding.objects()) {
        const auto& object = Layout[item.object];
        auto* target = output.data() + item.offset;
        if (object.kind == ObjectKind::axis) {
            const float value =
                sample.connected ? sample.axes[object.instance] : 0.0f;
            const LONG scaled = scale_axis(value, axes[object.instance]);
            std::memcpy(target, &scaled, sizeof(scaled));
        } else if (object.kind == ObjectKind::button) {
            *target = std::byte(
                sample.connected && sample.buttons[object.instance] ? 0x80
                                                                    : 0x00);
        } else {
            const DWORD pov = sample.connected ? sample.pov : PovCentered;
            std::memcpy(target, &pov, sizeof(pov));
        }
    }
    return true;
}

DWORD read_object(const BoundObject& object,
                  std::span<const std::byte> state) noexcept {
    const auto kind = Layout[object.object].kind;
    if (kind == ObjectKind::button) {
        return object.offset < state.size()
                   ? DWORD(std::to_integer<std::uint8_t>(state[object.offset]))
                   : 0;
    }
    DWORD value{};
    if (object.offset + sizeof(value) <= state.size()) {
        std::memcpy(&value, state.data() + object.offset, sizeof(value));
    }
    return value;
}

} // namespace tdur::mod::gamepad

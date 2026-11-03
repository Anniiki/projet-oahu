#pragma once

#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>

namespace tdur::mod {

enum class NativeInputKind { hid_interface, keyboard_class, pointer_class };
struct NativeInputPath {
    NativeInputKind kind;
    // HID DOS device alias, excluding the optional keyboard collection suffix.
    // Empty for a direct KeyboardClass/PointerClass object path.
    std::wstring_view alias;
};

constexpr wchar_t AsciiLower(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? wchar_t(value + (L'a' - L'A'))
                                          : value;
}
constexpr bool EqualInputPath(std::wstring_view left,
                              std::wstring_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (AsciiLower(left[i]) != AsciiLower(right[i])) {
            return false;
        }
    }
    return true;
}
constexpr bool StartsInputPath(std::wstring_view path,
                               std::wstring_view prefix) noexcept {
    return path.size() >= prefix.size() &&
           EqualInputPath(path.substr(0, prefix.size()), prefix);
}

// A narrow input-device classification, not a generic device/protection rule.
// Native absolute paths only; never accept a filesystem path containing a HID
// shaped filename. The caller must additionally resolve a HID alias to a real
// simple Device object before forwarding an open unchanged.
constexpr std::optional<NativeInputPath>
ClassifyNativeInputPath(std::wstring_view path) noexcept {
    for (const auto [prefix, kind] :
         {std::pair{std::wstring_view(L"\\Device\\KeyboardClass"),
                    NativeInputKind::keyboard_class},
          std::pair{std::wstring_view(L"\\Device\\PointerClass"),
                    NativeInputKind::pointer_class}}) {
        if (StartsInputPath(path, prefix) && path.size() > prefix.size()) {
            bool digits = true;
            for (auto value : path.substr(prefix.size())) {
                digits &= value >= L'0' && value <= L'9';
            }
            if (digits) {
                return NativeInputPath{kind, {}};
            }
        }
    }
    if (!StartsInputPath(path, L"\\??\\HID#")) {
        return std::nullopt;
    }
    path.remove_prefix(4);
    if (path.size() >= 4 &&
        EqualInputPath(path.substr(path.size() - 4), L"\\kbd")) {
        path.remove_suffix(4);
    }
    constexpr std::wstring_view hidClass =
        L"#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    if (path.size() <= 4 + hidClass.size() ||
        !EqualInputPath(path.substr(path.size() - hidClass.size()), hidClass)) {
        return std::nullopt;
    }
    std::size_t separators = 0;
    for (const auto value : path) {
        if (value == L'\\' || value == L'/' || value == L':' || value < L' ') {
            return std::nullopt;
        }
        separators += value == L'#';
    }
    if (separators < 3) {
        return std::nullopt;
    }
    return NativeInputPath{NativeInputKind::hid_interface, path};
}

constexpr bool IsSimpleInputDeviceTarget(std::wstring_view target) noexcept {
    constexpr std::wstring_view prefix = L"\\Device\\";
    if (!StartsInputPath(target, prefix) || target.size() == prefix.size()) {
        return false;
    }
    const auto object = target.substr(prefix.size());
    // Volumes and filesystem/named-pipe paths are never input devices.
    if (StartsInputPath(object, L"Harddisk") ||
        StartsInputPath(object, L"Volume") ||
        EqualInputPath(object, L"NamedPipe") ||
        EqualInputPath(object, L"Mailslot")) {
        return false;
    }
    for (const auto value : object) {
        if (value == L'\\' || value == L'/' || value == L':' || value < L' ') {
            return false;
        }
    }
    return true;
}

} // namespace tdur::mod

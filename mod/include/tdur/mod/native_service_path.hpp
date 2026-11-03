#pragma once

#include <optional>
#include <string_view>

namespace tdur::mod {

enum class NativeServiceKind { afd_endpoint, mmcss_thread };

// Only these complete native Windows service names are accepted. A root
// handle, DOS alias, suffix, embedded NUL or traversal is never equivalent.
constexpr std::optional<NativeServiceKind>
ClassifyNativeServicePath(std::wstring_view path,
                          bool hasRootDirectory) noexcept {
    if (hasRootDirectory) {
        return std::nullopt;
    }
    if (path == L"\\Device\\Afd\\Endpoint") {
        return NativeServiceKind::afd_endpoint;
    }
    if (path == L"\\Device\\MMCSS\\MmThread") {
        return NativeServiceKind::mmcss_thread;
    }
    return std::nullopt;
}

} // namespace tdur::mod

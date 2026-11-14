#pragma once

#include <cstddef>
#include <string_view>

namespace tdur::mod {

// True when one component of `path` is exactly "savegame"
// (ASCII case-insensitive), the folder the game uses for its profile list and
// its saves, under Documents or, as a fallback, under its own folder.
// Separators are '\\' and '/'; a longer or shorter name never matches. This
// is a policy of the observation log, not a routine of the game: it only
// selects which write-intent opens get a dedicated line.
constexpr bool HasSaveComponent(std::wstring_view path) noexcept {
    constexpr std::wstring_view component = L"savegame";
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = start;
        while (end < path.size() && path[end] != L'\\' && path[end] != L'/') {
            ++end;
        }
        if (end - start == component.size()) {
            bool equal = true;
            for (std::size_t index = 0; equal && index < component.size();
                 ++index) {
                wchar_t character = path[start + index];
                if (character >= L'A' && character <= L'Z') {
                    character = wchar_t(character - L'A' + L'a');
                }
                equal = character == component[index];
            }
            if (equal) {
                return true;
            }
        }
        start = end + 1;
    }
    return false;
}

} // namespace tdur::mod

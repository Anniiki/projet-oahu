#include "tdur/mod/save_path.hpp"

#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

using tdur::mod::HasSaveComponent;

// The decision is constant: checked by the compiler as well.
static_assert(HasSaveComponent(L"C:\\Documents\\savegame\\ProfileList.dat"));
static_assert(!HasSaveComponent(L"C:\\Documents\\savegames\\ProfileList.dat"));

int main() {
    unsigned checks = 0;
    const auto require = [&](bool condition, const wchar_t* path) {
        ++checks;
        if (!condition) {
            std::wcerr << L"Save path decision mismatch: " << path << L'\n';
            throw std::runtime_error("Save path classification mismatch");
        }
    };
    try {
        for (const auto* path :
             {L"C:\\Documents\\Test Drive Unlimited\\savegame\\ProfileList.dat",
              L"C:\\Documents\\Test Drive Unlimited\\savegame",
              L"C:\\Documents\\Test Drive Unlimited\\savegame\\",
              L"C:\\Games\\copy\\game\\savegame\\Oahu\\playersave",
              L"savegame\\ProfileList.dat", L"savegame", L"savegame\\",
              L"\\??\\C:\\game\\savegame\\ProfileList.dat",
              L"C:\\game\\SaveGame\\ProfileList.dat", L"C:\\game\\SAVEGAME",
              L"C:/game/savegame/ProfileList.dat", L"C:\\game/savegame",
              L"savegame/x", L"\\savegame", L"C:\\savegame\\savegames\\x"}) {
            require(HasSaveComponent(path), path);
        }
        for (const auto* path :
             {L"", L"\\", L"C:\\", L"C:\\game\\savegames\\ProfileList.dat",
              L"C:\\game\\xsavegame\\ProfileList.dat",
              L"C:\\game\\savegame.bak", L"C:\\game\\savegam",
              L"C:\\game\\savegame.dat\\x", L"C:\\game\\save game\\x",
              L"C:\\game\\savegame \\x", L"C:\\game\\ savegame\\x",
              L"savegames", L"avegame", L"C:\\game\\save\\game",
              L"C:\\game\\s\u00e0vegame\\x", L"C:\\game\\savegame:stream"}) {
            require(!HasSaveComponent(path), path);
        }
        // An embedded NUL ends no component: the view's length decides.
        constexpr wchar_t embedded[] = L"C:\\game\\savegame\0suffix\\x";
        require(!HasSaveComponent(
                    std::wstring_view(embedded, std::size(embedded) - 1)),
                L"embedded NUL after the name");
        constexpr wchar_t prefixed[] = L"C:\\game\\x\0\\savegame\\x";
        require(HasSaveComponent(
                    std::wstring_view(prefixed, std::size(prefixed) - 1)),
                L"embedded NUL in another component");
        std::cout << "PASS " << checks << " save path component checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

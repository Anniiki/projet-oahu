#include "tdur/mod/native_service_path.hpp"

#include <iostream>
#include <iterator>
#include <stdexcept>

using tdur::mod::ClassifyNativeServicePath;

int main() {
    unsigned checks = 0;
    const auto require = [&](bool condition) {
        ++checks;
        if (!condition) {
            throw std::runtime_error(
                "Native Windows service classification mismatch");
        }
    };
    try {
        require(ClassifyNativeServicePath(L"\\Device\\Afd\\Endpoint", false)
                    .has_value());
        require(ClassifyNativeServicePath(L"\\Device\\MMCSS\\MmThread", false)
                    .has_value());
        require(!ClassifyNativeServicePath(L"\\Device\\Afd\\Endpoint", true)
                     .has_value());
        require(!ClassifyNativeServicePath(L"\\Device\\MMCSS\\MmThread", true)
                     .has_value());
        for (const auto* path :
             {L"C:\\Documents\\Device\\Afd\\Endpoint",
              L"\\??\\C:\\Device\\Afd\\Endpoint",
              L"\\??\\GLOBALROOT\\Device\\Afd\\Endpoint",
              L"\\\\.\\Afd\\Endpoint",
              L"\\Device\\Afd\\Endpoint\\..\\Documents",
              L"\\Device\\Afd\\Endpoint\\file", L"\\Device\\Afd\\Endpoint ",
              L"\\Device\\Afd\\EndpointX", L"\\Device\\MMCSS\\MmThread\\file",
              L"\\Device\\OtherService", L"", L"Endpoint"}) {
            require(!ClassifyNativeServicePath(path, false).has_value());
        }
        constexpr wchar_t embedded[] = L"\\Device\\Afd\\Endpoint\0suffix";
        require(
            !ClassifyNativeServicePath(
                 std::wstring_view(embedded, std::size(embedded) - 1), false)
                 .has_value());
        std::cout << "PASS " << checks
                  << " exact native Windows service path checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "tdur/mod/native_input_path.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace tdur::mod;

int main() {
    unsigned checks = 0;
    const auto require = [&](bool condition) {
        ++checks;
        if (!condition) {
            throw std::runtime_error(
                "Native input path classification mismatch");
        }
    };
    try {
        constexpr std::wstring_view hid =
            L"\\??\\hid#vid_046d&pid_c53a&mi_00#a&1ca88057&0&0000#{4d1e55b2-"
            L"f16f-11cf-88cb-001111000030}";
        require(ClassifyNativeInputPath(hid).has_value());
        require(
            ClassifyNativeInputPath(std::wstring(hid) + L"\\kbd").has_value());
        require(
            ClassifyNativeInputPath(L"\\??\\HID#HID_DEVICE_SYSTEM_VHF#A&0000#{"
                                    L"4D1E55B2-F16F-11CF-88CB-001111000030}")
                .has_value());
        require(
            ClassifyNativeInputPath(L"\\Device\\KeyboardClass0").has_value());
        require(
            ClassifyNativeInputPath(L"\\Device\\PointerClass12").has_value());
        require(!ClassifyNativeInputPath(L"C:\\Documents\\hid#fixture")
                     .has_value());
        require(!ClassifyNativeInputPath(L"\\??\\C:\\Documents\\save.sav")
                     .has_value());
        require(!ClassifyNativeInputPath(hid.substr(4)).has_value());
        require(!ClassifyNativeInputPath(std::wstring(hid) +
                                         L"\\..\\Documents\\save.sav")
                     .has_value());
        require(!ClassifyNativeInputPath(std::wstring(hid) + L"\\other")
                     .has_value());
        require(!ClassifyNativeInputPath(L"\\Device\\KeyboardClass0\\file")
                     .has_value());
        require(
            !ClassifyNativeInputPath(L"\\Device\\KeyboardClass").has_value());
        require(IsSimpleInputDeviceTarget(L"\\Device\\000000ab"));
        require(IsSimpleInputDeviceTarget(L"\\Device\\KeyboardClass0"));
        require(!IsSimpleInputDeviceTarget(L"\\Device\\HarddiskVolume3"));
        require(!IsSimpleInputDeviceTarget(
            L"\\Device\\HarddiskVolume3\\Documents\\save.sav"));
        require(!IsSimpleInputDeviceTarget(L"\\Device\\NamedPipe"));
        require(!IsSimpleInputDeviceTarget(L"\\??\\C:\\Documents\\save.sav"));
        std::cout << "PASS " << checks
                  << " native-input path checks; no device opened\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

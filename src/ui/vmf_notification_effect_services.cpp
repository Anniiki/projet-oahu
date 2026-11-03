#include "tdur/ui/vmf_notification_effect_services.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace tdur::ui {
std::expected<VmfNotificationTime, VmfNotificationStateError>
VmfNotificationWindowsClock::get_local_time() noexcept {
#if defined(_WIN32)
    SYSTEMTIME value;
    GetLocalTime(&value);
    return VmfNotificationTime{
        value.wYear, value.wMonth,  value.wDayOfWeek, value.wDay,
        value.wHour, value.wMinute, value.wSecond,    value.wMilliseconds};
#else
    return std::unexpected(VmfNotificationStateError{
        VmfNotificationStateErrorCode::host_failure,
        "Windows local clock is unavailable on this platform"});
#endif
}
} // namespace tdur::ui

#pragma once

#include <windows.h>

// Called only from this statically imported observer's process attach.
// Config is process-local environment supplied before process creation.
bool TdurInstallObservationHooks() noexcept;
void TdurObservationRecord(const char* event, DWORD error,
                           const wchar_t* path = nullptr,
                           const char* reason = nullptr,
                           const void* caller = nullptr) noexcept;

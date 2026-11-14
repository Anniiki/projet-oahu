#pragma once

#include <windows.h>

// Called only from this statically imported observer's process attach.
// Config is process-local environment supplied before process creation.
bool TdurInstallObservationHooks() noexcept;
bool TdurObservationFrametimesEnabled() noexcept;
void TdurObservationRecord(const char* event, DWORD error,
                           const wchar_t* path = nullptr,
                           const char* reason = nullptr,
                           const void* caller = nullptr) noexcept;

// Shell-free save folder deletion (new policy, see
// docs/evidence/shell_delete_without_shell.md). The save root is
// <TDUR_PRIVATE_DOCUMENTS>\Test Drive Unlimited\savegame in canonical form;
// false when the source Documents or the source installation lies at or
// under it.
bool TdurPrivateSaveRoot(wchar_t* root, size_t capacity) noexcept;
// True when `path`, canonicalized as by the write guard, lies strictly
// under the save root; `canonical` then receives that form.
bool TdurPrivateSaveTarget(const wchar_t* path, wchar_t* canonical,
                           size_t capacity) noexcept;
// Internal operation scope of the write guard: the deletion engine checks
// every object it opens itself.
bool TdurEnterInternalOperation() noexcept;
void TdurLeaveInternalOperation(bool previous) noexcept;
bool TdurInstallShellDeleteHooks() noexcept;

#pragma once

// Last-chance crash signal for the owned launcher. Called only from
// the observer's process attach, after TdurInstallObservationHooks.
// Without TDUR_CRASH_CHANNEL the handler stays disabled and returns true.
bool TdurInstallCrashHandler() noexcept;

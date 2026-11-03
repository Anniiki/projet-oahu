#pragma once

// Called at initial process attach, after observation is configured. Only an
// already-loaded public D3D9 factory entry and the optional audited retail
// renderer admission entry are armed here. Runtime/device work and file
// identity checks run outside DllMain.
// A missing/zero TDUR_D3D12_DISPLAY does not install anything.
bool TdurInstallDisplayStartupGate() noexcept;

#pragma once

#include <d3d9.h>

// Explicit maximum-at-startup policy for the audited private retail image.
// These callbacks run outside DllMain, never on the ordinary Reset path.
HRESULT TdurApplyInitialVideoDefaults(IDirect3DDevice9*) noexcept;
void TdurSealInitialVideoDefaults() noexcept;

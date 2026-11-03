#pragma once

#include <d3d9.h>

// One startup-only compatibility bridge for the audited private retail image.
// Does not change the global activation flag or any manual-pause callback.
HRESULT TdurApplyInitialBackgroundPolicy(IDirect3DDevice9*) noexcept;
void TdurSealInitialBackgroundPolicy() noexcept;

// Queue only the fixed, signature-checked entry gate in the caller's pre-entry
// Detours transaction. No file I/O, device work or thread enumeration here.
LONG TdurQueueRetailRendererGate(bool& queued) noexcept;
bool TdurVerifyRetailRendererGate() noexcept;

// Entered outside loader lock by the audited initializer itself. The gate
// forwards its exact native calling convention and always leaves on unwind.
bool TdurEnterInitialRendererPhase(void* renderer, void* parent,
                                   void* return_address) noexcept;
void TdurLeaveInitialRendererPhase() noexcept;

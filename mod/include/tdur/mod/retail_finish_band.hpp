#pragma once

#include <d3d9.h>

// Finish-line gearbox band of the audited private retail image.
// At the finish line the game applies an AI personality to the local
// player's car (CALL 0095A679 -> 0090C460), which leaves the band at
// [physics+41F0] = 0.3. The call is redirected to a stub of this module that
// gives the local car its installation band back after the original call.
// Applied once, in the admitted initial renderer phase; every refusal is
// logged and keeps the original behaviour. Never fatal for the game.
HRESULT TdurApplyFinishBandFix(IDirect3DDevice9*) noexcept;
void TdurSealFinishBandFix() noexcept;

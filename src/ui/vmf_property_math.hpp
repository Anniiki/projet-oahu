#pragma once

#include "tdur/ui/vmf_transform.hpp"

namespace tdur::ui::detail {

// Native442D00/442D20/442D40 math, separate from flags and propagation.
// No allocation or pointer borrowed from the original executable.
// Transcendentals use the C++ double runtime, not MSVCR71/_CIacos or an x87
// emulator. Native evaluation order, binary32 stores and degree/radian quirk
// are retained; bit-identical x87 rounding/NaN payloads are not promised.
void set_vmf_property_x_scale(VmfTransform &local, float value) noexcept;
void set_vmf_property_y_scale(VmfTransform &local, float value) noexcept;
void set_vmf_property_rotation(VmfTransform &local, float degrees) noexcept;

} // namespace tdur::ui::detail

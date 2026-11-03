#pragma once

#include "tdur/ui/vmf_display_runtime.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

enum class VmfHostVariableErrorCode {
    text_limit,
    non_ascii_text,
    integer_range,
    missing_services,
    missing_variable_name,
    display,
    variables,
};

struct VmfHostVariableError {
    VmfHostVariableErrorCode code{};
    std::string_view detail;
    std::optional<VmfDisplayRuntimeError> display_error;
    std::optional<VmfVariableError> variable_error;
};

// The successful path of native440E00 calls MSVCR71::atoi, not the VMF
// floating-point converter. Reconstruct its defined int32 ASCII domain:
// C whitespace, optional sign, decimal digits, stop at the first non-digit.
// A first NUL or view end terminates input. No digits yields zero.
// The native local text buffer is 32 bytes including NUL. Oversized prefixes,
// non-ASCII text and integer overflow are explicit modern refusals; this
// does not establish historical CRT overflow or non-ASCII locale behavior.
[[nodiscard]] std::expected<std::int32_t, VmfHostVariableError>
vmf_host_integer_from_text(std::string_view text) noexcept;

// Host440E00 selects the supplied movie's ROOT, independently of a script's
// current target. Use this display's owned dictionary and native4491A0 path
// classification/resolution. Missing target/variable yields zero; scope0 and
// a present empty key are valid. A target-only expression resolves to native
// NULL name and is refused here before its unsafe native hash/lookup.
// No stack, frame, current target, variable or cleanup flag is changed.
// Per-display state removes the original global switching and mutex calls;
// serialize this call with every access to the display/dictionary/backing text.
// No ordinary heap allocation or callback; a closed display/missing backend
// remains an explicit error. Diagnostic logging is disabled.
[[nodiscard]] std::expected<std::int32_t, VmfHostVariableError>
read_vmf_host_integer(VmfDisplayRuntime& display, std::string_view expression,
                      VmfTargetLookupLimits limits = {}) noexcept;

// Native440B00 copies text (31 stored bytes);440AB0 keeps a live text link.
// Both resolve from the supplied movie's ROOT through4491A0, then use the
// owned variable setters. A missing path is a native no-op. A target-only
// expression or full table still arms that target's cleanup flag200.
// Empty names are real keys. Modern errors stop before any write/flag change.
// Links retain their backing; their resource limit remains explicit.
// These operations do not change a suspended script, stack or frame state.
[[nodiscard]] std::expected<void, VmfHostVariableError>
write_vmf_host_text(VmfDisplayRuntime& display, std::string_view expression,
                    std::string_view text,
                    VmfTargetLookupLimits limits = {}) noexcept;
[[nodiscard]] std::expected<void, VmfHostVariableError>
write_vmf_host_linked_text(VmfDisplayRuntime& display,
                           std::string_view expression,
                           const VmfTextReference& text,
                           VmfTargetLookupLimits limits = {}) noexcept;

// Native440BA0 stores sprintf("%d", int32) as TEXT via443770; it does not
// store a float or a typed number. Decimal conversion is locale independent
// and exact for the full int32 domain, including values not exact in float.
[[nodiscard]] std::expected<void, VmfHostVariableError>
write_vmf_host_integer(VmfDisplayRuntime& display, std::string_view expression,
                       std::int32_t value,
                       VmfTargetLookupLimits limits = {}) noexcept;

// Read-only prefix9AB950..9ABA13: preserve initial flags, set0x1, then add
// masks0x2..0x200 when each corresponding root integer is EXACTLY1. An error
// returns no partial mask. This is only the selection mask: it does not
// execute the following data bindings, locale setup, tutorial or sbdd.
[[nodiscard]] std::expected<std::uint32_t, VmfHostVariableError>
read_vmf_menu_binding_mask(VmfDisplayRuntime& display,
                           std::uint32_t initial_flags,
                           VmfTargetLookupLimits limits = {}) noexcept;

} // namespace tdur::ui

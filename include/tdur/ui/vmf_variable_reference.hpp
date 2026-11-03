#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

struct VmfVariableReference {
    std::string_view expression; // Prefix before the first NUL or view end.
    // Absence selects the caller's current target without a target lookup.
    // When present, pass the WHOLE expression to the target resolver.
    std::optional<std::string_view> target_path;
    // Absence is native NULL (target-only), distinct from a present empty name.
    std::optional<std::string_view> variable_name;
};

enum class VmfVariableReferenceError { text_limit };

// Classifies the name/target inputs of native4491A0; does not resolve a target,
// touch a dictionary, pop a stack, set flags or perform diagnostic logging.
// ':' takes priority over '/', wherever it occurs before the first NUL.
// All views borrow input. Keep its backing alive and unchanged until consumed.
// A view end acts as NUL. max_bytes counts the entire prefix, excluding NUL,
// including a suffix the target resolver may ignore. No allocation or locale.
[[nodiscard]] std::expected<VmfVariableReference, VmfVariableReferenceError>
classify_vmf_variable_reference(std::string_view input,
                                std::size_t max_bytes = 64U * 1024U) noexcept;

} // namespace tdur::ui

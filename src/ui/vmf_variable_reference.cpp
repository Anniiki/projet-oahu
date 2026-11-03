#include "tdur/ui/vmf_variable_reference.hpp"

namespace tdur::ui {

std::expected<VmfVariableReference, VmfVariableReferenceError>
classify_vmf_variable_reference(std::string_view input,
                                std::size_t max_bytes) noexcept {
    std::size_t length = 0;
    while (length < input.size() && input[length] != '\0') {
        if (length == max_bytes)
            return std::unexpected(VmfVariableReferenceError::text_limit);
        ++length;
    }
    input = input.substr(0, length);
    VmfVariableReference result{input, std::nullopt, std::nullopt};
    if (const auto colon = input.find(':'); colon != std::string_view::npos) {
        result.target_path = input;
        result.variable_name = input.substr(colon + 1);
    } else if (input.find('/') != std::string_view::npos) {
        result.target_path = input;
    } else {
        result.variable_name = input;
    }
    return result;
}

} // namespace tdur::ui

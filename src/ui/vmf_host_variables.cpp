#include "tdur/ui/vmf_host_variables.hpp"

#include "tdur/ui/vmf_variable_reference.hpp"

#include <array>
#include <charconv>
#include <limits>

namespace tdur::ui {
namespace {

VmfHostVariableError error(VmfHostVariableErrorCode code,
                           std::string_view detail) noexcept {
    return {code, detail, {}, {}};
}

bool ascii_space(unsigned char value) noexcept {
    return value == ' ' || (value >= '\t' && value <= '\r');
}

template <class Assign>
std::expected<void, VmfHostVariableError>
write_root_variable(VmfDisplayRuntime& display, std::string_view expression,
                    VmfTargetLookupLimits limits, Assign&& assign) noexcept {
    const auto root = display.root();
    const auto live = display.inspect_instance(root);
    if (!live)
        return std::unexpected(
            VmfHostVariableError{VmfHostVariableErrorCode::display,
                                 "VMF host write movie is not live",
                                 live.error(),
                                 {}});
    if (!display.variable_dictionary())
        return std::unexpected(error(VmfHostVariableErrorCode::missing_services,
                                     "VMF host write needs owned variables"));
    const auto reference =
        classify_vmf_variable_reference(expression, limits.max_path_bytes);
    if (!reference)
        return std::unexpected(
            error(VmfHostVariableErrorCode::text_limit,
                  "VMF host write expression exceeds quota"));
    auto selected = root;
    if (reference->target_path) {
        const auto resolved =
            display.resolve_target(root, *reference->target_path, limits);
        if (!resolved)
            return std::unexpected(
                VmfHostVariableError{VmfHostVariableErrorCode::display,
                                     "VMF host write target rejected",
                                     resolved.error(),
                                     {}});
        if (!resolved->found)
            return {};
        selected = resolved->target.value_or(VmfInstanceHandle{});
    }
    const auto stored = assign(selected, reference->variable_name);
    if (!stored)
        return std::unexpected(
            VmfHostVariableError{VmfHostVariableErrorCode::display,
                                 "VMF host variable write rejected",
                                 stored.error(),
                                 {}});
    return {};
}

} // namespace

std::expected<std::int32_t, VmfHostVariableError>
vmf_host_integer_from_text(std::string_view text) noexcept {
    std::size_t length = 0;
    while (length < text.size() && text[length] != '\0') {
        if (length == 31)
            return std::unexpected(
                error(VmfHostVariableErrorCode::text_limit,
                      "VMF host integer exceeds its 32-byte text buffer"));
        if (static_cast<unsigned char>(text[length]) > 127)
            return std::unexpected(
                error(VmfHostVariableErrorCode::non_ascii_text,
                      "VMF host integer non-ASCII locale is not established"));
        ++length;
    }
    text = text.substr(0, length);
    std::size_t offset = 0;
    while (offset < text.size() &&
           ascii_space(static_cast<unsigned char>(text[offset])))
        ++offset;
    bool negative = false;
    if (offset < text.size() && (text[offset] == '+' || text[offset] == '-')) {
        negative = text[offset] == '-';
        ++offset;
    }
    const std::uint32_t limit = negative ? 0x80000000U : 0x7fffffffU;
    std::uint32_t magnitude = 0;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
        const auto digit = static_cast<std::uint32_t>(text[offset] - '0');
        if (magnitude > (limit - digit) / 10U)
            return std::unexpected(
                error(VmfHostVariableErrorCode::integer_range,
                      "VMF host integer exceeds the proved int32 domain"));
        magnitude = magnitude * 10U + digit;
        ++offset;
    }
    if (!negative)
        return static_cast<std::int32_t>(magnitude);
    if (magnitude == 0x80000000U)
        return std::numeric_limits<std::int32_t>::min();
    return -static_cast<std::int32_t>(magnitude);
}

std::expected<std::int32_t, VmfHostVariableError>
read_vmf_host_integer(VmfDisplayRuntime& display, std::string_view expression,
                      VmfTargetLookupLimits limits) noexcept {
    const auto root = display.root();
    const auto live = display.inspect_instance(root);
    if (!live)
        return std::unexpected(
            VmfHostVariableError{VmfHostVariableErrorCode::display,
                                 "VMF host integer movie is not live",
                                 live.error(),
                                 {}});
    const auto* variables = display.variable_dictionary();
    if (!variables)
        return std::unexpected(error(VmfHostVariableErrorCode::missing_services,
                                     "VMF host integer needs owned variables"));
    const auto reference =
        classify_vmf_variable_reference(expression, limits.max_path_bytes);
    if (!reference)
        return std::unexpected(
            error(VmfHostVariableErrorCode::text_limit,
                  "VMF host integer expression exceeds quota"));
    auto selected = root;
    if (reference->target_path) {
        const auto resolved =
            display.resolve_target(root, *reference->target_path, limits);
        if (!resolved)
            return std::unexpected(
                VmfHostVariableError{VmfHostVariableErrorCode::display,
                                     "VMF host integer target rejected",
                                     resolved.error(),
                                     {}});
        if (!resolved->found)
            return 0;
        selected = resolved->target.value_or(VmfInstanceHandle{});
    }
    const auto instance = display.inspect_instance(selected);
    if (!instance)
        return std::unexpected(
            VmfHostVariableError{VmfHostVariableErrorCode::display,
                                 "VMF host integer target is not live",
                                 instance.error(),
                                 {}});
    if (instance->kind != VmfInstanceKind::timeline || !instance->scope_id)
        return std::unexpected(VmfHostVariableError{
            VmfHostVariableErrorCode::display,
            "VMF host integer target lacks a timeline scope",
            VmfDisplayRuntimeError{
                instance->kind != VmfInstanceKind::timeline
                    ? VmfDisplayRuntimeErrorCode::wrong_kind
                    : VmfDisplayRuntimeErrorCode::invalid_native_state,
                "VMF host integer target lacks a timeline scope"},
            {}});
    if (!reference->variable_name)
        return std::unexpected(
            error(VmfHostVariableErrorCode::missing_variable_name,
                  "VMF host integer refuses native NULL variable name"));
    std::array<char, 32> text{};
    const auto copied = variables->copy_text(*instance->scope_id,
                                             *reference->variable_name, text);
    if (!copied)
        return std::unexpected(
            VmfHostVariableError{VmfHostVariableErrorCode::variables,
                                 "VMF host integer variable text rejected",
                                 {},
                                 copied.error()});
    if (!*copied)
        return 0;
    return vmf_host_integer_from_text(text.data());
}

std::expected<void, VmfHostVariableError>
write_vmf_host_text(VmfDisplayRuntime& display, std::string_view expression,
                    std::string_view text,
                    VmfTargetLookupLimits limits) noexcept {
    return write_root_variable(
        display, expression, limits,
        [&](VmfInstanceHandle target,
            std::optional<std::string_view> name) noexcept {
            return display.set_variable_text(target, name, text);
        });
}

std::expected<void, VmfHostVariableError> write_vmf_host_linked_text(
    VmfDisplayRuntime& display, std::string_view expression,
    const VmfTextReference& text, VmfTargetLookupLimits limits) noexcept {
    return write_root_variable(
        display, expression, limits,
        [&](VmfInstanceHandle target,
            std::optional<std::string_view> name) noexcept {
            return display.set_variable_linked(target, name, text);
        });
}

std::expected<void, VmfHostVariableError>
write_vmf_host_integer(VmfDisplayRuntime& display, std::string_view expression,
                       std::int32_t value,
                       VmfTargetLookupLimits limits) noexcept {
    std::array<char, 32> text{};
    const auto converted =
        std::to_chars(text.data(), text.data() + text.size(), value);
    if (converted.ec != std::errc{})
        return std::unexpected(error(VmfHostVariableErrorCode::integer_range,
                                     "VMF host decimal formatting rejected"));
    return write_vmf_host_text(
        display, expression,
        std::string_view{text.data(),
                         static_cast<std::size_t>(converted.ptr - text.data())},
        limits);
}

std::expected<std::uint32_t, VmfHostVariableError>
read_vmf_menu_binding_mask(VmfDisplayRuntime& display,
                           std::uint32_t initial_flags,
                           VmfTargetLookupLimits limits) noexcept {
    constexpr std::array<std::string_view, 9> names{
        "db_hud", "db_hud_cha", "db_map",      "db_home", "db_challenge",
        "db_gme", "db_shops",   "db_drive_in", "db_ice"};
    auto flags = initial_flags | 1U;
    std::uint32_t bit = 2;
    for (const auto name : names) {
        const auto value = read_vmf_host_integer(display, name, limits);
        if (!value)
            return std::unexpected(value.error());
        if (*value == 1)
            flags |= bit;
        bit <<= 1U;
    }
    return flags;
}

} // namespace tdur::ui

#include "tdur/ui/vmf_execution.hpp"
#include "tdur/ui/vmf_script_context.hpp"
#include "tdur/ui/vmf_variable_reference.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <emmintrin.h>
#include <utility>
#include <xmmintrin.h>

namespace tdur::ui {
namespace {
bool truth(__m128 value) noexcept {
    return (_mm_movemask_ps(_mm_cmpneq_ss(value, _mm_setzero_ps())) & 1) != 0;
}
std::uint32_t bits(__m128 value) noexcept {
    return static_cast<std::uint32_t>(
        _mm_cvtsi128_si32(_mm_castps_si128(value)));
}

struct Runner {
    const assets::VmfMovie& movie;
    const assets::VmfScript& script;
    VmfExecutionCursor& cursor;
    VmfStack& stack;
    VmfExecutionLimits limits;
    VmfScriptSnapshot* host{};
    std::size_t executed{};
    std::size_t text_used{};
    std::uint16_t opcode{};
    std::size_t skipped{};

    VmfExecutionError fail(VmfExecutionErrorCode code, std::string_view detail,
                           std::optional<VmfStackError> nested = {}) noexcept {
        cursor.faulted = true;
        if (host)
            host->phase = VmfScriptPhase::faulted;
        return {code,
                cursor.script_index,
                cursor.next_action,
                executed,
                opcode,
                nested,
                detail,
                {},
                skipped,
                text_used};
    }
    VmfExecutionError stack_failure(VmfStackError error) noexcept {
        return fail(VmfExecutionErrorCode::stack, "VMF operand stack error",
                    error);
    }
    bool charge(std::size_t bytes) noexcept {
        if (bytes > limits.text_bytes - text_used)
            return false;
        text_used += bytes;
        return true;
    }
    VmfExecutionError text_failure() noexcept {
        return fail(VmfExecutionErrorCode::text_limit,
                    "VMF text budget exceeded");
    }
    std::expected<float, VmfExecutionError> number() noexcept {
        if (const auto* top = stack.peek()) {
            const auto reference = top->text_reference();
            if (reference && !charge(reference->text().size()))
                return std::unexpected(text_failure());
        }
        auto value = stack.pop_number();
        if (!value)
            return std::unexpected(stack_failure(value.error()));
        return *value;
    }
    std::expected<VmfTextReference, VmfExecutionError> text() noexcept {
        auto value = stack.pop_text();
        if (!value)
            return std::unexpected(stack_failure(value.error()));
        return *value;
    }
    VmfExecutionReport report(VmfExecutionStatus status) const noexcept {
        return {status, executed, text_used, opcode, skipped};
    }
};
std::expected<VmfExecutionReport, VmfExecutionError>
execute(const assets::VmfMovie &movie, VmfExecutionCursor &cursor,
        VmfStack &stack, VmfExecutionLimits limits,
        VmfDisplayRuntime *display = nullptr, VmfInstanceHandle target = {},
        VmfScriptSnapshot *host = nullptr,
        VmfTargetLookupLimits target_limits = {},
        const VmfVariables *variables = nullptr, bool write_variables = false,
        bool properties = false) noexcept {
    const auto invalid = [&](VmfExecutionErrorCode code,
                             std::string_view detail) {
        cursor.faulted = true;
        if (host)
            host->phase = VmfScriptPhase::faulted;
        return std::unexpected(VmfExecutionError{
            code, cursor.script_index, cursor.next_action, 0, 0, {}, detail});
    };
    if (cursor.faulted || cursor.script_index >= movie.scripts().size())
        return invalid(VmfExecutionErrorCode::invalid_cursor,
                       "VMF cursor is faulted or names no script");
    const auto& script = movie.scripts()[cursor.script_index];
    if (cursor.next_action >= script.action_count)
        return invalid(VmfExecutionErrorCode::invalid_cursor,
                       "VMF cursor outside its script");
    if (limits.text_bytes > 16'777'216)
        return invalid(VmfExecutionErrorCode::invalid_limits,
                       "VMF text budget exceeds exact StringLength domain");
    Runner runner{movie, script, cursor, stack, limits, host};
    if (cursor.ended) {
        if (movie.actions()[script.first_action + cursor.next_action].opcode !=
            0)
            return invalid(VmfExecutionErrorCode::invalid_cursor,
                           "VMF ended cursor does not point to End");
        return runner.report(VmfExecutionStatus::ended);
    }

    for (;;) {
        const auto& action =
            movie.actions()[script.first_action + cursor.next_action];
        runner.opcode = action.opcode;
        if (host && host->phase == VmfScriptPhase::skip_to_target) {
            if (action.opcode == 0 || action.opcode == 0x8b ||
                action.opcode == 0x20) {
                host->phase = VmfScriptPhase::execute;
            } else {
                // Ignore even control flow and host actions while scanning.
                // This budget is independent of the dispatch budget.
                if (runner.skipped == limits.skipped_actions)
                    return runner.report(VmfExecutionStatus::scan_limit);
                ++runner.skipped;
                if (cursor.next_action + 1 >= script.action_count)
                    return std::unexpected(
                        runner.fail(VmfExecutionErrorCode::invalid_program,
                                    "VMF target skip lacks End"));
                ++cursor.next_action;
                continue;
            }
        }
        if (runner.executed == limits.instructions)
            return runner.report(VmfExecutionStatus::instruction_limit);
        auto next = cursor.next_action + 1;
        std::expected<void, VmfStackError> pushed;
        switch (action.opcode) {
        case 0x00:
            ++runner.executed;
            cursor.ended = true;
            if (host) {
                host->current = host->origin;
                host->phase = VmfScriptPhase::ended;
            }
            return runner.report(VmfExecutionStatus::ended);
        case 0x96: {
            if (!action.strings[0])
                return std::unexpected(
                    runner.fail(VmfExecutionErrorCode::invalid_program,
                                "VMF Push has no string"));
            const auto data = movie.bytes(*action.strings[0]);
            if (!data)
                return std::unexpected(
                    runner.fail(VmfExecutionErrorCode::invalid_program,
                                "VMF Push range invalid"));
            const std::string_view text(
                reinterpret_cast<const char*>(data->data()), data->size());
            if (!runner.charge(text.size()))
                return std::unexpected(runner.text_failure());
            pushed = stack.push_text(text);
            break;
        }
        case 0x17:
            stack.discard();
            break;
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11: {
            const auto b = runner.number();
            if (!b)
                return std::unexpected(b.error());
            const auto a = runner.number();
            if (!a)
                return std::unexpected(a.error());
            const auto left = _mm_set_ss(*a);
            auto right = _mm_set_ss(*b);
            switch (action.opcode) {
            case 0x0a:
                pushed = stack.push_number_bits(bits(_mm_add_ss(left, right)));
                break;
            case 0x0b:
                pushed = stack.push_number_bits(bits(_mm_sub_ss(left, right)));
                break;
            case 0x0c:
                pushed = stack.push_number_bits(bits(_mm_mul_ss(left, right)));
                break;
            case 0x0d: {
                const auto epsilon =
                    _mm_set_ss(std::bit_cast<float>(0x33d6bf95U));
                if (_mm_comige_ss(epsilon, right))
                    right = epsilon;
                pushed = stack.push_number_bits(bits(_mm_div_ss(left, right)));
                break;
            }
            default: {
                bool value = false;
                if (action.opcode == 0x0e)
                    value =
                        (_mm_movemask_ps(_mm_cmpeq_ss(left, right)) & 1) != 0;
                else if (action.opcode == 0x0f)
                    value =
                        (_mm_movemask_ps(_mm_cmplt_ss(left, right)) & 1) != 0;
                else if (action.opcode == 0x10)
                    value = truth(left) && truth(right);
                else
                    value = truth(left) || truth(right);
                pushed = stack.push_text(value ? "1" : "0");
                break;
            }
            }
            break;
        }
        case 0x12:
        case 0x18: {
            const auto value = runner.number();
            if (!value)
                return std::unexpected(value.error());
            const auto numeric = _mm_set_ss(*value);
            if (action.opcode == 0x12)
                pushed = stack.push_text(truth(numeric) ? "0" : "1");
            else
                pushed = stack.push_number_bits(bits(_mm_cvtsi32_ss(
                    _mm_setzero_ps(), _mm_cvttss_si32(numeric))));
            break;
        }
        case 0x13:
        case 0x21: {
            const auto b = runner.text();
            if (!b)
                return std::unexpected(b.error());
            const auto a = runner.text();
            if (!a)
                return std::unexpected(a.error());
            // Resolve AFTER both pops: the second number may overwrite the
            // first reference's shared scratch, as in the native routines.
            const auto left = a->text();
            const auto right = b->text();
            if (!runner.charge(left.size()) || !runner.charge(right.size()))
                return std::unexpected(runner.text_failure());
            if (action.opcode == 0x13) {
                pushed = stack.push_text(left == right ? "1" : "0");
            } else {
                std::array<char, 32> joined{};
                const auto first = std::min<std::size_t>(left.size(), 31);
                const auto second =
                    std::min<std::size_t>(right.size(), 31 - first);
                if (first)
                    std::copy_n(left.data(), first, joined.data());
                if (second)
                    std::copy_n(right.data(), second, joined.data() + first);
                pushed = stack.push_text({joined.data(), first + second});
            }
            break;
        }
        case 0x14: {
            const auto text = runner.text();
            if (!text)
                return std::unexpected(text.error());
            const auto length = text->text().size();
            if (!runner.charge(length))
                return std::unexpected(runner.text_failure());
            // Native FILD/FSTP uses x87. This bounded integer domain is exact
            // in binary32, so no MXCSR/CWx87 rounding substitution is made.
            pushed = stack.push_number_bits(bits(
                _mm_cvtsi32_ss(_mm_setzero_ps(), static_cast<int>(length))));
            break;
        }
        case 0x15: {
            const auto length_value = runner.number();
            if (!length_value)
                return std::unexpected(length_value.error());
            const auto index_value = runner.number();
            if (!index_value)
                return std::unexpected(index_value.error());
            const auto reference = runner.text();
            if (!reference)
                return std::unexpected(reference.error());
            const auto text = reference->text();
            if (!runner.charge(text.size()))
                return std::unexpected(runner.text_failure());
            const auto length = _mm_cvttss_si32(_mm_set_ss(*length_value));
            const auto index = _mm_cvttss_si32(_mm_set_ss(*index_value));
            const auto start =
                index > 0 ? static_cast<std::size_t>(index - 1) : 0;
            if (length < 0 || start > text.size())
                return std::unexpected(
                    runner.fail(VmfExecutionErrorCode::string_range,
                                "VMF substring outside safe range"));
            const auto count = std::min<std::size_t>(
                {static_cast<std::size_t>(length), text.size() - start, 31});
            pushed = stack.push_text(text.substr(start, count));
            break;
        }
        case 0x99:
        case 0x9d: {
            bool take = action.opcode == 0x99;
            if (!take && stack.size() != 0) {
                const auto condition = runner.number();
                if (!condition)
                    return std::unexpected(condition.error());
                take = truth(_mm_set_ss(*condition));
            }
            if (take) {
                if (!action.branch ||
                    action.branch->target_action_index < script.first_action ||
                    action.branch->target_action_index - script.first_action >=
                        script.action_count)
                    return std::unexpected(
                        runner.fail(VmfExecutionErrorCode::invalid_program,
                                    "VMF branch leaves script"));
                next = action.branch->target_action_index - script.first_action;
            }
            break;
        }
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x81: {
            if (!display)
                return runner.report(VmfExecutionStatus::needs_host);
            auto command = VmfTimelineAction::next_frame;
            std::uint16_t frame_index{};
            switch (action.opcode) {
            case 0x05:
                command = VmfTimelineAction::previous_frame;
                break;
            case 0x06:
                command = VmfTimelineAction::play;
                break;
            case 0x07:
                command = VmfTimelineAction::stop;
                break;
            case 0x81: {
                command = VmfTimelineAction::goto_frame;
                const auto payload = movie.bytes(action.payload);
                if (!payload || payload->size() != 2)
                    return std::unexpected(
                        runner.fail(VmfExecutionErrorCode::invalid_program,
                                    "VMF GotoFrame operand is not a WORD"));
                const auto first =
                    std::to_integer<std::uint16_t>((*payload)[0]);
                const auto second =
                    std::to_integer<std::uint16_t>((*payload)[1]);
                frame_index = static_cast<std::uint16_t>(
                    movie.header().profile == assets::VmfProfile::xbox_2_5
                        ? (first << 8U) | second
                        : first | (second << 8U));
                break;
            }
            default:
                break;
            }
            const auto applied = display->apply_timeline_action(
                host ? host->current.value_or(VmfInstanceHandle{}) : target,
                command, frame_index);
            if (!applied) {
                auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                         "VMF timeline action rejected");
                error.display_error = applied.error();
                return std::unexpected(error);
            }
            break;
        }
        case 0x8b:
        case 0x20:
        case 0x8c: {
            if (!host)
                return runner.report(VmfExecutionStatus::needs_host);
            const bool select = action.opcode != 0x8c;
            if (select)
                host->current = host->origin;
            // pop_text may release the stack's last owner of linked storage.
            // Keep the returned owner alive through charge and target lookup.
            std::optional<VmfTextReference> popped_text;
            std::string_view text;
            if (action.opcode == 0x20) {
                auto reference = runner.text();
                if (!reference)
                    return std::unexpected(reference.error());
                popped_text = std::move(*reference);
                text = popped_text->text();
            } else {
                if (!action.strings[0])
                    return std::unexpected(
                        runner.fail(VmfExecutionErrorCode::invalid_program,
                                    "VMF target/label action has no string"));
                const auto bytes = movie.bytes(*action.strings[0]);
                if (!bytes)
                    return std::unexpected(
                        runner.fail(VmfExecutionErrorCode::invalid_program,
                                    "VMF target/label text range invalid"));
                text = {reinterpret_cast<const char*>(bytes->data()),
                        bytes->size()};
            }
            // TellTarget has already reset the target and popped its text.
            // A modern quota error retains that prefix; it cannot be retried.
            if (!runner.charge(text.size()))
                return std::unexpected(runner.text_failure());
            if (select) {
                const auto result =
                    display->resolve_target(host->origin, text, target_limits);
                if (!result) {
                    auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                             "VMF target lookup rejected");
                    error.display_error = result.error();
                    return std::unexpected(error);
                }
                host->current = result->target;
                if (!result->found)
                    host->phase = VmfScriptPhase::skip_to_target;
            } else {
                const auto applied = display->apply_goto_label(
                    host->current.value_or(VmfInstanceHandle{}), text);
                if (!applied) {
                    auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                             "VMF label action rejected");
                    error.display_error = applied.error();
                    return std::unexpected(error);
                }
            }
            break;
        }
        case 0x1c: {
            if (!host || !variables)
                return runner.report(VmfExecutionStatus::needs_host);
            // Keep the popped owner alive: a name may alias the shared
            // numeric scratch or a stack arena that the result will reuse.
            const auto name = runner.text();
            if (!name)
                return std::unexpected(name.error());
            const auto text = name->text();
            if (!runner.charge(text.size()))
                return std::unexpected(runner.text_failure());
            const auto reference = classify_vmf_variable_reference(
                text, target_limits.max_path_bytes);
            if (!reference)
                return std::unexpected(runner.text_failure());
            // Unlike SetTarget/TellTarget, Get uses current by value. A
            // missing path neither clears current nor starts skip mode.
            auto selected = host->current.value_or(VmfInstanceHandle{});
            if (reference->target_path) {
                const auto resolved = display->resolve_target(
                    selected, *reference->target_path, target_limits);
                if (!resolved) {
                    auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                             "VMF variable target rejected");
                    error.display_error = resolved.error();
                    return std::unexpected(error);
                }
                if (!resolved->found) {
                    pushed = stack.push_text("");
                    break;
                }
                selected = resolved->target.value_or(VmfInstanceHandle{});
            }
            const auto instance = display->inspect_instance(selected);
            if (!instance || instance->kind != VmfInstanceKind::timeline ||
                !instance->scope_id) {
                auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                         "VMF variable scope rejected");
                error.display_error =
                    !instance
                        ? instance.error()
                        : VmfDisplayRuntimeError{
                              instance->kind != VmfInstanceKind::timeline
                                  ? VmfDisplayRuntimeErrorCode::wrong_kind
                                  : VmfDisplayRuntimeErrorCode::
                                        invalid_native_state,
                              "VMF variable target lacks a timeline scope"};
                return std::unexpected(error);
            }
            if (!reference->variable_name) {
                pushed = stack.push_sprite(vmf_sprite_identity(selected));
                break;
            }
            const auto found = variables->push(
                *instance->scope_id, *reference->variable_name, stack);
            if (!found) {
                const auto &nested = found.error();
                auto error = nested.code == VmfVariableErrorCode::stack &&
                                     nested.stack_error
                                 ? runner.stack_failure(*nested.stack_error)
                                 : runner.fail(VmfExecutionErrorCode::variables,
                                               "VMF variable lookup rejected");
                error.variable_error = nested;
                return std::unexpected(error);
            }
            if (!*found)
                pushed = stack.push_text("");
            break;
        }
        case 0x1d: {
            if (!host || !variables || !write_variables)
                return runner.report(VmfExecutionStatus::needs_host);
            const auto *top = stack.peek();
            const bool numeric = top && top->kind() == VmfValueKind::number;
            const bool linked = top && top->kind() == VmfValueKind::linked_text;
            std::uint32_t value_bits{};
            std::optional<VmfTextReference> value_text;
            if (numeric) {
                // 44B200's known type1 path is a bit load and pop. Avoid a
                // float-return ABI (notably x87 on x86) for signaling NaNs.
                value_bits = *top->number_bits();
                stack.discard();
            } else {
                auto value = runner.text();
                if (!value)
                    return std::unexpected(value.error());
                value_text = std::move(*value);
            }
            const auto name = runner.text();
            if (!name)
                return std::unexpected(name.error());
            // A numeric name may overwrite scratch referenced by the value.
            // Resolve and charge texts only after both native pops.
            if (value_text && !runner.charge(value_text->text().size()))
                return std::unexpected(runner.text_failure());
            const auto text = name->text();
            if (!runner.charge(text.size()))
                return std::unexpected(runner.text_failure());
            const auto reference = classify_vmf_variable_reference(
                text, target_limits.max_path_bytes);
            if (!reference)
                return std::unexpected(runner.text_failure());
            auto selected = host->current.value_or(VmfInstanceHandle{});
            if (reference->target_path) {
                const auto resolved = display->resolve_target(
                    selected, *reference->target_path, target_limits);
                if (!resolved) {
                    auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                             "VMF variable target rejected");
                    error.display_error = resolved.error();
                    return std::unexpected(error);
                }
                if (!resolved->found)
                    break;
                selected = resolved->target.value_or(VmfInstanceHandle{});
            }
            const auto assigned =
                numeric ? display->set_variable_number_bits(
                              selected, reference->variable_name, value_bits)
                : linked ? display->set_variable_linked(
                               selected, reference->variable_name, *value_text)
                         : display->set_variable_text(selected,
                                                      reference->variable_name,
                                                      value_text->text());
            if (!assigned) {
                const auto &nested = assigned.error();
                auto error = runner.fail(
                    nested.code == VmfDisplayRuntimeErrorCode::variables
                        ? VmfExecutionErrorCode::variables
                        : VmfExecutionErrorCode::timeline,
                    "VMF variable assignment rejected");
                error.display_error = nested;
                error.variable_error = nested.variable_error;
                return std::unexpected(error);
            }
            break;
        }
        case 0x23: {
            if (!host || !properties)
                return runner.report(VmfExecutionStatus::needs_host);
            const auto value = runner.text();
            if (!value)
                return std::unexpected(value.error());
            const auto numeric_index = runner.number();
            if (!numeric_index)
                return std::unexpected(numeric_index.error());
            const auto index = static_cast<std::uint32_t>(_mm_cvttss_si32(
                _mm_add_ss(_mm_set_ss(*numeric_index), _mm_set_ss(0.5f))));
            VmfInstanceHandle selected;
            if (const auto *top = stack.peek();
                top && top->kind() == VmfValueKind::sprite) {
                selected = vmf_instance_handle(*top->sprite_identity());
                stack.discard();
            } else {
                const auto name = runner.text();
                if (!name)
                    return std::unexpected(name.error());
                const auto text = name->text();
                if (!runner.charge(text.size()))
                    return std::unexpected(runner.text_failure());
                const auto resolved = display->resolve_target(
                    host->current.value_or(VmfInstanceHandle{}), text,
                    target_limits);
                if (!resolved) {
                    auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                             "VMF property target rejected");
                    error.display_error = resolved.error();
                    return std::unexpected(error);
                }
                if (!resolved->found)
                    break; // Native miss: no state write, current unchanged.
                selected = *resolved->target;
            }
            // A later numeric target pop may have changed this shared text.
            // Ignored properties do not inspect the value with logging off.
            const bool reads_value = index == 0 || index == 1 || index == 2 ||
                                     index == 3 || index == 6 || index == 7 ||
                                     index == 10 || index == 16 ||
                                     index == 17 || index == 18;
            const auto text = reads_value ? value->text() : std::string_view{};
            if (reads_value && !runner.charge(text.size()))
                return std::unexpected(runner.text_failure());
            const auto applied = display->set_property(selected, index, text);
            if (!applied) {
                auto error = runner.fail(VmfExecutionErrorCode::timeline,
                                         "VMF property assignment rejected");
                error.display_error = applied.error();
                return std::unexpected(error);
            }
            break;
        }
        case 0x22:
        case 0x26:
        case 0x83:
        case 0x9a:
        case 0x9e:
        case 0x9f:
            return runner.report(VmfExecutionStatus::needs_host);
        default:
            return std::unexpected(
                runner.fail(VmfExecutionErrorCode::invalid_program,
                            "VMF action not covered"));
        }
        if (!pushed)
            return std::unexpected(runner.stack_failure(pushed.error()));
        ++runner.executed;
        if (next >= script.action_count)
            return std::unexpected(
                runner.fail(VmfExecutionErrorCode::invalid_program,
                            "VMF action stream lacks End"));
        cursor.next_action = next;
    }
}

} // namespace

std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(const assets::VmfMovie& movie, VmfExecutionCursor& cursor,
                    VmfStack& stack, VmfExecutionLimits limits) noexcept {
    return execute(movie, cursor, stack, limits);
}

std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(VmfDisplayRuntime& display, VmfInstanceHandle target,
                    VmfExecutionCursor& cursor, VmfStack& stack,
                    VmfExecutionLimits limits) noexcept {
    const auto instance = display.inspect_instance(target);
    if (!instance || instance->kind != VmfInstanceKind::timeline) {
        cursor.faulted = true;
        const auto reason =
            instance
                ? VmfDisplayRuntimeError{VmfDisplayRuntimeErrorCode::wrong_kind,
                                         "VMF action target is not a timeline"}
                : instance.error();
        return std::unexpected(
            VmfExecutionError{VmfExecutionErrorCode::timeline,
                              cursor.script_index,
                              cursor.next_action,
                              0,
                              0,
                              {},
                              "VMF action target rejected",
                              reason});
    }
    const auto program = display.program();
    return execute(program->movie(), cursor, stack, limits, &display, target);
}

VmfScriptContext::VmfScriptContext(VmfProgramPtr program,
                                   VmfInstanceHandle origin,
                                   std::size_t script_index,
                                   VmfTargetLookupLimits target_limits) noexcept
    : program_(std::move(program)),
      state_{VmfExecutionCursor{script_index}, origin, origin,
             VmfScriptPhase::execute},
      target_limits_(target_limits) {}

std::expected<VmfScriptContext, VmfExecutionError>
VmfScriptContext::create(VmfDisplayRuntime& display, VmfInstanceHandle origin,
                         std::size_t script_index,
                         VmfTargetLookupLimits target_limits) noexcept {
    const auto instance = display.inspect_instance(origin);
    if (!instance || instance->kind != VmfInstanceKind::timeline) {
        const auto reason =
            instance
                ? VmfDisplayRuntimeError{VmfDisplayRuntimeErrorCode::wrong_kind,
                                         "VMF script origin is not a timeline"}
                : instance.error();
        return std::unexpected(
            VmfExecutionError{VmfExecutionErrorCode::timeline,
                              script_index,
                              0,
                              0,
                              0,
                              {},
                              "VMF script origin rejected",
                              reason});
    }
    const auto program = display.program();
    if (script_index >= program->movie().scripts().size())
        return std::unexpected(
            VmfExecutionError{VmfExecutionErrorCode::invalid_cursor,
                              script_index,
                              0,
                              0,
                              0,
                              {},
                              "VMF context names no script"});
    return VmfScriptContext{program, origin, script_index, target_limits};
}

VmfScriptSnapshot VmfScriptContext::snapshot() const noexcept { return state_; }

std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(VmfDisplayRuntime& display, VmfScriptContext& context,
                    VmfStack& stack, VmfExecutionLimits limits) noexcept {
    return context.run(display, stack, limits, nullptr);
}

std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions(VmfDisplayRuntime &display, VmfScriptContext &context,
                    VmfStack &stack, const VmfVariables &variables,
                    VmfExecutionLimits limits) noexcept {
    return context.run(display, stack, limits, &variables);
}

std::expected<VmfExecutionReport, VmfExecutionError>
VmfScriptContext::run(VmfDisplayRuntime &display, VmfStack &stack,
                      VmfExecutionLimits limits, const VmfVariables *variables,
                      bool write_variables, bool properties) noexcept {
    auto &state = state_;
    const auto reject_context =
        [&](VmfExecutionErrorCode code, std::string_view detail,
            std::optional<VmfDisplayRuntimeError> reason = {}) {
            state.cursor.faulted = true;
            state.phase = VmfScriptPhase::faulted;
            return std::unexpected(VmfExecutionError{code,
                                                     state.cursor.script_index,
                                                     state.cursor.next_action,
                                                     0,
                                                     0,
                                                     {},
                                                     detail,
                                                     reason});
        };
    if (!program_ || program_ != display.program())
        return reject_context(VmfExecutionErrorCode::invalid_program,
                              "VMF script context program mismatch");
    const auto origin = display.inspect_instance(state.origin);
    if (!origin || origin->kind != VmfInstanceKind::timeline) {
        const auto reason =
            origin
                ? VmfDisplayRuntimeError{VmfDisplayRuntimeErrorCode::wrong_kind,
                                         "VMF script origin is not a timeline"}
                : origin.error();
        return reject_context(VmfExecutionErrorCode::timeline,
                              "VMF script origin rejected", reason);
    }
    return execute(program_->movie(), state.cursor, stack, limits, &display, {},
                   &state, target_limits_, variables, write_variables,
                   properties);
}

std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions_with_variables(VmfDisplayRuntime &display,
                                   VmfScriptContext &context, VmfStack &stack,
                                   VmfExecutionLimits limits) noexcept {
    return context.run(display, stack, limits, display.variable_dictionary(),
                       true);
}

std::expected<VmfExecutionReport, VmfExecutionError>
execute_vmf_actions_with_properties(VmfDisplayRuntime &display,
                                    VmfScriptContext &context, VmfStack &stack,
                                    VmfExecutionLimits limits) noexcept {
    return context.run(display, stack, limits, display.variable_dictionary(),
                       true, true);
}

} // namespace tdur::ui

#include "tdur/ui/vmf_fscommand_global.hpp"

#include <bit>
#include <stdexcept>
#include <utility>

namespace tdur::ui {
namespace {
using Branch = VmfFsGlobalBranch;
using Phase = VmfFsGlobalPhase;
using Code = VmfFsCommandErrorCode;
using Error = VmfFsCommandError;
struct Guard {
    bool& active;
    explicit Guard(bool& value) noexcept : active(value) { active = true; }
    ~Guard() { active = false; }
};
Error error(Code code, std::string_view detail) noexcept {
    return {code, detail, {}};
}
} // namespace

std::expected<std::uint32_t, Error>
classify_vmf_command4(std::string_view command) noexcept {
    if (command.size() < 4)
        return std::unexpected(
            error(Code::invalid_binding, "Four command bytes are unavailable"));
    std::uint32_t code{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto raw = static_cast<unsigned char>(command[i]);
        const auto signed_byte = std::bit_cast<std::int8_t>(raw);
        code = (code << 8U) | static_cast<std::uint32_t>(
                                  static_cast<std::int32_t>(signed_byte));
    }
    return code;
}
Branch identify_vmf_global_branch(std::uint32_t code) noexcept {
    switch (code) {
    case 0x61766e6fU:
        return Branch::avno;
    case 0x61767965U:
        return Branch::avye;
    case 0x63616d73U:
        return Branch::cams;
    case 0x636c6d73U:
        return Branch::clms;
    case 0x66726361U:
        return Branch::frca;
    case 0x66726f6bU:
        return Branch::frok;
    case 0x6765696eU:
        return Branch::gein;
    case 0x67656f75U:
        return Branch::geou;
    case 0x6d63696eU:
        return Branch::mcin;
    case 0x6d636f75U:
        return Branch::mcou;
    case 0x6e656176U:
        return Branch::neav;
    case 0x74666f63U:
        return Branch::tfoc;
    case 0x7475696eU:
        return Branch::tuin;
    case 0x74756f75U:
        return Branch::tuou;
    case 0x77656e64U:
        return Branch::wend;
    default:
        return Branch::unknown;
    }
}
VmfFsGlobalContext::VmfFsGlobalContext(std::string_view command,
                                       const void* userdata) noexcept
    : command_(command), userdata_(userdata) {}
void VmfFsGlobalContext::finish(std::uint8_t result) noexcept {
    state_.result_al = result;
    state_.phase = Phase::finished;
}
void VmfFsGlobalContext::select(Branch branch) noexcept {
    state_.branch = branch;
    switch (branch) {
    case Branch::unknown:
        finish(0);
        break;
    case Branch::frca:
        state_.phase = Phase::collection;
        break;
    case Branch::frok:
        state_.phase = Phase::frok_root;
        break;
    case Branch::mcin:
    case Branch::mcou:
        state_.phase = Phase::mode_owner;
        break;
    case Branch::tfoc:
        state_.phase = Phase::focus_key;
        break;
    case Branch::tuin:
        state_.phase = Phase::tuin_owner_first;
        break;
    case Branch::tuou:
        state_.phase = Phase::tuou_owner_first;
        break;
    case Branch::wend:
        state_.phase = Phase::wend_argument;
        break;
    default:
        state_.phase = Phase::owner4;
        break;
    }
}
void VmfFsGlobalContext::next_item() noexcept {
    ++state_.loop_index; // At most INT32_MAX: loop_index was strictly < count.
    state_.phase = state_.loop_index < state_.loop_count ? Phase::loop_lookup
                                                         : Phase::post_owner;
}
void VmfFsGlobalContext::after_tutorial_selection() noexcept {
    if (state_.branch == Branch::tuin)
        finish(1);
    else
        state_.phase = Phase::tutorial_call;
}
VmfFsGlobalSnapshot VmfFsGlobalContext::snapshot() const noexcept {
    return state_;
}
std::optional<Error> VmfFsGlobalContext::last_error() const noexcept {
    return error_;
}
std::expected<std::uint8_t, Error> VmfFsGlobalContext::result() const noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "Global context is running"));
    if (error_)
        return std::unexpected(*error_);
    if (state_.phase != Phase::finished || !state_.result_al)
        return std::unexpected(
            error(Code::invalid_context, "Global result is not finished"));
    return *state_.result_al;
}
std::expected<void, Error> VmfFsGlobalContext::cancel() noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "Global context is running"));
    if (state_.faulted || state_.phase == Phase::finished)
        return {};
    error_ = error(Code::abandoned,
                   "Global command was abandoned without completion");
    state_.faulted = true;
    return {};
}
std::expected<VmfHostCallReport, Error>
VmfFsGlobalContext::run(const VmfFsGlobalInputs& inputs,
                        std::size_t budget) noexcept {
    if (running_)
        return std::unexpected(
            error(Code::invalid_context, "Global context is running"));
    if (state_.faulted)
        return std::unexpected(*error_);
    Guard guard(running_);
    std::size_t steps{};
    const auto fail = [&](Error reason) {
        error_ = reason;
        state_.faulted = true;
        return std::unexpected(reason);
    };
    const auto called_error = [&](const Error& cause) {
        return fail(Error{Code::callback, cause.detail, cause.code});
    };
    const auto wait = [&](std::string_view name) {
        state_.waiting_for = name;
        return VmfHostCallReport{VmfHostCallStatus::pending, steps};
    };
    const auto load = [&]<class T>(const std::optional<T*>& source, T*& target,
                                   std::string_view name,
                                   bool nullable = false) {
        if (!source) {
            state_.waiting_for = name;
            return false;
        }
        target = *source;
        if (!target && !nullable) {
            error_ = error(Code::invalid_binding, name);
            state_.faulted = true;
            return false;
        }
        return true;
    };
    const auto stopped = [&]() -> std::expected<VmfHostCallReport, Error> {
        if (error_)
            return std::unexpected(*error_);
        return VmfHostCallReport{VmfHostCallStatus::pending, steps};
    };
    while (steps < budget && state_.phase != Phase::finished) {
        switch (state_.phase) {
        case Phase::classify: {
            if (command_.size() < 4) {
                select(Branch::unknown);
                break;
            }
            const auto code = classify_vmf_command4(command_);
            if (!code)
                return fail(code.error());
            select(identify_vmf_global_branch(*code));
            break;
        }
        case Phase::owner4: {
            const bool delegate = state_.branch == Branch::avno ||
                                  state_.branch == Branch::cams ||
                                  state_.branch == Branch::gein ||
                                  state_.branch == Branch::geou;
            if (!load(inputs.state.owner_4, owner4_, "owner4", delegate))
                return stopped();
            argument_ = owner4_;
            state_.phase = delegate ? Phase::leaf_call : Phase::state_compare;
            break;
        }
        case Phase::state_compare:
            if (!owner4_->state_c)
                return wait("owner4.stateC");
            was_two_ = *owner4_->state_c == 2U;
            state_.phase = Phase::state_clear;
            break;
        case Phase::state_clear:
            owner4_->field_38 = 0U;
            ++state_.writes;
            if (was_two_)
                state_.phase = Phase::state_set;
            else
                finish(1);
            break;
        case Phase::state_set:
            owner4_->state_c = 3U;
            ++state_.writes;
            finish(1);
            break;
        case Phase::wend_argument:
        case Phase::post_owner:
            if (!load(state_.phase == Phase::wend_argument
                          ? inputs.globals.owner_114c048
                          : inputs.globals.owner_10e75f0,
                      argument_, "helper argument", true))
                return stopped();
            state_.phase = Phase::leaf_call;
            break;
        case Phase::leaf_call: {
            const std::optional<VmfFsGlobalCall*>* binding{};
            switch (state_.branch) {
            case Branch::avno:
            case Branch::cams:
                binding = &inputs.call_99fc80;
                break;
            case Branch::gein:
                binding = &inputs.call_9a0110;
                break;
            case Branch::geou:
                binding = &inputs.call_99fcc0;
                break;
            case Branch::wend:
                binding = &inputs.call_99feb0;
                break;
            case Branch::frca:
                binding = &inputs.call_866040;
                break;
            case Branch::frok:
                binding = &inputs.call_866080;
                break;
            default:
                return fail(error(Code::invalid_context,
                                  "Invalid global helper phase"));
            }
            VmfFsGlobalCall* service{};
            if (!load(*binding, service, "global helper"))
                return stopped();
            ++state_.calls;
            const auto result = service->invoke(argument_);
            if (!result)
                return called_error(result.error());
            finish(1);
            break;
        }
        case Phase::frok_root:
            if (!load(inputs.globals.root_11403d0, frok_root_, "root11403D0",
                      true))
                return stopped();
            state_.phase = frok_root_ ? Phase::frok_child : Phase::collection;
            break;
        case Phase::frok_child:
            if (!load(frok_root_->child_90, frok_child_, "root.child90", true))
                return stopped();
            state_.phase = frok_child_ ? Phase::frok_flags : Phase::collection;
            break;
        case Phase::frok_flags:
            if (!frok_child_->flags_8)
                return wait("frok.flags8");
            frok_child_->flags_8 =
                static_cast<std::uint8_t>(((*frok_child_->flags_8 & 1U) << 1U) |
                                          (*frok_child_->flags_8 & 0xe1U));
            ++state_.writes;
            state_.phase = Phase::collection;
            break;
        case Phase::collection:
            if (!load(inputs.collection_114b0c0, collection_, "fixed114B0C0"))
                return stopped();
            state_.phase = Phase::count;
            break;
        case Phase::count: {
            ++state_.calls;
            const auto result = collection_->count(1U);
            if (!result)
                return called_error(result.error());
            state_.loop_count = *result;
            state_.loop_index = 0;
            state_.phase = *result > 0 ? Phase::loop_lookup : Phase::post_owner;
            break;
        }
        case Phase::loop_lookup: {
            ++state_.calls;
            const auto result = collection_->item(1U, state_.loop_index);
            if (!result)
                return called_error(result.error());
            item_ = *result;
            if (item_)
                state_.phase = Phase::loop_flags;
            else
                next_item();
            break;
        }
        case Phase::loop_flags:
            if (!item_->flags_ac4)
                return wait("item.flagsAC4");
            item_->flags_ac4 = *item_->flags_ac4 & 0xffffdfffU;
            ++state_.writes;
            next_item();
            break;
        case Phase::mode_owner:
        case Phase::tuin_owner_first:
        case Phase::tuin_owner_second:
        case Phase::tuou_owner_after:
            if (!load(inputs.globals.owner_114c04c, mode_, "owner114C04C"))
                return stopped();
            state_.phase =
                state_.phase == Phase::tuin_owner_first    ? Phase::tuin_read
                : state_.phase == Phase::tuin_owner_second ? Phase::tuin_zero
                : state_.phase == Phase::tuou_owner_after  ? Phase::tuou_restore
                : state_.branch == Branch::mcin            ? Phase::mcin_dword
                                                           : Phase::mcou_byte;
            break;
        case Phase::mcin_dword:
            mode_->dword_2c = 2U;
            ++state_.writes;
            state_.phase = Phase::mcin_byte;
            break;
        case Phase::mcin_byte:
        case Phase::mcou_byte:
            mode_->byte_31 = state_.phase == Phase::mcin_byte ? std::uint8_t{1}
                                                              : std::uint8_t{0};
            ++state_.writes;
            finish(1);
            break;
        case Phase::focus_key:
            if (!load(inputs.constant_f07c88, focus_key_, "constantF07C88"))
                return stopped();
            state_.phase = Phase::focus_query;
            break;
        case Phase::focus_query: {
            VmfFsGlobalFocusQuery* service{};
            if (!load(inputs.focus_query_4409c0, service, "focus4409C0"))
                return stopped();
            ++state_.calls;
            const auto result = service->query(
                userdata_, focus_key_, std::span<std::byte, 32>(local_));
            if (!result)
                return called_error(result.error());
            if (*result)
                state_.phase = Phase::focus_count;
            else
                finish(1);
            break;
        }
        case Phase::focus_count:
            if (!inputs.globals.focus.count_14160fc)
                return wait("focus.count14160FC");
            focus_count_ = *inputs.globals.focus.count_14160fc;
            state_.phase = Phase::focus_zero100;
            break;
        case Phase::focus_zero100:
            inputs.globals.focus.state_1416100 = 0U;
            ++state_.writes;
            state_.phase = Phase::focus_zero108;
            break;
        case Phase::focus_zero108:
            inputs.globals.focus.state_1416108 = 0U;
            ++state_.writes;
            state_.phase = Phase::focus_index;
            break;
        case Phase::focus_index:
            inputs.globals.focus.index_1416104 = focus_count_ - 1U;
            ++state_.writes;
            state_.phase = Phase::focus_apply;
            break;
        case Phase::focus_apply: {
            VmfFsGlobalFocusApply* service{};
            if (!load(inputs.focus_apply_441200, service, "focus441200"))
                return stopped();
            ++state_.calls;
            const auto result = service->apply(
                userdata_, std::span<const std::byte, 32>(local_));
            if (!result)
                return called_error(result.error());
            finish(1);
            break;
        }
        case Phase::tuin_read:
            if (!mode_->byte_30)
                return wait("owner.byte30");
            saved_byte_ = *mode_->byte_30;
            state_.phase = Phase::tuin_save;
            break;
        case Phase::tuin_save:
            inputs.state.saved_8 = saved_byte_;
            ++state_.writes;
            state_.phase = Phase::tuin_owner_second;
            break;
        case Phase::tuin_zero:
            mode_->byte_30 = std::uint8_t{0};
            ++state_.writes;
            state_.phase = Phase::tutorial_call;
            break;
        case Phase::tuou_owner_first:
        case Phase::tuin_owner_after:
            if (!load(inputs.globals.owner_10e72f0, tutorial_owner_,
                      "owner10E72F0", true))
                return stopped();
            state_.phase = Phase::tutorial_flag;
            break;
        case Phase::tutorial_flag:
            inputs.globals.tutorial_flag_141194d = state_.branch == Branch::tuin
                                                       ? std::uint8_t{1}
                                                       : std::uint8_t{0};
            ++state_.writes;
            state_.phase = Phase::tutorial_kind;
            break;
        case Phase::tutorial_kind:
            if (!tutorial_owner_) {
                after_tutorial_selection();
                break;
            }
            if (!tutorial_owner_->kind_44)
                return wait("tutorial.kind44");
            if (*tutorial_owner_->kind_44 == 0x17U)
                state_.phase = Phase::tutorial_enclosing;
            else
                after_tutorial_selection();
            break;
        case Phase::tutorial_enclosing:
            if (!load(tutorial_owner_->enclosing, enclosing_,
                      "tutorial.enclosing"))
                return stopped();
            state_.phase = Phase::tutorial_link;
            break;
        case Phase::tutorial_link:
            if (!load(enclosing_->link_ffa6c, link_, "tutorial.linkFFA6C",
                      true))
                return stopped();
            if (link_)
                state_.phase = Phase::tutorial_write;
            else
                after_tutorial_selection();
            break;
        case Phase::tutorial_write:
            link_->byte_4 = state_.branch == Branch::tuin ? std::uint8_t{0}
                                                          : std::uint8_t{1};
            ++state_.writes;
            after_tutorial_selection();
            break;
        case Phase::tutorial_call: {
            VmfFsGlobalTutorial* service{};
            if (!load(inputs.tutorial_1214d30, service, "fixed1214D30"))
                return stopped();
            ++state_.calls;
            const auto result = service->invoke(state_.branch == Branch::tuin);
            if (!result)
                return called_error(result.error());
            state_.phase = state_.branch == Branch::tuin
                               ? Phase::tuin_owner_after
                               : Phase::tuou_saved_read;
            break;
        }
        case Phase::tuou_saved_read:
            if (!inputs.state.saved_8)
                return wait("handler.saved8");
            saved_byte_ = *inputs.state.saved_8;
            state_.phase = Phase::tuou_owner_after;
            break;
        case Phase::tuou_restore:
            mode_->byte_30 = saved_byte_ ? std::uint8_t{1} : std::uint8_t{0};
            ++state_.writes;
            finish(1);
            break;
        case Phase::finished:
            break;
        }
        state_.waiting_for = {};
        ++steps;
        ++state_.steps;
    }
    return VmfHostCallReport{state_.phase == Phase::finished
                                 ? VmfHostCallStatus::finished
                                 : VmfHostCallStatus::step_limit,
                             steps};
}

struct VmfFsGlobalHandler::Slot final : VmfFsHandlerFrame {
    const VmfFsGlobalInputs* inputs{};
    std::optional<VmfFsGlobalContext> context;
    bool leased{}, running{};
    std::expected<VmfHostCallReport, Error>
    run(std::size_t budget) noexcept override {
        if (!leased || running)
            return std::unexpected(
                error(Code::invalid_context, "Global frame is not runnable"));
        Guard guard(running);
        return context->run(*inputs, budget);
    }
    std::expected<std::uint8_t, Error> result() const noexcept override {
        if (!leased || running)
            return std::unexpected(error(Code::invalid_context,
                                         "Global frame result is unavailable"));
        return context->result();
    }
    void release() noexcept override {
        if (!leased || running)
            return;
        Guard guard(running);
        (void)context->cancel();
        context.reset();
        leased = false;
    }
};
VmfFsGlobalHandler::VmfFsGlobalHandler(const VmfFsGlobalInputs& inputs,
                                       std::size_t capacity)
    : inputs_(inputs) {
    if (capacity == 0 || capacity > 32)
        throw std::invalid_argument("Global handler capacity must be1..32");
    slots_ = std::make_unique<Slot[]>(capacity);
    capacity_ = capacity;
    for (std::size_t i = 0; i < capacity_; ++i)
        slots_[i].inputs = &inputs_;
}
VmfFsGlobalHandler::~VmfFsGlobalHandler() noexcept {
    closing_ = true;
    for (std::size_t i = 0; i < capacity_; ++i)
        slots_[i].release();
}
std::expected<VmfFsHandlerStart, Error>
VmfFsGlobalHandler::begin(std::string_view command,
                          const void* userdata) noexcept {
    if (closing_)
        return std::unexpected(
            error(Code::invalid_context, "Global handler is closing"));
    if (command.size() < 4)
        return VmfFsHandlerStart{nullptr, 0};
    const auto code = classify_vmf_command4(command);
    if (!code)
        return std::unexpected(code.error());
    const auto branch = identify_vmf_global_branch(*code);
    if (branch == Branch::unknown)
        return VmfFsHandlerStart{nullptr, 0};
    for (std::size_t i = 0; i < capacity_; ++i) {
        auto& slot = slots_[i];
        if (slot.leased)
            continue;
        slot.leased = true;
        slot.context.emplace(command, userdata);
        slot.context->select(branch);
        slot.context->state_.steps = 1; // Classification belongs to begin.
        return VmfFsHandlerStart{&slot, 0};
    }
    return std::unexpected(
        error(Code::invalid_context, "Global handler pool is full"));
}
std::size_t VmfFsGlobalHandler::capacity() const noexcept { return capacity_; }
std::size_t VmfFsGlobalHandler::leased() const noexcept {
    std::size_t count{};
    for (std::size_t i = 0; i < capacity_; ++i)
        count += static_cast<std::size_t>(slots_[i].leased);
    return count;
}
} // namespace tdur::ui

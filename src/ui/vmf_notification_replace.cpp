#include "tdur/ui/vmf_notification_replace.hpp"
#include "tdur/ui/vmf_notification_prefix.hpp"

#include <algorithm>
#include <limits>

namespace tdur::ui {
namespace {
using Code = VmfNotificationReplaceErrorCode;
using Phase = VmfNotificationReplacePhase;
using Status = VmfNotificationReplaceStatus;
constexpr std::string_view delimiter{"\xc2\xa7", 2};
constexpr std::string_view null_argument{"(ERR:2)"};
VmfNotificationReplaceError error(Code code, std::string_view detail,
                                  std::size_t at = 0) noexcept {
    return {code, detail, at};
}
std::uint32_t width(unsigned byte) noexcept {
    if ((byte & 0x80U) == 0)
        return 1;
    if ((byte & 0xe0U) == 0xc0U)
        return 2;
    return (byte & 0xf0U) == 0xe0U ? 3 : 1;
}
} // namespace

VmfNotificationFormatSource
VmfNotificationFormatSource::borrowed(std::span<const char> bytes) noexcept {
    VmfNotificationFormatSource source;
    source.bytes_ = bytes;
    return source;
}
std::expected<VmfNotificationFormatSource, VmfNotificationReplaceError>
VmfNotificationFormatSource::retained(const VmfTextReference& text) noexcept {
    if (text.extent_bytes() == 0 || !text.c_str())
        return std::unexpected(
            error(Code::invalid_reference,
                  "Format reference has no established backing"));
    auto result = borrowed({text.c_str(), text.extent_bytes()});
    result.keeper_ = text;
    return result;
}
std::span<const char> VmfNotificationFormatSource::bytes() const noexcept {
    return bytes_;
}

VmfNotificationReplaceContext::VmfNotificationReplaceContext(
    std::span<char> destination) noexcept
    : destination_(destination) {}
VmfNotificationReplaceSnapshot
VmfNotificationReplaceContext::snapshot() const noexcept {
    return state_;
}
std::expected<unsigned, VmfNotificationReplaceError>
VmfNotificationReplaceContext::byte(std::size_t at) const noexcept {
    const auto bytes = format_->bytes();
    if (at > bytes.size())
        return std::unexpected(error(
            Code::source_range, "Format read exceeds physical backing", at));
    return at == bytes.size() ? 0U : static_cast<unsigned char>(bytes[at]);
}
std::expected<bool, VmfNotificationReplaceError>
VmfNotificationReplaceContext::marker(std::size_t at,
                                      std::uint32_t count) const noexcept {
    const auto bytes = format_->bytes();
    if (at > bytes.size())
        return std::unexpected(error(
            Code::source_range, "Marker read exceeds physical backing", at));
    const auto tail = at == bytes.size() ? std::string_view{}
                                         : std::string_view{bytes.data() + at,
                                                            bytes.size() - at};
    const auto result = compare_vmf_notification_prefix(delimiter, tail, count);
    if (!result)
        return std::unexpected(
            error(Code::source_range, "Marker comparison binding failed", at));
    return *result == 0;
}
std::expected<void, VmfNotificationReplaceError>
VmfNotificationReplaceContext::advance(std::uint32_t count) noexcept {
    if (state_.cursor > std::numeric_limits<std::size_t>::max() - count)
        return std::unexpected(
            error(Code::source_range, "Format cursor overflow", state_.cursor));
    state_.cursor += count;
    return {};
}
std::expected<void, VmfNotificationReplaceError>
VmfNotificationReplaceContext::copy(std::string_view source) noexcept {
    if (state_.logical_written > destination_.size())
        return std::unexpected(error(Code::output_range,
                                     "Replacement write exceeds destination",
                                     state_.logical_written));
    const auto remaining = static_cast<std::uint32_t>(destination_.size()) -
                           state_.logical_written;
    const auto copied = copy_vmf_notification_prefix(
        destination_.subspan(state_.logical_written), source, remaining);
    if (!copied) {
        state_.stores += copied.error().offset;
        return std::unexpected(
            error(Code::output_range,
                  "Replacement copy was refused after partial stores",
                  copied.error().offset));
    }
    state_.logical_written += *copied;
    state_.stores += *copied;
    return {};
}

std::expected<VmfNotificationReplaceReport, VmfNotificationReplaceError>
VmfNotificationReplaceContext::run(const VmfNotificationReplaceInputs& inputs,
                                   std::size_t budget) noexcept {
    if (running_ || state_.faulted)
        return std::unexpected(
            error(Code::invalid_context,
                  "Replacement context is running or faulted"));
    running_ = true;
    struct Reset {
        bool& running;
        ~Reset() { running = false; }
    } reset{running_};
    const auto initial_steps = state_.steps;
    const auto report = [&](Status status) {
        return VmfNotificationReplaceReport{status, state_,
                                            state_.steps - initial_steps};
    };
    const auto fail = [&](VmfNotificationReplaceError failure)
        -> std::expected<VmfNotificationReplaceReport,
                         VmfNotificationReplaceError> {
        state_.faulted = true;
        return std::unexpected(failure);
    };
    if (state_.phase == Phase::finished)
        return report(Status::finished);
    for (std::size_t used = 0; used < budget; ++used) {
        ++state_.steps;
        switch (state_.phase) {
        case Phase::bind: {
            if (destination_.empty() || destination_.size() > UINT32_MAX)
                return fail(
                    error(Code::invalid_capacity,
                          "Replacement requires nonzero DWORD capacity"));
            if (!inputs.format)
                return report(Status::needs_format);
            format_ = inputs.format;
            if (inputs.arguments && inputs.borrowed_arguments)
                return fail(error(Code::invalid_arguments,
                                  "Two positional binding kinds supplied"));
            if (inputs.arguments)
                arguments_ = inputs.arguments;
            if (inputs.borrowed_arguments)
                borrowed_arguments_ = inputs.borrowed_arguments;
            const auto first = byte(0);
            if (!first)
                return fail(first.error());
            state_.phase = *first ? Phase::iteration : Phase::terminate;
            break;
        }
        case Phase::iteration: {
            if (state_.logical_written >= destination_.size()) {
                state_.phase = Phase::terminate;
                break;
            }
            const auto current = byte(state_.cursor);
            if (!current)
                return fail(current.error());
            const auto size = width(*current);
            const auto opening = marker(state_.cursor, size);
            if (!opening)
                return fail(opening.error());
            if (!*opening) {
                const auto amount = std::min(
                    size, static_cast<std::uint32_t>(destination_.size()) -
                              state_.logical_written);
                // Literal width<=3: native REP MOVSB is a live forward copy.
                for (std::uint32_t i = 0; i < amount; ++i) {
                    if (state_.cursor >
                        std::numeric_limits<std::size_t>::max() - i)
                        return fail(error(Code::source_range,
                                          "Literal read cursor overflow",
                                          state_.cursor));
                    const auto value = byte(state_.cursor + i);
                    if (!value)
                        return fail(value.error());
                    destination_[state_.logical_written + i] =
                        static_cast<char>(*value);
                    ++state_.stores;
                }
                state_.logical_written +=
                    size; // DWORD wrap, not clipped amount.
                state_.phase = Phase::advance;
                break;
            }
            state_.delimiter_met = true;
            const auto advanced = advance(size);
            if (!advanced)
                return fail(advanced.error());
            const auto body = state_.cursor;
            const auto digit = byte(body);
            if (!digit)
                return fail(digit.error());
            bool positional = false;
            if (*digit >= '0' && *digit <= '9') {
                const auto next = advance(1);
                if (!next)
                    return fail(next.error());
                const auto lead = byte(state_.cursor);
                if (!lead)
                    return fail(lead.error());
                const auto closed = marker(state_.cursor, width(*lead));
                if (!closed)
                    return fail(closed.error());
                positional = *closed;
                // Native backs up to the ASCII digit even on a named path.
                state_.cursor = body;
                argument_index_ = *digit == '0' ? 0 : *digit - '1';
            }
            state_.phase = positional ? Phase::positional : Phase::named_begin;
            break;
        }
        case Phase::positional: {
            if (!arguments_ && !borrowed_arguments_) {
                if (inputs.arguments && inputs.borrowed_arguments)
                    return fail(error(Code::invalid_arguments,
                                      "Two positional binding kinds supplied"));
                if (!inputs.arguments && !inputs.borrowed_arguments)
                    return report(Status::needs_arguments);
                if (inputs.arguments)
                    arguments_ = inputs.arguments;
                else
                    borrowed_arguments_ = inputs.borrowed_arguments;
            }
            std::string_view source = null_argument;
            if (borrowed_arguments_) {
                const auto& selected = (*borrowed_arguments_)[argument_index_];
                if (selected) {
                    const auto bytes = selected->bytes();
                    source = bytes.empty()
                                 ? std::string_view{}
                                 : std::string_view{bytes.data(), bytes.size()};
                }
            } else {
                const auto& selected = (*arguments_)[argument_index_];
                if (selected) {
                    if (selected->extent_bytes() == 0 || !selected->c_str())
                        return fail(error(
                            Code::invalid_reference,
                            "Selected positional reference has no backing",
                            argument_index_));
                    source = {selected->c_str(), selected->extent_bytes()};
                }
            }
            const auto copied = copy(source);
            if (!copied)
                return fail(copied.error());
            ++state_.positional_copies;
            closing_loaded_ = false;
            state_.phase = Phase::closing;
            break;
        }
        case Phase::named_begin: {
            if (!backend_) {
                if (!inputs.named_backend)
                    return report(Status::needs_named_backend);
                backend_ = *inputs.named_backend;
                if (!backend_)
                    return fail(
                        error(Code::invalid_named_binding,
                              "Named implementation is explicitly NULL"));
            }
            ++state_.named_begins;
            const auto started = backend_->begin({*format_, state_.cursor});
            if (!started)
                return fail(started.error());
            state_.phase = Phase::named_run;
            break;
        }
        case Phase::named_run: {
            ++state_.named_runs;
            const auto named = backend_->run(1);
            if (!named)
                return fail(named.error());
            if (named->steps > 1)
                return fail(
                    error(Code::invalid_named_result,
                          "Named implementation exceeded its step budget"));
            if (named->status != VmfNotificationNamedStatus::finished &&
                named->status != VmfNotificationNamedStatus::step_limit &&
                named->status != VmfNotificationNamedStatus::needs_inputs)
                return fail(
                    error(Code::invalid_named_result,
                          "Named implementation returned unknown status"));
            if (named->status != VmfNotificationNamedStatus::finished) {
                if (named->status == VmfNotificationNamedStatus::needs_inputs ||
                    named->steps == 0)
                    return report(Status::named_pending);
                break;
            }
            const auto text = named->initialized_text;
            if (text.empty() || text.size() > named_output_.size())
                return fail(
                    error(Code::invalid_named_result,
                          "Named output has no bounded initialized bytes"));
            const auto nul = std::find(text.begin(), text.end(), '\0');
            if (nul == text.end())
                return fail(error(Code::invalid_named_result,
                                  "Named output has no established NUL"));
            named_bytes_ = static_cast<std::size_t>(nul - text.begin()) + 1;
            std::copy_n(text.begin(), named_bytes_, named_output_.begin());
            state_.phase = Phase::named_copy;
            break;
        }
        case Phase::named_copy: {
            const auto copied = copy({named_output_.data(), named_bytes_});
            if (!copied)
                return fail(copied.error());
            closing_loaded_ = false;
            state_.phase = Phase::closing;
            break;
        }
        case Phase::closing: {
            if (!closing_loaded_) {
                const auto current = byte(state_.cursor);
                if (!current)
                    return fail(current.error());
                closing_byte_ = *current;
            }
            closing_loaded_ = true;
            if (!closing_byte_) {
                state_.phase = Phase::advance;
                closing_loaded_ = false;
                break;
            }
            const auto size = width(closing_byte_);
            const auto closed = marker(state_.cursor, size);
            if (!closed)
                return fail(closed.error());
            if (*closed) {
                state_.phase = Phase::advance;
                closing_loaded_ = false;
                break;
            }
            const auto advanced = advance(size);
            if (!advanced)
                return fail(advanced.error());
            const auto next = byte(state_.cursor);
            if (!next)
                return fail(next.error());
            closing_byte_ = *next;
            if (!closing_byte_) {
                closing_loaded_ = false;
                state_.phase = Phase::advance;
            }
            break;
        }
        case Phase::advance: {
            // Re-read after copies: source may alias output or be host-mutated.
            const auto current = byte(state_.cursor);
            if (!current)
                return fail(current.error());
            const auto advanced = advance(width(*current));
            if (!advanced)
                return fail(advanced.error());
            const auto next = byte(state_.cursor);
            if (!next)
                return fail(next.error());
            state_.phase = *next ? Phase::iteration : Phase::terminate;
            break;
        }
        case Phase::terminate:
            destination_[std::min<std::size_t>(state_.logical_written,
                                               destination_.size() - 1)] = '\0';
            ++state_.stores;
            state_.terminated = true;
            state_.phase = Phase::finished;
            return report(Status::finished);
        case Phase::finished:
            return report(Status::finished);
        }
    }
    return report(Status::step_limit);
}
} // namespace tdur::ui

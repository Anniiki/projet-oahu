#pragma once

#include "tdur/ui/vmf_fscommand.hpp"
#include "tdur/ui/vmf_program.hpp"
#include "tdur/ui/vmf_stack.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

// Value token of one reserved or in-flight host action. Compared, never
// dereferenced. host is the issuing adapter's process-unique serial, never
// zero; generation changes each time the slot is freed.
struct VmfHostTicket {
    std::uint32_t host{};
    std::uint32_t slot{};
    std::uint32_t generation{};
    friend bool operator==(const VmfHostTicket&,
                           const VmfHostTicket&) = default;
};

enum class VmfHostActionStatus { finished, step_limit, waiting };
struct VmfHostActionReport {
    VmfHostActionStatus status{};
    std::size_t steps{}; // This call only, never above its budget.
};

enum class VmfHostActionErrorCode {
    invalid_request,
    invalid_ticket,
    capacity,
    reentry,
    closing,
    command,
};
struct VmfHostActionError {
    VmfHostActionErrorCode code{};
    std::string_view detail; // Static literal; error paths do not allocate.
    std::optional<VmfFsCommandError> command{};
};

// GetURL83 or GetURL2 9A operands already validated, and for 9A already
// popped, by the executor (443AF0 dispatch, 445D40 pops; secondary f452).
struct VmfHostUrlRequest {
    std::uint16_t opcode{};
    // Bounded C-string read window. 83: the literal without its NUL, whose
    // next source byte is that NUL. 9A: c_str() to the end of the live
    // backing, last byte NUL. Consumers read the live prefix before NUL. A
    // bound URL rewritten during the callback is read as it now stands;
    // equivalence with a native in-place rewrite is not established.
    std::string_view url;
    // 83 literal target, validated, never passed to 9C8. Empty for 9A.
    std::string_view target;
    VmfProgramPtr program;                       // Keeps 83 literals alive.
    std::optional<VmfTextReference> url_backing; // Keeps 9A text alive.
    std::size_t script_index{};
    std::size_t action_index{};
};

// Host services called by the VMF executor AT the action, where 443AF0
// calls 445CB0/445D40. Modern interface, not a native ABI. Calls are
// serialized and keep callee affinity; nothing allocates after construction.
// An instance outlives every ticket it issued and every player bound to it,
// and is never destroyed during one of its calls.
class VmfHostActions {
  public:
    virtual ~VmfHostActions() = default;
    // Constant capability, consulted only for 83/9A, before any operand
    // access. false keeps the legacy needs_host boundary.
    [[nodiscard]] virtual bool accepts(std::uint16_t opcode) const noexcept = 0;
    // Reserves capacity before any pop, without game effect. capacity is a
    // retryable refusal; the executor then reports needs_host.
    [[nodiscard]] virtual std::expected<VmfHostTicket, VmfHostActionError>
    reserve(std::uint16_t opcode) noexcept = 0;
    // Takes over consumed operands, runs at most budget steps. finished
    // frees the ticket; step_limit/waiting keep it in flight. An error frees
    // it and retains every executed effect, except refusals that change
    // nothing: stale or foreign ticket (invalid_ticket), call during the
    // slot's own run (reentry), closing host (closing), ticket already begun
    // (invalid_request). The executor follows every error of this call with
    // one cancel whose answer it ignores, so no slot outlives its context.
    [[nodiscard]] virtual std::expected<VmfHostActionReport, VmfHostActionError>
    begin_url(VmfHostTicket ticket, const VmfHostUrlRequest& request,
              std::size_t budget) noexcept = 0;
    // Continues an in-flight ticket; never replays what already began. Same
    // error rule; a ticket not begun yet is refused unchanged
    // (invalid_request). invalid_ticket and reentry keep the executor's
    // ticket and fault nothing; any other error faults with the same cancel.
    [[nodiscard]] virtual std::expected<VmfHostActionReport, VmfHostActionError>
    resume(VmfHostTicket ticket, std::size_t budget) noexcept = 0;
    // Frees a reservation, or terminally abandons an in-flight call: prefix
    // retained, frame released once, no completion, no restore, no replay.
    [[nodiscard]] virtual std::expected<void, VmfHostActionError>
    cancel(VmfHostTicket ticket) noexcept = 0;
};

} // namespace tdur::ui

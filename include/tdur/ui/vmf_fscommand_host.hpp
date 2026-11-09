#pragma once

#include "tdur/ui/vmf_host_actions.hpp"

#include <memory>

namespace tdur::ui {

struct VmfFsHostSnapshot {
    std::uint16_t opcode{};
    VmfFsCommandStatus last_status{}; // Exact status of the last run.
    VmfFsCommandSnapshot command;
};

// Default VmfHostActions, implemented in src/ui/vmf_fscommand.cpp next to
// the 445CB0 envelope it hosts. GetURL83 runs the envelope on its literal
// URL; GetURL2 9A runs it on the URL popped by the executor, since 445D40's
// sequence after its pops is 445CB0's (secondary f452 listings). One
// in-place VmfFsCommandContext per preallocated slot. Inputs, owners,
// callbacks (normally VmfFsDefaultCallback), logger, gate and pointees are
// borrowed, stable and serialized. Unknown inputs wait; required known NULL
// fault inside the envelope; nothing finishes before the envelope finishes.
//
// Capacity is a modern choice. 1, the default, keeps at most one call in
// flight, as the synchronous native dispatch does; it reproduces no native
// mechanism. 2..32 have no native counterpart: calls in flight on the same
// globals interleave their saves and restores of 10E8078/10E807C, and the
// last call to finish writes its own saved owner. VmfFsDefaultCallback needs
// at least this capacity: otherwise "Router pool is full" faults the
// envelope at callback_begin, after the 9A pops, the log and leave, without
// enter. A callback that closes the movie or destroys the script's origin
// has no established native effect; the port only relies on the program and
// origin checks of each run, which fault the context and abandon a call
// still in flight. A bound 9A URL is read live (see VmfHostUrlRequest).
//
// Modern adapter, no native counterpart. Stale or foreign tickets are
// invalid_ticket and calls during a slot's own run are reentry, both
// without change; a closing adapter refuses everything. An incoherent
// request frees its reservation (invalid_request); begin_url on an in-flight
// ticket, or resume/inspect on a merely reserved one, is refused unchanged.
// Envelope errors free the slot (command) and keep its executed prefix.
// Freeing a slot changes its generation before any foreign release runs.
class VmfFsHostActions final : public VmfHostActions {
  public:
    // Allocates 1..32 slots once; other capacities throw invalid_argument.
    explicit VmfFsHostActions(const VmfFsCommandInputs& inputs,
                              std::size_t capacity = 1);
    ~VmfFsHostActions() noexcept override;
    VmfFsHostActions(const VmfFsHostActions&) = delete;
    VmfFsHostActions& operator=(const VmfFsHostActions&) = delete;
    VmfFsHostActions(VmfFsHostActions&&) = delete;
    VmfFsHostActions& operator=(VmfFsHostActions&&) = delete;

    bool accepts(std::uint16_t opcode) const noexcept override; // 83, 9A
    std::expected<VmfHostTicket, VmfHostActionError>
    reserve(std::uint16_t opcode) noexcept override;
    std::expected<VmfHostActionReport, VmfHostActionError>
    begin_url(VmfHostTicket ticket, const VmfHostUrlRequest& request,
              std::size_t budget) noexcept override;
    std::expected<VmfHostActionReport, VmfHostActionError>
    resume(VmfHostTicket ticket, std::size_t budget) noexcept override;
    std::expected<void, VmfHostActionError>
    cancel(VmfHostTicket ticket) noexcept override;

    // Diagnostic copy of an in-flight invocation (exact envelope status).
    [[nodiscard]] std::expected<VmfFsHostSnapshot, VmfHostActionError>
    inspect(VmfHostTicket ticket) const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t leased() const noexcept; // Reserved+in flight.

  private:
    struct Slot;
    [[nodiscard]] Slot* find(VmfHostTicket ticket) const noexcept;
    [[nodiscard]] std::expected<VmfHostActionReport, VmfHostActionError>
    run(Slot& slot, std::size_t budget) noexcept;
    void release(Slot& slot) noexcept;
    const VmfFsCommandInputs* inputs_{};
    std::unique_ptr<Slot[]> slots_;
    std::size_t capacity_{};
    std::uint32_t serial_{};
    bool closing_{};
};

} // namespace tdur::ui

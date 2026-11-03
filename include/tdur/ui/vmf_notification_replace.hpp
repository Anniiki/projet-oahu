#pragma once

#include "tdur/ui/vmf_stack.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfNotificationReplaceErrorCode {
    invalid_context,
    invalid_capacity,
    source_range,
    invalid_reference,
    output_range,
    invalid_named_binding,
    invalid_named_result,
    named_failure,
    invalid_arguments,
    recursion_limit,
    allocation_failure,
};
struct VmfNotificationReplaceError {
    VmfNotificationReplaceErrorCode code;
    std::string_view detail; // Static storage, including backend failures.
    std::size_t offset{};
};

class VmfNotificationFormatSource {
  public:
    // Borrowed physical backing: keep it alive/stable and serialize accesses.
    // One virtual NUL is supplied at its end; beyond-end reads are refused.
    static VmfNotificationFormatSource
    borrowed(std::span<const char> bytes) noexcept;
    // Retains backing, not contents. Uses the full physical extent, not text().
    static std::expected<VmfNotificationFormatSource,
                         VmfNotificationReplaceError>
    retained(const VmfTextReference& text) noexcept;
    std::span<const char> bytes() const noexcept;

  private:
    std::span<const char> bytes_;
    std::optional<VmfTextReference> keeper_;
};
struct VmfNotificationNamedRequest {
    VmfNotificationFormatSource format;
    // Start of the body in physical backing. Includes closing marker/tail;
    // 7178C0 copies its own512-byte scratch, not a pre-extracted token.
    std::size_t offset;
};
enum class VmfNotificationNamedStatus { finished, step_limit, needs_inputs };
struct VmfNotificationNamedReport {
    VmfNotificationNamedStatus status;
    std::size_t steps{};
    // Finished only: initialized bytes containing an actual NUL, <=512B.
    // Borrowed until the backend next runs/begins. Parent snapshots at return.
    std::span<const char> initialized_text;
};
class VmfNotificationNamedBackend {
  public:
    virtual ~VmfNotificationNamedBackend() = default;
    // Must execute the reconstructed7178C0 implementation, including actual
    // effects. A fixture must be declared as such; availability is not success.
    // One active invocation, serialized; backend outlives the parent context.
    // begin captures its key/sources once. Retries only call run, never begin.
    virtual std::expected<void, VmfNotificationReplaceError>
    begin(const VmfNotificationNamedRequest& request) noexcept = 0;
    virtual std::expected<VmfNotificationNamedReport,
                          VmfNotificationReplaceError>
    run(std::size_t step_budget) noexcept = 0;
};

using VmfNotificationPositionalArguments =
    std::array<std::optional<VmfTextReference>, 10>;
// Explicit lifetime-bounded alternatives, including live numeric arguments
// in recursive currency formatting. Each missing element is a known NULL.
using VmfNotificationBorrowedArguments =
    std::array<std::optional<VmfNotificationFormatSource>, 10>;
struct VmfNotificationReplaceInputs {
    std::optional<VmfNotificationFormatSource> format;
    // Unknown binding differs from ten explicitly known NULL arguments.
    std::optional<VmfNotificationPositionalArguments> arguments;
    std::optional<VmfNotificationNamedBackend*> named_backend;
    // Supply either arguments OR borrowed_arguments, never both. The latter
    // retains a FormatSource owner when present, otherwise borrows its span.
    std::optional<VmfNotificationBorrowedArguments> borrowed_arguments;
};
enum class VmfNotificationReplacePhase {
    bind,
    iteration,
    positional,
    named_begin,
    named_run,
    named_copy,
    closing,
    advance,
    terminate,
    finished,
};
enum class VmfNotificationReplaceStatus {
    finished,
    step_limit,
    needs_format,
    needs_arguments,
    needs_named_backend,
    named_pending,
};
struct VmfNotificationReplaceSnapshot {
    // stores counts destination stores, including the final NUL.
    VmfNotificationReplacePhase phase{};
    std::size_t cursor{}, steps{}, stores{}, positional_copies{},
        named_begins{}, named_runs{};
    std::uint32_t logical_written{};
    bool delimiter_met{}, terminated{}, faulted{};
};
struct VmfNotificationReplaceReport {
    VmfNotificationReplaceStatus status;
    VmfNotificationReplaceSnapshot state;
    std::size_t steps{};
};

// 717BF0 outer replacement engine. All source/destination aliases are live;
// literal writes are forward, logical width is not clipped. Search/advance
// remain after replacement even when output is full. Native AL means delimiter
// encountered. Zero/>DWORD capacity and unbound reads are modern refusals.
// Format/arguments are captured once; backing contents remain live. A selected
// backend persists across waits/tokens; completed effects are never replayed.
// No allocations in this engine. Dependencies may allocate. Native x86 va_list
// words are never reinterpreted as x64 pointers. Destination must remain
// stable. A context may not move or be used concurrently/reentrantly. Errors
// preserve previous stores, fault the context, and do not add a success
// terminator.
class VmfNotificationReplaceContext {
  public:
    explicit VmfNotificationReplaceContext(
        std::span<char> destination) noexcept;
    VmfNotificationReplaceContext(const VmfNotificationReplaceContext&) =
        delete;
    VmfNotificationReplaceContext&
    operator=(const VmfNotificationReplaceContext&) = delete;
    VmfNotificationReplaceContext(VmfNotificationReplaceContext&&) = delete;
    VmfNotificationReplaceContext&
    operator=(VmfNotificationReplaceContext&&) = delete;
    VmfNotificationReplaceSnapshot snapshot() const noexcept;
    std::expected<VmfNotificationReplaceReport, VmfNotificationReplaceError>
    run(const VmfNotificationReplaceInputs& inputs,
        std::size_t step_budget = 4096) noexcept;

  private:
    std::expected<unsigned, VmfNotificationReplaceError>
    byte(std::size_t at) const noexcept;
    std::expected<bool, VmfNotificationReplaceError>
    marker(std::size_t at, std::uint32_t width) const noexcept;
    std::expected<void, VmfNotificationReplaceError>
    advance(std::uint32_t width) noexcept;
    std::expected<void, VmfNotificationReplaceError>
    copy(std::string_view source) noexcept;
    std::span<char> destination_;
    std::optional<VmfNotificationFormatSource> format_;
    std::optional<VmfNotificationPositionalArguments> arguments_;
    std::optional<VmfNotificationBorrowedArguments> borrowed_arguments_;
    VmfNotificationNamedBackend* backend_{};
    VmfNotificationReplaceSnapshot state_;
    std::array<char, 512> named_output_{};
    std::size_t named_bytes_{};
    unsigned argument_index_{}, closing_byte_{};
    bool closing_loaded_{}, running_{};
};

} // namespace tdur::ui

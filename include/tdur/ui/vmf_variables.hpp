#pragma once

#include "tdur/ui/vmf_stack.hpp"
#include "tdur/ui/vmf_variable_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfVariableKind : std::uint32_t {
    empty = 0,
    number = 1,
    linked_text = 2,
    owned_text = 4,
    external_text = 5
};
struct VmfVariableValue {
    VmfVariableKind kind{};
    std::uint32_t number_bits{};
    std::array<char, 32> text{};
    // Owning reference, observing changes in explicitly shared text backing.
    // Meaningful only for linked/external text; no type5 producer is inferred.
    std::optional<VmfTextReference> linked;
};
struct VmfVariableSlot {
    std::uint32_t index{}; // Diagnostic position, never a stable handle.
    std::uint32_t scope{}; // Native scope0 is valid and distinct from absence.
    std::optional<std::uint32_t> next;
    std::array<char, 32> key{};
    VmfVariableValue value;
};
struct VmfVariableLimits {
    // Modern setting: the native G+0 capacity is not established globally.
    std::uint32_t capacity = 4096;
    // Includes the name's NUL. Longer views are admitted if an early NUL fits.
    std::size_t max_name_bytes = 64U * 1024U;
    // Stable backing extents, counting repeated active links repeatedly.
    std::size_t max_linked_reference_bytes = 4U * 1024U * 1024U;
    // Two fixed slot tables (transactional erase) and name scratch, excluding
    // object/allocator overhead and external backing owned by caller/source.
    std::size_t max_storage_bytes = 64U * 1024U * 1024U;
};
enum class VmfVariableErrorCode {
    invalid_limits,
    allocation_failure,
    resource_limit,
    invalid_state,
    invalid_value,
    invalid_slot,
    stack,
    // Native insertion returns NULL. Host wrappers still arm flags200.
    table_full
};
struct VmfVariableError {
    VmfVariableErrorCode code{};
    std::string_view detail;
    std::optional<VmfStackError> stack_error;
};

struct VmfVariableTextWrite {
    std::uint32_t scope{};
    // Borrowed for this call only. Absence of a write is separate from an
    // empty name, which remains a real key.
    std::string_view name;
    std::string_view text;
};
struct VmfVariableTextCleanupReport {
    std::uint32_t erased{};
    bool text_stored{};
    bool text_table_full{};
};

// Fixed coalesced table4434B0/4435D0 with typed moves443430 and cleanup4438D0.
// Keys are STORED on31 bytes, but hashed/looked up using the full C-string.
// Equality uses the calling CRT/thread's LC_CTYPE; no Unicode fold or original
// MSVCR71 non-ASCII locale equivalence is asserted. No callbacks/reentrancy.
// Slot snapshots own their links/values; retaining one does not pin a slot.
// Creation allocates fixed storage. Operations do not allocate ordinary heap.
// All access is serialized, including const calls using the name scratch.
class VmfVariables {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<VmfVariables>,
                                       VmfVariableError>
    create(VmfVariableLimits limits = {}) noexcept;
    ~VmfVariables();
    VmfVariables(const VmfVariables &) = delete;
    VmfVariables &operator=(const VmfVariables &) = delete;
    VmfVariables(VmfVariables &&) = delete;
    VmfVariables &operator=(VmfVariables &&) = delete;

    [[nodiscard]] std::uint32_t size() const noexcept;
    [[nodiscard]] std::uint32_t capacity() const noexcept;
    [[nodiscard]] std::size_t linked_reference_bytes() const noexcept;
    [[nodiscard]] std::expected<std::optional<VmfVariableSlot>,
                                VmfVariableError>
    find(std::uint32_t scope, std::string_view name) const noexcept;
    [[nodiscard]] std::expected<VmfVariableSlot, VmfVariableError>
    inspect_slot(std::uint32_t index) const noexcept;
    [[nodiscard]] std::expected<void, VmfVariableError>
    set_number_bits(std::uint32_t scope, std::string_view name,
                    std::uint32_t bits) noexcept;
    [[nodiscard]] std::expected<void, VmfVariableError>
    set_text(std::uint32_t scope, std::string_view name,
             std::string_view text) noexcept;
    [[nodiscard]] std::expected<void, VmfVariableError>
    set_linked(std::uint32_t scope, std::string_view name,
               const VmfTextReference &linked) noexcept;
    // Missing leaves the stack unchanged and returns false. The host's
    // GetVariable wrapper owns its separate missing=>Push("") behavior.
    [[nodiscard]] std::expected<bool, VmfVariableError>
    push(std::uint32_t scope, std::string_view name,
         VmfStack &stack) const noexcept;
    // Helper4436A0: missing writes NUL and returns false. Present formats
    // numbers with442330, including its precision cap. A too-small destination
    // is refused without writing; a successful missing lookup writes NUL.
    [[nodiscard]] std::expected<bool, VmfVariableError>
    copy_text(std::uint32_t scope, std::string_view name,
              std::span<char> destination) const noexcept;
    // Native selection: nonzero selects that scope; zero selects NONZERO
    // scopes. Preserve the table's index scan and typed compaction order,
    // including a survivor shifted behind the scan. Invalid chains refuse
    // the whole erase without changing the table.
    [[nodiscard]] std::expected<std::uint32_t, VmfVariableError>
    erase_scope(std::uint32_t scope) noexcept;
    // Modern transaction around the same ordered native erases. Do not merge
    // scopes: compaction order and scope0's nonzero selection are observable.
    // An error in any erase leaves the entire active table unchanged. An empty
    // sequence is a no-op. Uses the existing work table, without allocation.
    [[nodiscard]] std::expected<std::uint32_t, VmfVariableError>
    erase_scopes(std::span<const std::uint32_t> scopes) noexcept;
    // Display37 initialization followed by the same ordered erases. A full
    // table is the native void-setter no-op; erases still follow it, without
    // retrying the insertion into a later hole. Other errors publish neither
    // the write nor any erase. No erases => existing atomic setter fast path.
    // No ordinary heap allocation, callbacks or interpreter-wide batching.
    [[nodiscard]] std::expected<VmfVariableTextCleanupReport, VmfVariableError>
    write_text_and_erase_scopes(std::optional<VmfVariableTextWrite> write,
                                std::span<const std::uint32_t> scopes) noexcept;

  private:
    struct Storage;
    explicit VmfVariables(std::unique_ptr<Storage> storage) noexcept;
    std::unique_ptr<Storage> storage_;
};

} // namespace tdur::ui

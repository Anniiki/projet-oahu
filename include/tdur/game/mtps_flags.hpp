#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game {

// Owned fields, not a native ABI or guessed class hierarchy. Unknown fields
// remain unbound; engaged nullptr is an established NULL pointer.
struct MtpsFlagType {
    std::optional<std::uint32_t> signature;
};
struct MtpsFlagObject {
    std::optional<MtpsFlagType*> type;     // Native +24.
    std::optional<std::uint32_t> identity; // +3D0.
    std::optional<std::uint32_t> flags;    // +1C.
};
using MtpsFlagSlot = std::optional<MtpsFlagObject*>;
struct MtpsFlagBucket {
    std::optional<std::uint32_t> kind; // Derived subobject +2A4.
    std::optional<std::int32_t> count; // Masked subobject +EEC4.
    // Inline array beginning at +EECC, not a native nullable list pointer.
    std::optional<std::span<MtpsFlagSlot>> slots;
};
struct MtpsFlagRoot {
    std::array<MtpsFlagBucket, 8> buckets;
};
struct MtpsFlagFilter {
    std::optional<std::uint8_t> enabled;
    std::array<std::optional<std::uint32_t>, 8> identities;
};
struct MtpsFlagInputs {
    // Only zero/nonzero is tested; no pointer following at10E7F6C.
    std::optional<std::uintptr_t> presence_10e7f6c;
    std::optional<MtpsFlagRoot*> root_fa48b0;
};

enum class MtpsFlagRoutine { selective_7d6b80, activation_7e7a00 };
enum class MtpsFlagPhase {
    presence,
    root,
    kind_first,
    kind_second,
    kind_mask,
    initial_count,
    slots,
    first_slot,
    type,
    signature,
    filter_flag,
    identity,
    filter_scan,
    first_write,
    second_slot,
    second_write,
    next_count,
    next_bucket,
    finished,
};
enum class MtpsFlagStatus {
    finished,
    step_limit,
    needs_presence,
    needs_root,
    needs_kind,
    needs_count,
    needs_slots,
    needs_slot,
    needs_type,
    needs_signature,
    needs_filter_flag,
    needs_identity,
    needs_filter_identity,
    needs_flags,
};
enum class MtpsFlagErrorCode {
    invalid_context,
    null_masked_bucket,
    row_out_of_range,
    null_second_object,
};
struct MtpsFlagError {
    MtpsFlagErrorCode code;
    std::string_view detail;
};
struct MtpsFlagSnapshot {
    MtpsFlagRoutine routine;
    MtpsFlagPhase phase;
    std::size_t steps{}, bucket{}, row{}, filter_index{};
    std::size_t buckets_completed{}, entries_visited{}, filter_comparisons{};
    std::size_t flag_writes{};
    bool faulted{};
};
struct MtpsFlagReport {
    MtpsFlagStatus status;
    MtpsFlagSnapshot state;
    std::size_t steps;
};

// Admitted-data reconstruction of7D6B80 and7E7A00. Root/buckets and filter
// must represent valid owned storage with nonwrapping native derived
// addresses. Invalid native pointer arithmetic is outside this domain.
// Null root/entry/type are actual guards; the third kind read producing a
// null masked bucket and a NULL second slot would be native dereferences.
// Those cases are explicit modern faults, never invented success guards.
// Each bucket reloads the globals; its selected bucket/list base remains
// retained. Kind/count/slots/filter/flags are read in native order. The two
// stores reload the slot between them, WITHOUT repeating type/filter guards.
// All storage must live without relocation, accesses must be serialized.
// Values may change between runs; waiting and budgets are modern boundaries.
class MtpsFlagContext {
  public:
    // Fixed valid argument buffer. enabled maps+A0, identities map+A4..C0.
    [[nodiscard]] static MtpsFlagContext
    selective(MtpsFlagFilter& argument_buffer) noexcept;
    // Fixed global filter owner:120BE98,120BE9C..BEB8. Only ARG1's low byte
    // is tested. Argument low byte zero never reads filter or object ID.
    [[nodiscard]] static MtpsFlagContext
    activation(std::uint32_t argument, MtpsFlagFilter& global_filter) noexcept;
    MtpsFlagContext(const MtpsFlagContext&) = delete;
    MtpsFlagContext& operator=(const MtpsFlagContext&) = delete;
    MtpsFlagContext(MtpsFlagContext&& other) noexcept;
    MtpsFlagContext& operator=(MtpsFlagContext&& other) noexcept;
    [[nodiscard]] MtpsFlagSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<MtpsFlagReport, MtpsFlagError>
    run(const MtpsFlagInputs& inputs, std::size_t step_budget = 4096) noexcept;

  private:
    MtpsFlagContext(MtpsFlagRoutine routine, std::uint32_t argument,
                    MtpsFlagFilter& filter) noexcept;
    MtpsFlagSnapshot state_;
    MtpsFlagFilter* filter_{};
    MtpsFlagBucket* bucket_{};
    std::span<MtpsFlagSlot> slots_;
    MtpsFlagObject* object_{};
    MtpsFlagType* type_{};
    std::uint32_t argument_{}, identity_{};
    std::uint8_t filter_enabled_{};
    bool set_flags_{}, valid_ = true, running_{};
};

} // namespace tdur::game

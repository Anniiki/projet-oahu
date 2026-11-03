#pragma once

#include "tdur/assets/database.hpp"
#include "tdur/ui/vmf_host_variables.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

struct VmfMenuDataRow {
    // Absence models the zero guards at9ABA6D/9ABA74. Present empty text
    // remains a real string, distinct from a native NULL.
    std::optional<VmfTextReference> expression;
    std::optional<VmfTextReference> text;
    std::uint32_t flags{};
};

struct VmfMenuDataLimits {
    std::size_t max_rows = 100'000;
    std::size_t max_catalog_entries = 100'000;
    std::size_t max_reference_bytes = 64U * 1024U;
    // Counts backing extents of repeated references repeatedly.
    std::size_t max_linked_bytes = 8U * 1024U * 1024U;
};

enum class VmfMenuDataErrorCode {
    invalid_schema,
    invalid_reference,
    reference_not_found,
    invalid_flags,
    resource_limit,
    allocation_failure,
    invalid_context,
    host_variables,
    invalid_number,
};
struct VmfMenuDataError {
    VmfMenuDataErrorCode code{};
    std::size_t row{};
    std::string_view detail; // Static; execution errors do not allocate.
    std::optional<VmfHostVariableError> host_error{};
};

// Owns the references used by the native9AB950 row loop. TDU_Menus is table0,
// stride24: u/h reference fields at0/8, BitField at16; word20 is unobserved.
// Native9AE4D0/9AE600 choose the FIRST matching 64-bit string ID. They do not
// reject duplicate catalog definitions. Asset construction admits the proved
// ASCII decimal uint64 ID domain; malformed/overflowing IDs are modern errors.
// No original pointers, commercial constants, assets or activation data.
class VmfMenuDatabase {
  public:
    [[nodiscard]] static std::expected<VmfMenuDatabase, VmfMenuDataError>
    from_table(const assets::DatabaseTable& table,
               const assets::DatabaseStrings& strings,
               VmfMenuDataLimits limits = {}) noexcept;
    // Explicit owned backing also permits synthetic rows and native zero
    // guards. Mutable references remain live; serialize accesses to them.
    [[nodiscard]] static std::expected<VmfMenuDatabase, VmfMenuDataError>
    from_rows(std::span<const VmfMenuDataRow> rows,
              VmfMenuDataLimits limits = {}) noexcept;
    [[nodiscard]] std::span<const VmfMenuDataRow> rows() const noexcept;

  private:
    struct Storage;
    explicit VmfMenuDatabase(std::shared_ptr<const Storage> storage) noexcept;
    friend class VmfMenuBindingContext;
    std::shared_ptr<const Storage> storage_;
};

enum class VmfMenuBindingStatus {
    rows_finished,
    row_limit,
    needs_indirect_service,
};
struct VmfMenuBindingSnapshot {
    std::size_t next_row{};
    std::size_t selected_rows{}; // Includes selected rows skipped by guards.
    std::uint32_t mask{};
    bool faulted{};
};
struct VmfMenuBindingReport {
    VmfMenuBindingStatus status{};
    VmfMenuBindingSnapshot state;
    std::size_t rows_visited{};     // This invocation only.
    std::size_t writes_attempted{}; // Host calls, not successful insertions.
};

// Resumable row phase9ABA13..9ABAA8. Reads the nine switches ONCE at create,
// then preserves source order, live links, guards and the selected-row count.
// Errors retain completed prefix effects and fault this context; row-limit
// resumes never replay them. An unported bit31 service stops UNCONSUMED.
// rows_finished is not sbdd completion: common initialization,9AB830 and the
// enclosing receiver/handler remain separate required services.
// Keep the bound display alive; all display/backing accesses are serialized.
class VmfMenuBindingContext {
  public:
    [[nodiscard]] static std::expected<VmfMenuBindingContext, VmfMenuDataError>
    create(VmfDisplayRuntime& display, VmfMenuDatabase data,
           std::uint32_t initial_flags,
           VmfTargetLookupLimits target_limits = {}) noexcept;
    VmfMenuBindingContext(const VmfMenuBindingContext&) = delete;
    VmfMenuBindingContext& operator=(const VmfMenuBindingContext&) = delete;
    VmfMenuBindingContext(VmfMenuBindingContext&&) noexcept = default;
    VmfMenuBindingContext&
    operator=(VmfMenuBindingContext&&) noexcept = default;
    [[nodiscard]] VmfMenuBindingSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfMenuBindingReport, VmfMenuDataError>
    run(VmfDisplayRuntime& display, std::size_t row_budget = 100'000) noexcept;

  private:
    friend class VmfMenuFinalizerContext;
    [[nodiscard]] bool
    ready_for_finalization(const VmfDisplayRuntime& display) const noexcept;
    VmfMenuBindingContext(VmfDisplayRuntime& display, VmfMenuDatabase data,
                          std::uint32_t mask,
                          VmfTargetLookupLimits target_limits) noexcept;
    VmfDisplayRuntime* display_;
    VmfInstanceHandle root_;
    VmfMenuDatabase data_;
    VmfMenuBindingSnapshot state_;
    VmfTargetLookupLimits target_limits_;
};

} // namespace tdur::ui

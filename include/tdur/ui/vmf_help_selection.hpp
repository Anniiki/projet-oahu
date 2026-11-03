#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace tdur::ui {

// Owned modern fields observed in the native 64-byte activity records.
// Their remaining bytes and the meaning of the two category bytes are not
// inferred. Keep the selected record alive and serialize mutations.
struct VmfHelpActivity {
    std::uint64_t identity;
    std::uint8_t category_1c;
    std::uint8_t category_1d;
};

enum class VmfHelpQuery {
    world_gate_38,
    mode_11f32a0,
    secondary_11f3bd8,
    threshold_11f3da8,
    blocked_11f41b8,
    kind4_component_18,
    progress_flag_1c,
    progress_identity,
};

// Scalar queries retain all 32 bits (AL tests use only the low byte).
// kind4_component_18: nullopt means the component is confirmed absent;
// otherwise the service has invoked its virtual +18 and supplies its result.
// progress_flag_1c / progress_identity each invoke virtual +1C separately,
// then read the indicated fields of THAT returned object. A missing service
// is an outstanding request, never a zero result or an absent object.
using VmfHelpQueryValue =
    std::variant<std::uint32_t, std::optional<std::uint32_t>, std::uint64_t>;

enum class VmfHelpSelectionError {
    invalid_context,
    unexpected_query,
    wrong_value_type,
    unsafe_routing_index,
};

// Reconstructs 9812C0's complete branch/table selection. Mandatory inputs
// are established observations: null world_kind means known absent world;
// activities is the valid native record range, not an unknown registry.
// The initial selected-record pointer and world kind are captured once.
// Virtual services are explicit typed requests, in native order. Providing
// one advances the context once; retrying an old query is rejected.
class VmfHelpSelectionContext {
  public:
    [[nodiscard]] static VmfHelpSelectionContext
    create(std::optional<std::int32_t> world_kind,
           std::span<const VmfHelpActivity> activities,
           std::int32_t selected_activity, std::int32_t force) noexcept;
    VmfHelpSelectionContext(const VmfHelpSelectionContext&) = delete;
    VmfHelpSelectionContext& operator=(const VmfHelpSelectionContext&) = delete;
    VmfHelpSelectionContext(VmfHelpSelectionContext&& other) noexcept;
    VmfHelpSelectionContext&
    operator=(VmfHelpSelectionContext&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::optional<std::int32_t> result() const noexcept;
    [[nodiscard]] std::optional<VmfHelpQuery> pending_query() const noexcept;
    [[nodiscard]] std::size_t completed_queries() const noexcept;
    // Bad type/order leaves the pending request unconsumed.
    [[nodiscard]] std::expected<void, VmfHelpSelectionError>
    provide(VmfHelpQuery query, const VmfHelpQueryValue& value) noexcept;

  private:
    VmfHelpSelectionContext() noexcept = default;
    void finish(std::int32_t index) noexcept;
    void select_activity(std::int32_t world_kind) noexcept;
    const VmfHelpActivity* activity_{};
    std::optional<std::int32_t> result_;
    std::optional<VmfHelpQuery> pending_;
    std::size_t completed_{};
    bool valid_ = true;
};

// 980FB0's 32 functional routing identifiers, not localized tutorial text.
// -1 and >=32 return no tag. Other negative indices would read before the
// native stack table and are explicit modern errors.
[[nodiscard]] std::expected<std::optional<std::string_view>,
                            VmfHelpSelectionError>
vmf_help_routing_tag(std::int32_t index) noexcept;

} // namespace tdur::ui

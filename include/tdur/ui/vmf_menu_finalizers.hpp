#pragma once

#include "tdur/ui/vmf_menu_data.hpp"
#include "tdur/ui/vmf_menu_movie.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfMenuFinalizerErrorCode {
    invalid_context,
    rows_incomplete,
    invalid_catalog,
    invalid_reference,
    reference_not_found,
    resource_limit,
    allocation_failure,
    locale_limit,
    missing_metric_display,
    host_variables,
};
struct VmfMenuFinalizerError {
    VmfMenuFinalizerErrorCode code{};
    std::string_view detail;
    std::optional<VmfHostVariableError> host_error{};
};

// 9A96D0 compares three bytes including NUL;898960 uses just the first two.
//  Inputs are bounded C-string prefixes. In particular, "usx" selects units1
//  but is not the three-byte language code "us\0".
[[nodiscard]] std::uint32_t
vmf_menu_language_index(std::string_view code) noexcept;
[[nodiscard]] std::uint32_t vmf_menu_unit_mode(std::string_view code) noexcept;

struct VmfMenuLanguageBinding {
    std::uint32_t language{};
    std::optional<VmfTextReference> text;
};
class VmfMenuLanguageTable {
  public:
    // Five16-byte source records: ID64, language32, unused32. The caller
    // obtains the80 bytes from its owned installation; no game IDs embedded.
    // First definition of each numeric ID wins. Missing texts remain absent
    // until selected, preserving the native ordering of earlier effects.
    [[nodiscard]] static std::expected<VmfMenuLanguageTable,
                                       VmfMenuFinalizerError>
    from_catalog(std::span<const std::byte> records,
                 const assets::DatabaseStrings& catalog,
                 VmfMenuDataLimits limits = {}) noexcept;
    [[nodiscard]] static std::expected<VmfMenuLanguageTable,
                                       VmfMenuFinalizerError>
    from_bindings(std::array<VmfMenuLanguageBinding, 5> bindings,
                  VmfMenuDataLimits limits = {}) noexcept;
    // 9AB850: first matching language tag; no match selects record0.
    [[nodiscard]] const VmfMenuLanguageBinding&
    select(std::uint32_t language) const noexcept;

  private:
    std::array<VmfMenuLanguageBinding, 5> bindings_;
};

class VmfMenuLocaleState {
  public:
    [[nodiscard]] static std::expected<VmfMenuLocaleState,
                                       VmfMenuFinalizerError>
    create(std::string_view source, std::string_view cached,
           std::uint32_t unit_mode) noexcept;
    [[nodiscard]] std::string_view source() const noexcept;
    [[nodiscard]] std::string_view cached() const noexcept;
    // Includes NUL and preserved tail bytes. Borrowed until this locale moves
    // or dies; mutations remain visible through existing spans.
    [[nodiscard]] std::span<const char, 64> cached_storage() const noexcept;
    [[nodiscard]] std::expected<void, VmfMenuFinalizerError>
    set_source(std::string_view value) noexcept;
    [[nodiscard]] std::expected<void, VmfMenuFinalizerError>
    set_cached(std::string_view value) noexcept;
    [[nodiscard]] std::uint32_t unit_mode() const noexcept;
    void set_unit_mode(std::uint32_t value) noexcept;

  private:
    // Modern bounded owned storage, not a claim of a64-byte native field.
    std::array<char, 64> source_{}, cached_{};
    std::uint32_t unit_mode_{};
};

// 99CC40 removes ONE matching callback, shifts whole following records, then
//  clears only the first payload byte and callback of the vacated last slot.
//  Keys identify reconstructed callbacks; they are never native code pointers
//  to invoke. uintptr_t permits the reconstructed x64 runtime's identities.
struct VmfMenuEventEntry {
    std::array<std::byte, 32> payload{};
    std::uintptr_t callback{};
    std::array<std::byte, 4> trailing{};
    friend bool operator==(const VmfMenuEventEntry&,
                           const VmfMenuEventEntry&) = default;
};
struct VmfMenuEventQueue {
    std::array<VmfMenuEventEntry, 32> entries{};
    std::size_t count{};
};
[[nodiscard]] std::expected<bool, VmfMenuFinalizerError>
remove_vmf_menu_callback(VmfMenuEventQueue& queue,
                         std::uintptr_t callback) noexcept;
struct VmfMenuUnitCallbackKeys {
    std::uintptr_t callback_6e7910{}, callback_6e77c0{};
    friend bool operator==(const VmfMenuUnitCallbackKeys&,
                           const VmfMenuUnitCallbackKeys&) = default;
};

// 6E8A10 copies six raw words, including five float bit patterns, without
//  arithmetic. Mode0 selects +170..184; every nonzero mode selects +158..16C.
struct VmfMenuGameUnitState {
    std::uint8_t flag_3263{}, flag_3262{};
    std::array<std::uint32_t, 6> parameters_zero{}, parameters_nonzero{};
    std::array<std::uint32_t, 6> parameters_current{};
    VmfDisplayRuntime* speed_display{};
    // Source strings from the owned game's +3B14 (mode1) / +3B18 (others).
    std::array<std::optional<VmfTextReference>, 2> speed_labels{};
};
struct VmfMenuFinalizerInputs {
    // nullopt: unresolved service. Engaged nullptr: confirmed absent global.
    // Never infer absence from a zero-filled PE initializer.
    std::optional<VmfMenuGameUnitState*> game;
    VmfMenuEventQueue* events{};
    std::optional<VmfMenuUnitCallbackKeys> callback_keys;
    // nullopt: unresolved registry; engaged empty span: known empty registry.
    std::optional<std::span<const VmfMenuMovie>> movies;
};

enum class VmfMenuFinalizerPhase {
    binding_cache,
    language_cache,
    language_write,
    finished,
};
enum class VmfMenuUnitStep {
    inactive,
    store_mode,
    game_binding,
    remove_first,
    clear_3263,
    remove_second,
    remove_third,
    clear_3262,
    parameters,
    speed_label,
    metric_first,
    metric_second,
};
enum class VmfMenuFinalizerStatus {
    bindings_finished, // Only9AB950 tail, not enclosing sbdd/FSCommand.
    step_limit,
    needs_game_state,
    needs_event_services,
    needs_speed_label,
    needs_movie_registry,
    language_ready, // Only898B40, including its full units pass when needed.
};
struct VmfMenuFinalizerSnapshot {
    VmfMenuFinalizerPhase phase{};
    VmfMenuUnitStep unit_step{};
    std::size_t steps{}, unit_passes{}, callback_remove_calls{};
    std::size_t speed_writes{}, metric_writes{}, language_writes{};
    bool faulted{};
};
struct VmfMenuFinalizerReport {
    VmfMenuFinalizerStatus status{};
    VmfMenuFinalizerSnapshot state;
    std::size_t steps{}; // This invocation only.
};

// Completes the post-row9AB950 tail and9AB830, including898B40/898960,
// 898890,6E8A10 and99CC40. Source behavior is serialized over owned objects.
// Cache/units/queue effects survive a missing service or step-budget yield.
// Errors fault the context; no prefix is replayed. All bound objects must
// remain alive. Game/event identities cannot change during an active pass.
class VmfMenuFinalizerContext {
  public:
    // Success consumes the row context, preventing a second tail from the
    // same invocation. A failed precondition leaves the rows resumable.
    [[nodiscard]] static std::expected<VmfMenuFinalizerContext,
                                       VmfMenuFinalizerError>
    after_rows(VmfMenuBindingContext&& rows, VmfDisplayRuntime& display,
               VmfMenuLocaleState& locale,
               VmfMenuLanguageTable languages) noexcept;
    // Standalone898B40: no rows, display root or localized label table.
    // Cache copy and the same real unit pass are retained across waits.
    [[nodiscard]] static VmfMenuFinalizerContext
    refresh_language(VmfMenuLocaleState& locale,
                     VmfTargetLookupLimits limits = {}) noexcept;
    VmfMenuFinalizerContext(const VmfMenuFinalizerContext&) = delete;
    VmfMenuFinalizerContext& operator=(const VmfMenuFinalizerContext&) = delete;
    VmfMenuFinalizerContext(VmfMenuFinalizerContext&& other) noexcept;
    VmfMenuFinalizerContext&
    operator=(VmfMenuFinalizerContext&& other) noexcept;
    [[nodiscard]] VmfMenuFinalizerSnapshot snapshot() const noexcept;
    [[nodiscard]] std::expected<VmfMenuFinalizerReport, VmfMenuFinalizerError>
    run(VmfDisplayRuntime& display, VmfMenuLocaleState& locale,
        const VmfMenuFinalizerInputs& inputs,
        std::size_t step_budget = 4096) noexcept;
    [[nodiscard]] std::expected<VmfMenuFinalizerReport, VmfMenuFinalizerError>
    run_language(VmfMenuLocaleState& locale,
                 const VmfMenuFinalizerInputs& inputs,
                 std::size_t step_budget = 4096) noexcept;

  private:
    VmfMenuFinalizerContext(VmfMenuBindingContext&& rows,
                            VmfDisplayRuntime& display,
                            VmfMenuLocaleState& locale,
                            VmfMenuLanguageTable languages,
                            VmfTargetLookupLimits target_limits) noexcept;
    VmfMenuFinalizerContext(VmfMenuLocaleState& locale,
                            VmfTargetLookupLimits limits) noexcept;
    [[nodiscard]] std::expected<VmfMenuFinalizerReport, VmfMenuFinalizerError>
    run_impl(VmfDisplayRuntime* display, VmfMenuLocaleState& locale,
             const VmfMenuFinalizerInputs& inputs,
             std::size_t step_budget) noexcept;
    std::optional<VmfMenuBindingContext> rows_;
    VmfDisplayRuntime* display_{};
    VmfInstanceHandle root_{};
    VmfMenuLocaleState* locale_{};
    VmfMenuLanguageTable languages_;
    VmfTargetLookupLimits target_limits_;
    VmfMenuFinalizerSnapshot state_{};
    std::uint32_t pending_mode_{};
    std::int32_t pending_metric_{};
    VmfMenuGameUnitState* game_{};
    VmfMenuEventQueue* events_{};
    VmfMenuUnitCallbackKeys callbacks_{};
    bool language_only_{};
};

} // namespace tdur::ui

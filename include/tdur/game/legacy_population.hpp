#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace tdur::game {

inline constexpr std::uint32_t legacy_population_adjustment_calls = 1000;
inline constexpr std::uint32_t legacy_population_list_capacity = 16;
inline constexpr std::string_view legacy_population_retirement_reason =
    "too many bots";

// Logical fields of the object passed in EAX to 0090C760, not a packed native
// object. target_bits preserves wrapping arithmetic and signed comparisons.
struct LegacyPopulationFields {
    std::uint32_t call_counter{}; // Native +4.
    std::uint32_t target_bits{};  // Native +8.
};

using LegacyPopulationPlayerWords = std::array<std::uint32_t, 4>;

class LegacyPopulationPlayer {
  public:
    virtual ~LegacyPopulationPlayer() = default;
    // Copies the four DWORDs returned through the player's vtable slot +C.
    // Their geometric meaning is deliberately not imposed by this interface.
    virtual LegacyPopulationPlayerWords sample_words() = 0;
};

struct LegacyPopulationController {
    std::int32_t raw_mode{}; // Native controller +C, must equal 2 to retire.
    float raw_score{};       // Native controller +34; meaning not established.
};

struct LegacyPopulationAgent {
    bool vehicle_present{}; // Native agent +21C is nonnull.
    std::shared_ptr<LegacyPopulationController> controller; // Agent +22D8.
    float raw_threshold{};                                  // Agent +98.
    std::uint8_t raw_retirement_requested{};                // Agent +22E4.
};

struct LegacyPopulationManager {
    // Native manager +2444C contains sixteen pointer slots before +2448C.
    std::array<std::shared_ptr<LegacyPopulationAgent>,
               legacy_population_list_capacity>
        agents;
    std::int32_t raw_count{}; // Native +2448C, tested as signed.
};

struct LegacyPopulationGlobals {
    std::uint32_t target_boundary{};        // Native 0143F738, equality only.
    std::int32_t classification_five_cap{}; // Native 0143F73C, signed minimum.
    std::shared_ptr<LegacyPopulationPlayer> player;   // Native 01214790.
    std::shared_ptr<LegacyPopulationManager> manager; // Native 013A70EC.
};

class LegacyPopulationServices {
  public:
    virtual ~LegacyPopulationServices() = default;
    // Typed boundary for 0090C410, not a substitute implementation of it.
    virtual std::int32_t
    classify(LegacyPopulationFields& fields,
             const LegacyPopulationPlayerWords& copied_player_words) = 0;
    // Boundary for 00923A90. The byte is already set before this callback.
    // No boolean result, clock argument, or completion of removal is implied.
    virtual void request_retirement(LegacyPopulationAgent& agent,
                                    float raw_delay_seconds,
                                    std::string_view reason) = 0;
};

enum class LegacyPopulationBudgetDecision {
    not_due,
    zero_to_one,
    boundary_decrement,
    random_increment,
    random_decrement,
};

enum class LegacyPopulationRetirementDecision {
    no_surplus,
    no_positive_candidate,
    threshold_not_exceeded,
    wrong_control_mode,
    requested,
};

// Diagnostics of the C++ reconstruction, not the native EAX return value.
struct LegacyPopulationResult {
    LegacyPopulationBudgetDecision budget =
        LegacyPopulationBudgetDecision::not_due;
    std::optional<std::uint32_t> random_draw;
    std::optional<std::int32_t> classification;
    std::optional<std::uint32_t> selected_index;
    LegacyPopulationRetirementDecision retirement =
        LegacyPopulationRetirementDecision::no_surplus;
};

enum class LegacyPopulationErrorCode {
    missing_manager,
    count_exceeds_slots,
    missing_agent,
    missing_controller,
};

struct LegacyPopulationError {
    LegacyPopulationErrorCode code{};
    std::optional<std::uint32_t> index;
};

// Autonomous reconstruction of 0090C760; no scheduling or native access.
// shared_random_state is the shared DWORD reached through native 013A8458.
// It may alias either DWORD in fields: the RNG write precedes the target
// reread. The caller serializes all access, keeps fields/globals/services
// alive, and masks floating-point invalid exceptions as on the admitted
// native path. No fast-math assumptions are permitted for the comparisons.
//
// Callbacks may synchronously mutate/rebind or reenter. Each callback's player
// and the selected manager/agent are pinned for the required lifetime. Bound
// object destructors must not throw, mutate, or reenter this operation.
//
// Invalid pointers/counts are reported only when the corresponding native
// path would dereference them. Prior changes remain applied on error or a
// propagated callback exception; no rollback or automatic retry occurs.
[[nodiscard]] std::expected<LegacyPopulationResult, LegacyPopulationError>
legacy_population_step(LegacyPopulationFields& fields,
                       LegacyPopulationGlobals& globals,
                       std::uint32_t& shared_random_state,
                       LegacyPopulationServices& services);

} // namespace tdur::game

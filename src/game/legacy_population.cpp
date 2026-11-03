#include "tdur/game/legacy_population.hpp"

#include <bit>

namespace tdur::game {
namespace {
std::int32_t signed_target(const LegacyPopulationFields& fields) noexcept {
    return std::bit_cast<std::int32_t>(fields.target_bits);
}

std::unexpected<LegacyPopulationError>
invalid(LegacyPopulationErrorCode code,
        std::optional<std::uint32_t> index = {}) {
    return std::unexpected(LegacyPopulationError{code, index});
}
} // namespace

std::expected<LegacyPopulationResult, LegacyPopulationError>
legacy_population_step(LegacyPopulationFields& fields,
                       LegacyPopulationGlobals& globals,
                       std::uint32_t& shared_random_state,
                       LegacyPopulationServices& services) {
    LegacyPopulationResult result;
    fields.call_counter += std::uint32_t{1};
    if (fields.call_counter >= legacy_population_adjustment_calls) {
        const auto previous_target = fields.target_bits;
        fields.call_counter = 0;
        if (previous_target == 0) {
            fields.target_bits = 1;
            result.budget = LegacyPopulationBudgetDecision::zero_to_one;
        } else if (previous_target == globals.target_boundary) {
            fields.target_bits = previous_target - std::uint32_t{1};
            result.budget = LegacyPopulationBudgetDecision::boundary_decrement;
        } else {
            shared_random_state =
                shared_random_state * std::uint32_t{0x7FF8A3ED} +
                std::uint32_t{0x2AA01D31};
            const auto draw = shared_random_state;
            result.random_draw = draw;
            // Native reloads +8 AFTER storing the shared RNG. Keep this
            // ordering even when the caller deliberately aliases both.
            const auto current_target = fields.target_bits;
            if (draw < 0x7FFFFFFFU) {
                fields.target_bits = current_target + std::uint32_t{1};
                result.budget =
                    LegacyPopulationBudgetDecision::random_increment;
            } else {
                fields.target_bits = current_target - std::uint32_t{1};
                result.budget =
                    LegacyPopulationBudgetDecision::random_decrement;
            }
        }
    }

    {
        const auto player = globals.player;
        if (player) {
            const auto words = player->sample_words();
            const auto classification = services.classify(fields, words);
            result.classification = classification;
            if (classification == 5) {
                // Both values are sampled after the callbacks. Equality also
                // writes the target in the native CMOVGE/MOV sequence.
                const auto target = signed_target(fields);
                const auto cap = globals.classification_five_cap;
                fields.target_bits =
                    std::bit_cast<std::uint32_t>(target >= cap ? cap : target);
            }
        }
    }

    // The binding is sampled only after player sampling/classification.
    const auto manager = globals.manager;
    if (!manager)
        return invalid(LegacyPopulationErrorCode::missing_manager);
    const auto count = manager->raw_count;
    if (count <= signed_target(fields) || count <= 0)
        return result;
    if (count > static_cast<std::int32_t>(legacy_population_list_capacity))
        return invalid(LegacyPopulationErrorCode::count_exceeds_slots);

    float best_score = 0.0F;
    std::optional<std::uint32_t> best_index;
    for (std::int32_t index = 0; index < count; ++index) {
        const auto slot = static_cast<std::uint32_t>(index);
        const auto& agent = manager->agents[slot];
        if (!agent)
            return invalid(LegacyPopulationErrorCode::missing_agent, slot);
        // The global player binding is read again per candidate; an earlier
        // callback may have removed it. Missing controllers on skipped paths
        // are not read, matching the native short-circuit order.
        if (!agent->vehicle_present || !globals.player)
            continue;
        const auto& controller = agent->controller;
        if (!controller)
            return invalid(LegacyPopulationErrorCode::missing_controller, slot);
        const auto score = controller->raw_score;
        if (score > best_score) {
            best_score = score;
            best_index = slot;
        }
    }
    if (!best_index) {
        result.retirement =
            LegacyPopulationRetirementDecision::no_positive_candidate;
        return result;
    }

    // Preserve the native second lookup and count reread. Under the host's
    // serialized access contract, count cannot change during the scan; its
    // native fallback to slot zero is consequently unreachable here too.
    const auto selected_index =
        static_cast<std::int32_t>(*best_index) < manager->raw_count
            ? *best_index
            : 0U;
    const auto agent = manager->agents[selected_index];
    result.selected_index = selected_index;
    if (!agent)
        return invalid(LegacyPopulationErrorCode::missing_agent,
                       selected_index);
    if (!(best_score > agent->raw_threshold)) {
        result.retirement =
            LegacyPopulationRetirementDecision::threshold_not_exceeded;
        return result;
    }
    const auto controller = agent->controller;
    if (!controller)
        return invalid(LegacyPopulationErrorCode::missing_controller,
                       selected_index);
    if (controller->raw_mode != 2) {
        result.retirement =
            LegacyPopulationRetirementDecision::wrong_control_mode;
        return result;
    }
    agent->raw_retirement_requested = 1;
    services.request_retirement(*agent, -1.0F,
                                legacy_population_retirement_reason);
    result.retirement = LegacyPopulationRetirementDecision::requested;
    return result;
}

} // namespace tdur::game

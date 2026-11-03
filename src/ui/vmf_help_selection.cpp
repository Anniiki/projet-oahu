#include "tdur/ui/vmf_help_selection.hpp"

#include <array>
#include <bit>
#include <utility>

namespace tdur::ui {

VmfHelpSelectionContext
VmfHelpSelectionContext::create(std::optional<std::int32_t> world_kind,
                                std::span<const VmfHelpActivity> activities,
                                std::int32_t selected_activity,
                                std::int32_t force) noexcept {
    VmfHelpSelectionContext context;
    if (!world_kind) {
        context.finish(-1);
        return context;
    }
    if (selected_activity >= 0 &&
        static_cast<std::size_t>(selected_activity) < activities.size()) {
        context.activity_ =
            &activities[static_cast<std::size_t>(selected_activity)];
    }
    switch (*world_kind) {
    case 1:
        context.finish(14);
        break;
    case 2:
        context.finish(force == 1 ? 20 : 19);
        break;
    case 10:
        context.finish(23);
        break;
    case 11:
        context.finish(9);
        break;
    case 15:
        context.finish(force == 1 ? 30 : force == 2 ? 31 : -1);
        break;
    case 16:
        context.finish(28);
        break;
    case 4:
    case 13:
    case 17:
    case 21:
    case 22:
        context.select_activity(*world_kind);
        break;
    case 23:
        context.pending_ = VmfHelpQuery::world_gate_38;
        break;
    default:
        context.finish(-1);
        break;
    }
    return context;
}

VmfHelpSelectionContext::VmfHelpSelectionContext(
    VmfHelpSelectionContext&& other) noexcept {
    *this = std::move(other);
}
VmfHelpSelectionContext&
VmfHelpSelectionContext::operator=(VmfHelpSelectionContext&& other) noexcept {
    if (this != &other) {
        activity_ = other.activity_;
        result_ = other.result_;
        pending_ = other.pending_;
        completed_ = other.completed_;
        valid_ = std::exchange(other.valid_, false);
        other.activity_ = nullptr;
        other.result_.reset();
        other.pending_.reset();
    }
    return *this;
}
bool VmfHelpSelectionContext::valid() const noexcept { return valid_; }
std::optional<std::int32_t> VmfHelpSelectionContext::result() const noexcept {
    return valid_ ? result_ : std::nullopt;
}
std::optional<VmfHelpQuery>
VmfHelpSelectionContext::pending_query() const noexcept {
    return valid_ ? pending_ : std::nullopt;
}
std::size_t VmfHelpSelectionContext::completed_queries() const noexcept {
    return completed_;
}
void VmfHelpSelectionContext::finish(std::int32_t index) noexcept {
    result_ = index;
    pending_.reset();
}
void VmfHelpSelectionContext::select_activity(
    std::int32_t world_kind) noexcept {
    if (!activity_ || (world_kind == 21 && activity_->category_1c == 0)) {
        finish(-1);
        return;
    }
    switch (activity_->category_1c) {
    case 0:
        finish(9);
        break;
    case 1:
        if (world_kind == 4) {
            pending_ = VmfHelpQuery::kind4_component_18;
        } else {
            finish(3);
        }
        break;
    case 2:
        finish(2);
        break;
    case 3:
        finish(1);
        break;
    case 4:
        finish(0);
        break;
    case 5:
        pending_ = VmfHelpQuery::progress_flag_1c;
        break;
    case 6:
        finish(8);
        break;
    case 8: {
        constexpr std::array<std::int32_t, 10> categories{7,  5,  6,  6,  10,
                                                          -1, -1, -1, -1, 24};
        const auto category = activity_->category_1d;
        finish(category < categories.size() ? categories[category] : -1);
        break;
    }
    default:
        finish(-1);
        break;
    }
}

std::expected<void, VmfHelpSelectionError>
VmfHelpSelectionContext::provide(VmfHelpQuery query,
                                 const VmfHelpQueryValue& value) noexcept {
    if (!valid_) {
        return std::unexpected(VmfHelpSelectionError::invalid_context);
    }
    if (!pending_ || *pending_ != query) {
        return std::unexpected(VmfHelpSelectionError::unexpected_query);
    }
    if (query == VmfHelpQuery::kind4_component_18) {
        const auto* component =
            std::get_if<std::optional<std::uint32_t>>(&value);
        if (!component) {
            return std::unexpected(VmfHelpSelectionError::wrong_value_type);
        }
        finish(*component && ((**component & 0xffU) != 0) ? 25 : 3);
    } else if (query == VmfHelpQuery::progress_identity) {
        const auto* identity = std::get_if<std::uint64_t>(&value);
        if (!identity) {
            return std::unexpected(VmfHelpSelectionError::wrong_value_type);
        }
        finish(*identity == activity_->identity ? 26 : -1);
    } else {
        const auto* scalar = std::get_if<std::uint32_t>(&value);
        if (!scalar) {
            return std::unexpected(VmfHelpSelectionError::wrong_value_type);
        }
        switch (query) {
        case VmfHelpQuery::world_gate_38:
            if ((*scalar & 0xffU) != 0) {
                finish(-1);
            } else {
                pending_ = VmfHelpQuery::mode_11f32a0;
            }
            break;
        case VmfHelpQuery::mode_11f32a0:
            if (*scalar == 0) {
                finish(11);
            } else if (*scalar == 1) {
                pending_ = VmfHelpQuery::secondary_11f3bd8;
            } else if (*scalar == 3) {
                pending_ = VmfHelpQuery::threshold_11f3da8;
            } else {
                finish(-1);
            }
            break;
        case VmfHelpQuery::secondary_11f3bd8:
            finish(*scalar != 0 ? 12 : 29);
            break;
        case VmfHelpQuery::threshold_11f3da8:
            if (std::bit_cast<std::int32_t>(*scalar) >= 1) {
                pending_ = VmfHelpQuery::blocked_11f41b8;
            } else {
                finish(13);
            }
            break;
        case VmfHelpQuery::blocked_11f41b8:
            finish(*scalar == 0 ? 27 : 13);
            break;
        case VmfHelpQuery::progress_flag_1c:
            if (*scalar == 0) {
                finish(4);
            } else {
                pending_ = VmfHelpQuery::progress_identity;
            }
            break;
        default:
            return std::unexpected(VmfHelpSelectionError::unexpected_query);
        }
    }
    ++completed_;
    return {};
}

std::expected<std::optional<std::string_view>, VmfHelpSelectionError>
vmf_help_routing_tag(std::int32_t index) noexcept {
    constexpr std::array<std::string_view, 32> tags{
        "tip_tuner",    "tip_shop",     "tip_",        "tip_cardeal",
        "tip_club1",    "tip_paintstd", "tip_carrent", "tip_",
        "tip_drive",    "tip_",         "tip_realt",   "tip_time",
        "tip_speed",    "tip_race",     "tut_fr4_pc",  "tip_",
        "tip_",         "tip_",         "tip_",        "tut_map2",
        "tip_",         "tip_",         "tip_",        "tut_home4",
        "tip_paintlux", "tip_carclas",  "tip_club2",   "tip_raceelim",
        "tut_home5",    "tip_speed2",   "tip_drivIn1", "tip_drivIn2"};
    if (index == -1 || index >= static_cast<std::int32_t>(tags.size())) {
        return std::nullopt;
    }
    if (index < 0) {
        return std::unexpected(VmfHelpSelectionError::unsafe_routing_index);
    }
    return tags[static_cast<std::size_t>(index)];
}

} // namespace tdur::ui

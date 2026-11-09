#include "tdur/game/startup_area_resource.hpp"

#include <algorithm>

namespace tdur::game::startup {
namespace {
ModeCallOutcome returned(AreaResourceOutcome result) {
    return result == AreaResourceOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace

AreaResourceOutcome reset_area_709200(AreaLoadPayload& resource,
                                      AreaResourcePorts& ports) {
    if (ports.reset_706210(resource.member_7c0) !=
        ModeCallOutcome::native_returned)
        return AreaResourceOutcome::refused_reset_706210;
    if (ports.reset_701fe0(resource.spatial_2b0) !=
        ModeCallOutcome::native_returned)
        return AreaResourceOutcome::refused_reset_701fe0;
    // All three conditions are read after the two reset callbacks.
    if (resource.borrowed_34 == 0 && !resource.buffer_04.is_null() &&
        resource.allocator_38.va != 0) {
        if (ports.free_674e0c(resource.allocator_38, resource.buffer_04) !=
            ModeCallOutcome::native_returned)
            return AreaResourceOutcome::refused_free;
    }
    // Clear unconditionally after a returned free, even if it replaced +04.
    resource.buffer_04 = {};
    resource.ready_2c = 0;
    resource.status_ca4 = 0;
    return AreaResourceOutcome::native_return;
}

AreaResourceOutcome load_area_709590(AreaLoadPayload& resource,
                                     const AreaLoadCall& call,
                                     AreaResourcePorts& ports) {
    // Native stack/register arguments are values. A callback cannot replace
    // the caller's captured coordinates, flags, data identity or offsets.
    const auto input = call;
    const auto selector = ports.read_selector_1140370_e0();
    if (!selector)
        return AreaResourceOutcome::refused_selector;
    std::array<char, 512> buffer;
    buffer.fill('\x7f');
    const BankPathFormat format{{0xF3B084},
                                {*selector ? 0x11404C4U : 0x1140444U},
                                {0x11405C4},
                                input.path};
    if (ports.format_bank_9ec378(buffer, format) !=
        ModeCallOutcome::native_returned)
        return AreaResourceOutcome::refused_format;
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    if (end == buffer.end())
        return AreaResourceOutcome::refused_unterminated_path;
    const std::string_view path(buffer.data(),
                                static_cast<std::size_t>(end - buffer.begin()));
    bool accepted = true;
    if (resource.ready_2c == 0) {
        const auto supplied = resource.supplied_ca0;
        resource.borrowed_34 = supplied.is_null() ? 0 : 1;
        resource.buffer_04 = supplied;
        resource.byte_3c = 0;
        resource.allocator_38 = {0xFA9CB0};
        // 7095D1 reads only the low byte of the flags argument.
        const bool alternate = (input.flags & 0xFFU) != 0;
        const BankLoadCall load{{0x10E80C0},         path,
                                {0x9A6D50},          0x180000,
                                {0xF3B0F4},          alternate ? 0x2FU : 0x31U,
                                alternate ? 1U : 0U, 0};
        const auto al = ports.load_9a4d40(resource, load);
        if (!al)
            return AreaResourceOutcome::refused_bank_load;
        accepted = *al != 0;
    }
    if (accepted) {
        const SpatialLoadCall spatial{{input.x_bits, input.y_bits, input.x_bits,
                                       input.y_bits, input.scale_bits, 0}};
        if (ports.load_701340(resource.spatial_2b0, spatial) !=
            ModeCallOutcome::native_returned)
            return AreaResourceOutcome::refused_spatial_load;
        resource.status_ca4 = 2;
    } else {
        resource.status_ca4 = 0;
    }
    // This common tail also runs after native AL=0. No ready byte is set.
    resource.y_2a8 = input.y_bits;
    resource.data_c9c = input.data;
    resource.position_29c = input.position;
    resource.x_2a4 = input.x_bits;
    return AreaResourceOutcome::native_return;
}

ModeCallOutcome
AreaResourceBinding::reset_area_709200(AreaLoadPayload& payload) {
    last_reset = startup::reset_area_709200(payload, area_);
    return returned(*last_reset);
}
ModeCallOutcome
AreaResourceBinding::load_area_709590(AreaLoadPayload& payload,
                                      const AreaLoadCall& call) {
    last_load = startup::load_area_709590(payload, call, area_);
    return returned(*last_load);
}
} // namespace tdur::game::startup

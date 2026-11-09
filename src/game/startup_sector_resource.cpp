#include "tdur/game/startup_sector_resource.hpp"

#include <algorithm>

namespace tdur::game::startup {
namespace {
ModeCallOutcome returned(SectorResourceOutcome result) {
    return result == SectorResourceOutcome::native_return
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace

SectorResourceOutcome reset_sector_705cb0(SectorLoadPayload& resource,
                                          SectorResetContext context) {
    auto& globals = context.globals;
    auto& ports = context.ports;
    const auto owner = globals.context_10e7fa0;
    if (owner != 0 && ports.detach_6fa640(owner, resource) !=
                          ModeCallOutcome::native_returned)
        return SectorResourceOutcome::refused_context_detach;
    auto node = resource.list_10e58;
    std::size_t visited = 0;
    while (node) {
        if (visited == context.max_nodes)
            return SectorResourceOutcome::refused_node_budget;
        auto next = node->next_0c; // Pin before the virtual callback.
        if (node->release_slot8() != ModeCallOutcome::native_returned)
            return SectorResourceOutcome::refused_node_release;
        node = std::move(next);
        ++visited;
    }
    for (const auto tag : {0x726F7061U, 0x70617274U}) {
        const auto count = globals.count_13d3e38;
        for (std::int32_t i = 0; i < count; ++i) {
            const auto index = static_cast<std::size_t>(i);
            if (index >= globals.registry_13d3e10.size())
                return SectorResourceOutcome::refused_registry;
            auto item = globals.registry_13d3e10[index];
            if (!item)
                return SectorResourceOutcome::refused_registry;
            if (item->tag_08 != tag)
                continue;
            const auto result = tag == 0x726F7061U
                                    ? ports.detach_8a5520(resource, *item)
                                    : ports.detach_8a4f60(resource, *item);
            if (result != ModeCallOutcome::native_returned)
                return SectorResourceOutcome::refused_tag_detach;
            break;
        }
    }
    if (resource.area_2ac) {
        // 705D55..705D6C reads +CB4 and computes +7D0, with no call or
        // observable write. Valid owned area objects cannot be native -7D0.
        (void)resource.area_2ac->payload_10.status_ca4;
        resource.area_2ac = nullptr;
        resource.byte_2b0 = 0;
    }
    if (resource.ready_2c != 0) {
        // The native EBP+8 zero case is outside valid owned-object storage.
        const auto key = resource.property_10e5c;
        if (key != 0) {
            for (std::size_t i = 0; i < globals.tracked_120be0c.size(); ++i) {
                if (globals.tracked_120be0c[i].identity != key)
                    continue;
                if (ports.detach_7d7a60(resource,
                                        static_cast<std::uint32_t>(i * 5)) !=
                    ModeCallOutcome::native_returned)
                    return SectorResourceOutcome::refused_tracked_detach;
                auto& entry = globals.tracked_120be0c[i];
                entry.active_10 = 0;
                entry.identity = 0;
                entry.words.fill(0);
                break;
            }
        }
        resource.property_10e5c = 0;
        if (ports.detach_901ab0({0xFA4CF8}, resource) !=
            ModeCallOutcome::native_returned)
            return SectorResourceOutcome::refused_resource_detach;
    }
    auto* object = context.forward.object_fa48b0;
    if (!object)
        return SectorResourceOutcome::refused_global;
    if (ports.reset_6fc800({object, 0x20}, resource) !=
        ModeCallOutcome::native_returned)
        return SectorResourceOutcome::refused_reset_6fc800;
    if (ports.reset_6ff980(resource.member_7d0) !=
        ModeCallOutcome::native_returned)
        return SectorResourceOutcome::refused_reset_6ff980;
    if (ports.reset_701fe0(resource.spatial_2c0) !=
        ModeCallOutcome::native_returned)
        return SectorResourceOutcome::refused_reset_701fe0;
    if (resource.borrowed_34 == 0 && !resource.buffer_04.is_null() &&
        resource.allocator_38.va != 0) {
        if (ports.free_674e0c(resource.buffer_04) !=
            ModeCallOutcome::native_returned)
            return SectorResourceOutcome::refused_free;
    }
    resource.buffer_04 = {};
    resource.ready_2c = 0;
    resource.status_2a4 = 0;
    return SectorResourceOutcome::native_return;
}

SectorResourceOutcome load_sector_706030(SectorLoadPayload& resource,
                                         const SectorLoadCall& call,
                                         SectorResourcePorts& ports) {
    const auto input = call;
    // Forward copy, preserving the written prefix on a modern capacity refusal.
    for (std::size_t i = 0;; ++i) {
        if (i == resource.name_10e00.size())
            return SectorResourceOutcome::refused_name_capacity;
        const auto byte = i == input.name.size() ? '\0' : input.name[i];
        resource.name_10e00[i] = byte;
        if (byte == '\0')
            break;
    }
    const auto selector = ports.read_selector_1140370_e0();
    if (!selector)
        return SectorResourceOutcome::refused_selector;
    std::array<char, 512> buffer;
    buffer.fill('\x7f');
    const BankPathFormat format{{0xF3B084},
                                {*selector ? 0x11404C4U : 0x1140444U},
                                {0x11405C4},
                                input.path};
    if (ports.format_bank_9ec378(buffer, format) !=
        ModeCallOutcome::native_returned)
        return SectorResourceOutcome::refused_format;
    const auto end = std::find(buffer.begin(), buffer.end(), '\0');
    if (end == buffer.end())
        return SectorResourceOutcome::refused_unterminated_path;
    const std::string_view path(buffer.data(),
                                static_cast<std::size_t>(end - buffer.begin()));
    bool accepted = true;
    if (resource.ready_2c == 0) {
        const auto supplied = resource.supplied_2b4;
        resource.borrowed_34 = supplied.is_null() ? 0 : 1;
        resource.buffer_04 = supplied;
        resource.allocator_38 = {0xFA9CB0};
        resource.byte_3c = 0;
        const BankLoadCall load{{0x10E80C0}, path, {0x9A6D50}, 0x480000,
                                {0xF3B154},  0x36, 1,          0};
        const auto al = ports.load_9a4d40(resource, load);
        if (!al)
            return SectorResourceOutcome::refused_bank_load;
        accepted = *al != 0;
    }
    if (accepted) {
        resource.link_2330 = {input.data.object, input.data.offset + 0x4F80U};
        const SpatialLoadCall spatial{{input.area_x_bits, input.area_y_bits,
                                       input.sector_x_bits, input.sector_y_bits,
                                       input.scale_bits, 0}};
        if (ports.load_701340(resource.spatial_2c0, spatial) !=
            ModeCallOutcome::native_returned)
            return SectorResourceOutcome::refused_spatial_load;
        resource.status_2a4 = 2;
    } else {
        resource.link_2330 = {};
        resource.status_2a4 = 3;
    }
    resource.data_2a8 = input.data;
    resource.x_29c = input.sector_x_bits;
    resource.y_2a0 = input.sector_y_bits;
    return SectorResourceOutcome::native_return;
}
ModeCallOutcome
SectorResourceBinding::reset_sector_705cb0(SectorLoadPayload& payload) {
    last_reset = startup::reset_sector_705cb0(payload, context_);
    return returned(*last_reset);
}
ModeCallOutcome
SectorResourceBinding::load_sector_706030(SectorLoadPayload& payload,
                                          const SectorLoadCall& call) {
    last_load = startup::load_sector_706030(payload, call, context_.ports);
    return returned(*last_load);
}
} // namespace tdur::game::startup

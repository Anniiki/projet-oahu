#include "tdur/game/startup_services.hpp"

#include <cstring>

namespace tdur::game::startup {
namespace {

// Common prologue of six bodies: owner into +0x10, then byte +0x18 = 0.
void store_owner(StartupServiceBase& self, GameModeRuntime& owner) {
    self.owner_10 = &owner;
    self.flag_18 = 0;
}

HelperOutcome run_helper(StartupServiceBase& member, Allocation& destination,
                         std::byte pattern, HelperContextSlot& global,
                         AllocatorPort& allocator) {
    HelperContext* first = global.load();
    if (first == nullptr)
        return HelperOutcome::refused_missing_context;
    if (first->byte_e4 == 0)
        return HelperOutcome::inactive;
    const std::uint32_t requested = first->dword_ec;
    const Allocation result = allocator.allocate_slot20(requested, 0, 0, 0);
    // Listing order: G is reloaded before the store, its +0xEC is read after
    // it. The flag is not tested again. The previous block is not released.
    HelperContext* second = global.load();
    (void)member;
    destination = result;
    if (second == nullptr)
        return HelperOutcome::refused_missing_context;
    const std::uint32_t length = second->dword_ec;
    if (length == 0)
        return HelperOutcome::filled;
    if (result.is_null() || length > result.capacity)
        return HelperOutcome::refused_fill_exceeds_storage;
    std::memset(result.data, std::to_integer<int>(pattern), length);
    return HelperOutcome::filled;
}

} // namespace

InitOutcome initialize_98f020(StartupServiceBase& self, GameModeRuntime& owner,
                              StartupContext context) {
    auto& globals = context.globals;
    Object143F520* object = globals.global_143f520;
    if (object == nullptr)
        return InitOutcome::refused_missing_object_143f520;
    object->byte_14 = 0;
    globals.block_1699070.field_2c = object;
    globals.global_fa9e24 = object;
    globals.global_fa8fd4 = object;
    store_owner(self, owner);
    auto& block = globals.block_1699070;
    block.field_7c = 0x7530;
    block.field_80 = 0x1F40'0000;
    block.field_84 = 0x0010'0000;
    block.field_72 = 0;
    context.calls.call_432fb0(block);
    const OpaqueWord first = context.calls.call_via_9ec484(
        globals.global_10e8058, 0x65, 1, 0, 0, 0x40);
    context.calls.call_via_9ec480(globals.block_1699070.field_4c, -0xE, first);
    context.calls.call_via_9ec47c(globals.block_1699070.field_4c,
                                  NativeIdentity{0xF0'8820});
    globals.block_1699070.field_64 = NativeIdentity{0x98'EF90};
    return InitOutcome::completed;
}

InitOutcome initialize_9cfa40(StartupServiceBase& self, GameModeRuntime& owner,
                              StartupContext context) {
    store_owner(self, owner);
    auto& block = context.globals.block_1699140;
    // DWORD arithmetic modulo 2^32, read order 0x3C, 0x44, 0x40.
    std::uint32_t value = block.field_3c + 0x1Du;
    const std::uint32_t field_44 = block.field_44;
    value *= block.field_40;
    value = value + field_44 * 8u + 0x10u;
    block.field_4c = value;
    block.field_2c = NativeIdentity{0xFA'7890};
    const Allocation result = context.allocator.allocate_slot20(value, 0, 0, 0);
    context.globals.block_1699140.field_50 = result;
    context.calls.call_438730(context.globals.block_1699140);
    return InitOutcome::completed;
}

InitOutcome initialize_982c40(StartupServiceBase& self, Fields982C40& fields,
                              GameModeRuntime& owner) {
    constexpr float constant_f4165c = 1.0F;
    fields.field_28 = constant_f4165c;
    fields.field_2c = constant_f4165c;
    store_owner(self, owner);
    fields.field_38 = 0.0F;
    fields.field_30 = 0.0F;
    fields.field_34 = 0.0F;
    return InitOutcome::completed;
}

InitOutcome initialize_998dd0(StartupServiceBase& self,
                              GameModeRuntime& owner) {
    (void)self;
    (void)owner;
    return InitOutcome::completed;
}

InitOutcome initialize_9a7e10(StartupServiceBase& self, Fields9A7E10& fields,
                              GameModeRuntime& owner, StartupContext context) {
    auto& globals = context.globals;
    Object143F520* object = globals.global_143f520;
    store_owner(self, owner);
    globals.region_169a808.v169a824 = object;
    {
        const std::uint32_t base = globals.region_169a808.v169a894;
        const std::uint32_t size = ((base * 4u) >> 5) + base;
        globals.region_169a808.v169a89c = size;
        const Allocation result =
            context.allocator.allocate_slot20(size, 0, 0, 0);
        globals.region_169a808.v169a898 = result;
    }
    auto& region = globals.region_169a808;
    if (region.v169a808 == 1 && region.v169a80c == 4 && region.v169a8a0 == 0) {
        const std::uint32_t copied_86c = region.v169a86c;
        const std::uint32_t copied_870 = region.v169a870;
        region.v169a8b8 = copied_86c;
        region.v169a8bc = copied_870;
        region.v169a8a8 = NativeIdentity{0x45'7440};
        region.v169a8dc.fill(0);
        const Allocation stored = region.v169a898;
        const std::uint32_t size = region.v169a89c;
        region.v169a8c8 = NativeIdentity{0xF6'B2C8};
        region.v169aa04.field_00 = stored;
        region.v169aa04.field_04 = stored;
        region.v169aa04.field_08 = size;
        region.v169aa04.field_0c = 0;
        if (!stored.is_null()) {
            const std::uint32_t count = region.v169a894;
            const std::uint32_t shifted = count >> 5;
            const std::uint32_t first =
                context.calls.call_41ee80(region.v169aa04, count);
            region.v169a9f8 = first;
            region.v169aa00 = count;
            const std::uint32_t second =
                context.calls.call_41ee80(region.v169aa04, shifted * 4u);
            region.v169a9f0 = second;
            region.v169a9fc = shifted;
        }
        const std::uint32_t status = context.calls.call_457e50();
        region.v169a8a0 = status;
        if (status == 0 && region.v169a828 == 0)
            region.v169a828 = 1;
    }
    region.v169a874 = 0xC;
    region.v169a8ac = NativeIdentity{0x9A'7CD0};
    fields.field_1c = 7;
    fields.field_20 = 0;
    return InitOutcome::completed;
}

InitOutcome initialize_999e80(StartupServiceBase& self, GameModeRuntime& owner,
                              StartupContext context) {
    store_owner(self, owner);
    auto& globals = context.globals;
    globals.block_f5b4b0.field_2c = globals.global_143f520;
    const std::uint32_t size =
        context.calls.call_6754a0(globals.block_f5b4b0.field_34);
    globals.block_f5b4b0.field_3c = size;
    if (size != 0) {
        const Allocation result =
            context.allocator.allocate_slot20(size, 0, 0, 0);
        globals.block_f5b4b0.field_38 = result;
    } else {
        globals.block_f5b4b0.field_38 = Allocation{};
    }
    context.calls.call_6755a0(globals.block_f5b4b0);
    return InitOutcome::completed;
}

InitOutcome initialize_9b3f80(StartupServiceBase& self,
                              GameModeRuntime& owner) {
    store_owner(self, owner);
    return InitOutcome::completed;
}

HelperOutcome initialization_helper_9b40c0(StartupServiceBase& member,
                                           HelperContextSlot& global,
                                           AllocatorPort& allocator) {
    return run_helper(member, member.allocation_04, std::byte{0xBD}, global,
                      allocator);
}

HelperOutcome initialization_helper_9b4060(StartupServiceBase& member,
                                           HelperContextSlot& global,
                                           AllocatorPort& allocator) {
    return run_helper(member, member.allocation_08, std::byte{0xAD}, global,
                      allocator);
}

} // namespace tdur::game::startup

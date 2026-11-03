#include "tdur/diagnostics/physics_binding.hpp"

#include <algorithm>
#include <limits>

namespace tdur::diagnostics {
namespace {
constexpr std::uint32_t service_vtable = 0x00f27068;
constexpr std::uint32_t initialize_entry = 0x0084d570;
constexpr std::uint32_t cleanup_entry = 0x00846cf0;
constexpr std::uint32_t world_vtable = 0x009ece38;
constexpr std::uint32_t simulation_vtable = 0x009efb90;
constexpr std::uint32_t step_entry = 0x004d46e0;
constexpr auto world_cell = physics_binding_service + 0x865cU;
constexpr auto ed_cell = physics_binding_service + 0x866dU;
constexpr auto x86_limit = std::uint64_t{1} << 32;

enum class Role { service_data, object_data, table, code };

bool valid_identity(const PhysicsBindingIdentity& id,
                    PhysicsBindingExpectedProcess expected) noexcept {
    return expected.pid != 0 && expected.creation_filetime != 0 &&
           id.pid == expected.pid &&
           id.creation_filetime == expected.creation_filetime &&
           id.official_sha_verified && id.pe32_i386 &&
           id.image_base == physics_binding_image_base &&
           id.image_size > ed_cell - physics_binding_image_base &&
           id.image_size <= x86_limit - id.image_base &&
           id.maximum_user_address < x86_limit &&
           id.maximum_user_address >= ed_cell && id.qpc_frequency != 0 &&
           id.qpc_frequency <= static_cast<std::uint64_t>(
                                   std::numeric_limits<std::int64_t>::max());
}

bool same_identity(const PhysicsBindingIdentity& a,
                   const PhysicsBindingIdentity& b) noexcept {
    return a.pid == b.pid && a.creation_filetime == b.creation_filetime &&
           a.image_base == b.image_base && a.image_size == b.image_size &&
           a.maximum_user_address == b.maximum_user_address &&
           a.qpc_frequency == b.qpc_frequency &&
           a.official_sha_verified == b.official_sha_verified &&
           a.pe32_i386 == b.pe32_i386;
}

struct PassReader {
    PhysicsBindingReader& memory;
    const PhysicsBindingIdentity& id;
    PhysicsBindingPass& pass;

    bool fail(PhysicsBindingStatus status) noexcept {
        pass.status = status;
        return false;
    }

    bool range(std::uint64_t address, std::uint32_t size, Role role) noexcept {
        if (address == 0 || address >= x86_limit || size == 0 ||
            address > id.maximum_user_address ||
            size > id.maximum_user_address - address + 1 ||
            (role != Role::code && size == 4 && (address & 3U) != 0))
            return fail(PhysicsBindingStatus::address_refused);
        const auto end = address + size;
        if (role != Role::object_data &&
            (address < id.image_base || end > id.image_base + id.image_size))
            return fail(PhysicsBindingStatus::address_refused);
        for (auto cursor = address; cursor < end;) {
            PhysicsBindingPage page{};
            if (!memory.query(static_cast<std::uint32_t>(cursor), page) ||
                page.size == 0 || page.begin > cursor ||
                page.begin >= x86_limit || page.size > x86_limit - page.begin ||
                page.begin + page.size <= cursor || !page.committed ||
                page.guard)
                return fail(PhysicsBindingStatus::page_refused);
            const bool main_image =
                page.type == PhysicsBindingMemoryType::image &&
                page.allocation_base == id.image_base &&
                address >= id.image_base &&
                end <= id.image_base + id.image_size;
            bool allowed = false;
            if (role == Role::code) {
                allowed = main_image && page.executable;
            } else if (page.readable && !page.executable) {
                switch (role) {
                case Role::service_data:
                    allowed = main_image;
                    break;
                case Role::object_data:
                    allowed =
                        main_image ||
                        page.type == PhysicsBindingMemoryType::private_memory;
                    break;
                case Role::table:
                    allowed = main_image && !page.writable;
                    break;
                case Role::code:
                    break;
                }
            }
            if (!allowed)
                return fail(PhysicsBindingStatus::page_refused);
            cursor = page.begin + page.size;
        }
        return true;
    }

    bool value(PhysicsBindingField field, std::uint64_t address, Role role,
               std::optional<std::uint32_t>& output) noexcept {
        pass.at = field;
        if (!range(address, 4, role))
            return false;
        std::array<std::byte, 4> bytes{};
        // Each pass has exactly eight DWORDs and one byte, with no loops
        // over payloads or alternate branches that could add a read.
        pass.requested_bytes += 4;
        const auto transfer =
            memory.read(static_cast<std::uint32_t>(address), bytes);
        pass.received_bytes += static_cast<std::uint32_t>(
            std::min(transfer.received, bytes.size()));
        if (!transfer.success || transfer.received != bytes.size())
            return fail(PhysicsBindingStatus::unreadable);
        output = std::to_integer<std::uint32_t>(bytes[0]) |
                 (std::to_integer<std::uint32_t>(bytes[1]) << 8) |
                 (std::to_integer<std::uint32_t>(bytes[2]) << 16) |
                 (std::to_integer<std::uint32_t>(bytes[3]) << 24);
        return true;
    }

    bool expected(std::optional<std::uint32_t> value,
                  std::uint32_t candidate) noexcept {
        if (*value == 0)
            return fail(PhysicsBindingStatus::absent);
        return *value == candidate ||
               fail(PhysicsBindingStatus::unsupported_binding);
    }

    bool pointer(std::optional<std::uint32_t> value) noexcept {
        return *value != 0 || fail(PhysicsBindingStatus::absent);
    }

    bool collect() noexcept {
        auto& v = pass.values;
        if (!value(PhysicsBindingField::service_vtable, physics_binding_service,
                   Role::service_data, v[0]) ||
            !expected(v[0], service_vtable) ||
            !value(PhysicsBindingField::initialize_entry, service_vtable + 8U,
                   Role::table, v[1]) ||
            !expected(v[1], initialize_entry) || !range(*v[1], 1, Role::code) ||
            !value(PhysicsBindingField::cleanup_entry, service_vtable + 0x1cU,
                   Role::table, v[2]) ||
            !expected(v[2], cleanup_entry) || !range(*v[2], 1, Role::code) ||
            !value(PhysicsBindingField::world, world_cell, Role::service_data,
                   v[3]) ||
            !pointer(v[3]) ||
            !value(PhysicsBindingField::world_vtable, *v[3], Role::object_data,
                   v[4]) ||
            !expected(v[4], world_vtable) || !range(*v[4], 4, Role::table) ||
            !value(PhysicsBindingField::simulation,
                   static_cast<std::uint64_t>(*v[3]) + 8, Role::object_data,
                   v[5]) ||
            !pointer(v[5]) ||
            !value(PhysicsBindingField::simulation_vtable, *v[5],
                   Role::object_data, v[6]) ||
            !expected(v[6], simulation_vtable) ||
            !value(PhysicsBindingField::step_entry,
                   static_cast<std::uint64_t>(*v[6]) + 8, Role::table, v[7]) ||
            !expected(v[7], step_entry) || !range(*v[7], 1, Role::code))
            return false;
        pass.at = PhysicsBindingField::ed;
        if (!range(ed_cell, 1, Role::service_data))
            return false;
        std::array<std::byte, 1> flag{};
        ++pass.requested_bytes;
        const auto transfer = memory.read(ed_cell, flag);
        pass.received_bytes += static_cast<std::uint32_t>(
            std::min(transfer.received, flag.size()));
        if (!transfer.success || transfer.received != flag.size())
            return fail(PhysicsBindingStatus::unreadable);
        pass.ed = std::to_integer<std::uint8_t>(flag[0]);
        pass.status = PhysicsBindingStatus::pass_complete;
        return true;
    }
};
} // namespace

PhysicsBindingObservation
collect_physics_binding(PhysicsBindingReader& reader,
                        PhysicsBindingExpectedProcess expected) noexcept {
    PhysicsBindingObservation result;
    result.identity = reader.identity();
    if (!valid_identity(result.identity, expected)) {
        result.status = PhysicsBindingStatus::identity_refused;
        return result;
    }
    std::optional<std::uint64_t> previous_end;
    for (auto& pass : result.passes) {
        pass.at = PhysicsBindingField::timestamp;
        std::uint64_t tick{};
        if (!reader.timestamp(tick) || (previous_end && tick < *previous_end)) {
            pass.status = PhysicsBindingStatus::timestamp_refused;
        } else {
            pass.begin_qpc = tick;
            pass.at = PhysicsBindingField::identity;
            if (!same_identity(result.identity, reader.identity())) {
                pass.status = PhysicsBindingStatus::identity_refused;
            } else {
                PassReader{reader, result.identity, pass}.collect();
                if (!same_identity(result.identity, reader.identity())) {
                    pass.at = PhysicsBindingField::identity;
                    pass.status = PhysicsBindingStatus::identity_refused;
                }
            }
            if (!reader.timestamp(tick) || tick < *pass.begin_qpc) {
                pass.at = PhysicsBindingField::timestamp;
                pass.status = PhysicsBindingStatus::timestamp_refused;
            } else {
                pass.end_qpc = tick;
                previous_end = tick;
            }
        }
        result.requested_bytes += pass.requested_bytes;
        result.received_bytes += pass.received_bytes;
        if (pass.status != PhysicsBindingStatus::pass_complete) {
            result.status = pass.status;
            return result;
        }
    }
    result.status = result.passes[0].values == result.passes[1].values &&
                            result.passes[0].ed == result.passes[1].ed
                        ? PhysicsBindingStatus::candidate_concordant
                        : PhysicsBindingStatus::changed;
    return result;
}

std::string_view
physics_binding_status_name(PhysicsBindingStatus status) noexcept {
    switch (status) {
    case PhysicsBindingStatus::not_attempted:
        return "not_attempted";
    case PhysicsBindingStatus::pass_complete:
        return "pass_complete";
    case PhysicsBindingStatus::candidate_concordant:
        return "candidate_concordant";
    case PhysicsBindingStatus::changed:
        return "changed";
    case PhysicsBindingStatus::identity_refused:
        return "identity_refused";
    case PhysicsBindingStatus::absent:
        return "absent";
    case PhysicsBindingStatus::unsupported_binding:
        return "unsupported_binding";
    case PhysicsBindingStatus::address_refused:
        return "address_refused";
    case PhysicsBindingStatus::page_refused:
        return "page_refused";
    case PhysicsBindingStatus::unreadable:
        return "unreadable";
    case PhysicsBindingStatus::timestamp_refused:
        return "timestamp_refused";
    }
    return "unknown";
}

std::string_view
physics_binding_field_name(PhysicsBindingField field) noexcept {
    switch (field) {
    case PhysicsBindingField::service_vtable:
        return "service_vtable";
    case PhysicsBindingField::initialize_entry:
        return "initialize_entry";
    case PhysicsBindingField::cleanup_entry:
        return "cleanup_entry";
    case PhysicsBindingField::world:
        return "world";
    case PhysicsBindingField::world_vtable:
        return "world_vtable";
    case PhysicsBindingField::simulation:
        return "simulation";
    case PhysicsBindingField::simulation_vtable:
        return "simulation_vtable";
    case PhysicsBindingField::step_entry:
        return "step_entry";
    case PhysicsBindingField::ed:
        return "ed";
    case PhysicsBindingField::identity:
        return "identity";
    case PhysicsBindingField::timestamp:
        return "timestamp";
    }
    return "unknown";
}
} // namespace tdur::diagnostics

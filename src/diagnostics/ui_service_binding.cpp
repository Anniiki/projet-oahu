#include "tdur/diagnostics/ui_service_binding.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace tdur::diagnostics {
namespace {
using Status = UiServiceBindingStatus;
using Field = UiServiceBindingField;
constexpr std::uint64_t limit = std::uint64_t{1} << 32;
constexpr std::uint32_t image_base = 0x00400000;
constexpr std::uint32_t host_cell = 0x0114c15c;
constexpr std::uint32_t catalog_cell = 0x011f2cdc;
enum class Role { root, object, table, code };

bool valid_identity(const UiServiceBindingIdentity& id,
                    UiServiceExpectedProcess expected) noexcept {
    return expected.pid != 0 && expected.creation_filetime != 0 &&
           id.pid == expected.pid &&
           id.creation_filetime == expected.creation_filetime &&
           id.official_sha_verified && id.pe32_i386 &&
           id.image_base == image_base && id.image_size == 0x012ad000 &&
           id.maximum_user_address < limit &&
           id.maximum_user_address >= catalog_cell + 3U &&
           id.qpc_frequency != 0 &&
           id.qpc_frequency <= static_cast<std::uint64_t>(
                                   std::numeric_limits<std::int64_t>::max());
}

struct PassReader {
    UiServiceBindingReader& memory;
    UiServiceBindingObservation& observation;
    UiServiceBindingPass& pass;
    UiServiceBindingChain* chain{};

    bool fail(Status status) noexcept {
        chain->status = status;
        return false;
    }
    bool range(std::uint64_t address, std::uint32_t size, Role role) noexcept {
        const auto& id = observation.identity;
        if (address == 0 || address >= limit || size == 0 ||
            size > limit - address || address > id.maximum_user_address ||
            size > id.maximum_user_address - address + 1 ||
            (size == 4 && (address & 3U) != 0))
            return fail(Status::address_refused);
        const auto end = address + size;
        if (role != Role::object &&
            (address < id.image_base || end > id.image_base + id.image_size))
            return fail(Status::address_refused);
        for (auto cursor = address; cursor < end;) {
            UiServiceBindingPage page{};
            if (!memory.query(static_cast<std::uint32_t>(cursor), page) ||
                !page.committed || page.guard || page.size == 0 ||
                page.begin > cursor || page.begin >= limit ||
                page.size > limit - page.begin ||
                page.begin + page.size <= cursor)
                return fail(Status::page_refused);
            const bool main = page.type == UiServiceMemoryType::image &&
                              page.allocation_base == id.image_base &&
                              address >= id.image_base &&
                              end <= id.image_base + id.image_size;
            const bool allowed =
                role == Role::code
                    ? main && page.executable
                    : page.readable && !page.executable &&
                          (role == Role::object
                               ? main || page.type ==
                                             UiServiceMemoryType::private_memory
                               : main &&
                                     (role != Role::table || !page.writable));
            if (!allowed)
                return fail(Status::page_refused);
            cursor = page.begin + page.size;
        }
        return true;
    }
    bool word(Field field, std::uint64_t address, Role role) noexcept {
        chain->at = field;
        if (!range(address, 4, role))
            return false;
        if (pass.requested_bytes > 48U - 4U ||
            observation.requested_bytes > ui_service_binding_max_bytes - 4U)
            return fail(Status::budget_refused);
        std::array<std::byte, 4> bytes{};
        pass.requested_bytes += 4;
        observation.requested_bytes += 4;
        const auto transfer =
            memory.read(static_cast<std::uint32_t>(address), bytes);
        const auto received = static_cast<std::uint32_t>(
            std::min(transfer.received, bytes.size()));
        pass.received_bytes += received;
        observation.received_bytes += received;
        if (transfer.received > bytes.size())
            return fail(Status::invalid_transfer);
        if (!transfer.success || transfer.received != bytes.size())
            return fail(Status::unreadable);
        pass.values[static_cast<std::size_t>(field)] =
            std::to_integer<std::uint32_t>(bytes[0]) |
            (std::to_integer<std::uint32_t>(bytes[1]) << 8) |
            (std::to_integer<std::uint32_t>(bytes[2]) << 16) |
            (std::to_integer<std::uint32_t>(bytes[3]) << 24);
        return true;
    }
    std::uint32_t value(Field field) const noexcept {
        return *pass.values[static_cast<std::size_t>(field)];
    }
    bool pointer(Field field) noexcept {
        return value(field) != 0 || fail(Status::absent);
    }
    void host() noexcept {
        chain = &pass.host;
        if (!word(Field::host_root, host_cell, Role::root) ||
            !pointer(Field::host_root) ||
            !word(Field::host_vtable, value(Field::host_root), Role::object) ||
            !pointer(Field::host_vtable))
            return;
        constexpr std::array fields{Field::host_1c, Field::host_30,
                                    Field::host_4c};
        constexpr std::array offsets{0x1cU, 0x30U, 0x4cU};
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (!word(fields[i],
                      static_cast<std::uint64_t>(value(Field::host_vtable)) +
                          offsets[i],
                      Role::table) ||
                !pointer(fields[i]) || !range(value(fields[i]), 1, Role::code))
                return;
        chain->status = Status::pass_complete;
    }
    void descriptor(std::size_t index) noexcept {
        chain = &pass.descriptors[index];
        constexpr std::array count_fields{Field::catalog_0_count,
                                          Field::catalog_7_count};
        constexpr std::array record_fields{Field::catalog_0_records,
                                           Field::catalog_7_records};
        constexpr std::array pool_fields{Field::catalog_0_pool,
                                         Field::catalog_7_pool};
        constexpr std::array<std::uint32_t, 2> offsets{0x48U, 0x25cU};
        const auto descriptor =
            static_cast<std::uint64_t>(value(Field::catalog_root)) +
            offsets[index];
        if (!word(count_fields[index], descriptor + 0x44U, Role::object))
            return;
        if (std::bit_cast<std::int32_t>(value(count_fields[index])) > 0) {
            if (!word(record_fields[index], descriptor + 0x40U, Role::object) ||
                !pointer(record_fields[index]) ||
                !range(value(record_fields[index]), 1, Role::object) ||
                !word(pool_fields[index], descriptor + 0x48U, Role::object) ||
                !pointer(pool_fields[index]) ||
                !range(value(pool_fields[index]), 1, Role::object))
                return;
        }
        chain->status = Status::pass_complete;
    }
    void collect() noexcept {
        host();
        chain = &pass.catalog_root;
        if (word(Field::catalog_root, catalog_cell, Role::root) &&
            pointer(Field::catalog_root)) {
            chain->status = Status::pass_complete;
            descriptor(0);
            descriptor(1);
        }
        pass.status = Status::pass_complete;
        for (const auto& part : {pass.host, pass.catalog_root,
                                 pass.descriptors[0], pass.descriptors[1]})
            if (part.status != Status::pass_complete &&
                pass.status == Status::pass_complete) {
                pass.status = Status::partial;
                pass.at = part.at;
            }
    }
};
bool same_capture(const UiServiceBindingPass& a,
                  const UiServiceBindingPass& b) noexcept {
    return a.host == b.host && a.catalog_root == b.catalog_root &&
           a.descriptors == b.descriptors && a.values == b.values &&
           a.requested_bytes == b.requested_bytes &&
           a.received_bytes == b.received_bytes;
}
} // namespace

UiServiceBindingObservation
collect_ui_service_binding(UiServiceBindingReader& reader,
                           UiServiceExpectedProcess expected) noexcept {
    UiServiceBindingObservation result;
    result.identity = reader.identity();
    if (!valid_identity(result.identity, expected)) {
        result.status = Status::identity_refused;
        return result;
    }
    std::optional<std::uint64_t> previous_end;
    for (auto& pass : result.passes) {
        pass.at = Field::timestamp;
        std::uint64_t tick{};
        if (!reader.timestamp(tick) || (previous_end && tick < *previous_end)) {
            pass.status = result.status = Status::timestamp_refused;
            return result;
        }
        pass.begin_qpc = tick;
        pass.at = Field::identity;
        if (result.identity != reader.identity()) {
            pass.status = result.status = Status::identity_refused;
            return result;
        }
        PassReader{reader, result, pass}.collect();
        if (result.identity != reader.identity()) {
            pass.at = Field::identity;
            pass.status = result.status = Status::identity_refused;
            return result;
        }
        if (!reader.timestamp(tick) || tick < *pass.begin_qpc ||
            tick - *pass.begin_qpc > result.identity.qpc_frequency) {
            pass.at = Field::timestamp;
            pass.status = result.status = Status::timestamp_refused;
            return result;
        }
        pass.end_qpc = tick;
        previous_end = tick;
    }
    result.status = !same_capture(result.passes[0], result.passes[1])
                        ? Status::changed
                    : result.passes[0].status == Status::pass_complete
                        ? Status::candidate_concordant
                        : Status::partial;
    return result;
}

std::string_view ui_service_binding_status_name(Status status) noexcept {
    constexpr std::array names{
        "not_attempted",     "pass_complete", "candidate_concordant",
        "changed",           "partial",       "identity_refused",
        "timestamp_refused", "absent",        "address_refused",
        "page_refused",      "unreadable",    "invalid_transfer",
        "budget_refused"};
    const auto index = static_cast<std::size_t>(status);
    return index < names.size() ? names[index] : "unknown";
}
std::string_view ui_service_binding_field_name(Field field) noexcept {
    constexpr std::array names{
        "host_root",       "host_vtable",       "host_1c",
        "host_30",         "host_4c",           "catalog_root",
        "catalog_0_count", "catalog_0_records", "catalog_0_pool",
        "catalog_7_count", "catalog_7_records", "catalog_7_pool",
        "identity",        "timestamp"};
    const auto index = static_cast<std::size_t>(field);
    return index < names.size() ? names[index] : "unknown";
}
} // namespace tdur::diagnostics

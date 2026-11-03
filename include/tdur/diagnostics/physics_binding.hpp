#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::diagnostics {

inline constexpr std::uint32_t physics_binding_image_base = 0x00400000;
inline constexpr std::uint32_t physics_binding_service = 0x01293880;
inline constexpr std::uint32_t physics_binding_max_bytes = 66;

struct PhysicsBindingExpectedProcess {
    std::uint32_t pid{};
    std::uint64_t creation_filetime{};
};

// A reader attests these facts; the collector independently checks the
// expected PID/creation, PE32 identity, image bounds and every page policy.
// Production readers must verify the official SHA while holding a file lock.
struct PhysicsBindingIdentity {
    std::uint32_t pid{};
    std::uint64_t creation_filetime{};
    std::uint32_t image_base{};
    std::uint64_t image_size{};
    std::uint64_t maximum_user_address{};
    std::uint64_t qpc_frequency{};
    bool official_sha_verified{};
    bool pe32_i386{};
};

enum class PhysicsBindingMemoryType { other, image, private_memory };

struct PhysicsBindingPage {
    std::uint64_t begin{};
    std::uint64_t size{};
    std::uint64_t allocation_base{};
    PhysicsBindingMemoryType type{};
    bool committed{};
    bool readable{};
    bool writable{};
    bool executable{};
    bool guard{};
};

struct PhysicsBindingTransfer {
    bool success{};
    std::size_t received{};
};

class PhysicsBindingReader {
  public:
    virtual ~PhysicsBindingReader() = default;
    virtual PhysicsBindingIdentity identity() noexcept = 0;
    virtual bool timestamp(std::uint64_t& qpc_ticks) noexcept = 0;
    virtual bool query(std::uint32_t address,
                       PhysicsBindingPage& page) noexcept = 0;
    // Only 1/4-byte payload reads are issued by this collector. A partial
    // transfer must be reported even when the OS returned an error.
    virtual PhysicsBindingTransfer
    read(std::uint32_t address, std::span<std::byte> destination) noexcept = 0;
};

enum class PhysicsBindingField : std::uint8_t {
    service_vtable,
    initialize_entry,
    cleanup_entry,
    world,
    world_vtable,
    simulation,
    simulation_vtable,
    step_entry,
    ed,
    identity,
    timestamp
};

enum class PhysicsBindingStatus {
    not_attempted,
    pass_complete,
    candidate_concordant,
    changed,
    identity_refused,
    absent,
    unsupported_binding,
    address_refused,
    page_refused,
    unreadable,
    timestamp_refused
};

struct PhysicsBindingPass {
    PhysicsBindingStatus status{PhysicsBindingStatus::not_attempted};
    PhysicsBindingField at{PhysicsBindingField::identity};
    std::array<std::optional<std::uint32_t>, 8> values{};
    std::optional<std::uint8_t> ed;
    std::optional<std::uint64_t> begin_qpc;
    std::optional<std::uint64_t> end_qpc;
    std::uint32_t requested_bytes{};
    std::uint32_t received_bytes{};
};

struct PhysicsBindingObservation {
    PhysicsBindingStatus status{PhysicsBindingStatus::not_attempted};
    PhysicsBindingIdentity identity{};
    std::array<PhysicsBindingPass, 2> passes{};
    std::uint32_t requested_bytes{};
    std::uint32_t received_bytes{};
};

// No configurable addresses, calls into the target, locks, suspension or
// writes. Stop on the first failed/unknown binding. At most two complete
// passes (66 payload bytes), with no retry. Equality is NOT lifetime, ABA,
// thread-affinity, atomic-snapshot or native-call permission evidence.
PhysicsBindingObservation
collect_physics_binding(PhysicsBindingReader& reader,
                        PhysicsBindingExpectedProcess expected) noexcept;

std::string_view
physics_binding_status_name(PhysicsBindingStatus status) noexcept;
std::string_view physics_binding_field_name(PhysicsBindingField field) noexcept;

} // namespace tdur::diagnostics

#pragma once

#include "tdur/game/startup_services.hpp"

#include <optional>

namespace tdur::game::startup {

// Owned bytes for the complete write extent of 6FC0F0. Bytes not written by
// that body remain meaningful input, including its nonzero guard at +02.
struct CycleResetState {
    std::array<std::byte, 0x994> bytes{};
};
void reset_cycle_state_6fc0f0(CycleResetState& state) noexcept;

// Only fields accessed here are modeled. Other members belong to the host
// implementing 708D50/7053E0. This is not the native memory layout.
struct ForwardCycleObject {
    OpaqueWord port_state = 0;
    CycleResetState member_20;
    std::vector<std::uint32_t> sector_properties_84090;
    std::uint8_t ready_8a3b1 = 0;
};
struct ForwardCycleGlobals {
    ForwardCycleObject* object_fa48b0 = nullptr;
};

// A typed member identity, never pointer arithmetic on a retail address.
// All arguments in a call refer to the same captured owned object.
struct CycleMemberRef {
    ForwardCycleObject* object = nullptr;
    std::uint32_t offset = 0;
    friend bool operator==(CycleMemberRef, CycleMemberRef) = default;
};
struct Prepare708d50Call {
    CycleMemberRef eax;                      // Captured object +08.
    CycleMemberRef esi;                      // Captured object +76FD0.
    std::array<CycleMemberRef, 2> arguments; // Object, object +7A750.
};
struct Prepare7053e0Call {
    CycleMemberRef edi;                      // Captured object +9C0.
    std::array<CycleMemberRef, 3> arguments; // Object, +08, +7A750.
};

class ServiceCyclePorts {
  public:
    virtual ~ServiceCyclePorts() = default;
    virtual ModeCallOutcome prepare_708d50(const Prepare708d50Call& call) = 0;
    virtual ModeCallOutcome prepare_7053e0(const Prepare7053e0Call& call) = 0;
    // The allocator/host owns storage. A native returned call is followed
    // by the caller's pointer clear; a modern refusal preserves that slot.
    virtual ModeCallOutcome free_674e0c(Allocation allocation) = 0;
};

enum class ServiceCycleOutcome {
    native_return,
    refused_missing_global,
    refused_prepare_708d50,
    refused_prepare_7053e0,
    refused_free_04,
    refused_free_08
};
ServiceCycleOutcome forward_service_9b4120(ForwardCycleGlobals& globals,
                                           ServiceCyclePorts& ports);
// The primary instruction at 9B3F30 is RET, with no preceding effect.
ModeCallOutcome reverse_service_9b3f30() noexcept;
// Identical caller logic at 9B3F40 and 982C00. Reads +08 only after the
// first free/clear; the callback may replace either allocation meanwhile.
ServiceCycleOutcome shutdown_service_9b3f40(StartupServiceBase& service,
                                            ServiceCyclePorts& ports);

struct ServiceCycleContext {
    ForwardCycleGlobals& globals;
    ServiceCyclePorts& ports;
};

// Shared actual cycle bodies. Initialization stays abstract: an unknown
// service cannot be made successful by constructing this base.
class StartupCycleService : public StartupServiceBase {
  public:
    explicit StartupCycleService(ServiceCycleContext context)
        : context_(context) {}
    ModeCallOutcome on_forward_pass() override;
    ModeCallOutcome on_reverse_pass() override;
    ModeCallOutcome on_shutdown() override;
    std::optional<ServiceCycleOutcome> last_forward;
    std::optional<ServiceCycleOutcome> last_shutdown;

  private:
    ServiceCycleContext context_;
};

// The host explicitly supplies these services to the existing service table.
// Static table/constructor evidence does not establish runtime installation.
class Service9b3f80 final : public StartupCycleService {
  public:
    using StartupCycleService::StartupCycleService;
    ModeInitOutcome initialize(GameModeRuntime& owner) override;
};
class Service982c40 final : public StartupCycleService {
  public:
    using StartupCycleService::StartupCycleService;
    Fields982C40 fields;
    ModeInitOutcome initialize(GameModeRuntime& owner) override;
};

} // namespace tdur::game::startup

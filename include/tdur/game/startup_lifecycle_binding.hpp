#pragma once

#include "tdur/game/config_driver_98e960.hpp"

#include <array>
#include <memory>
#include <optional>

namespace tdur::game::startup {

// Supplied in the fixed order of 0x9B4E40. These remain live references;
// no substitute service or default cycle implementation is supplied.
using BootServices =
    std::array<std::shared_ptr<StartupServiceBase>, boot_subsystem_count>;

// The gate remains unresolved; a derived host can supply the reconstructed
// completion using the same lifecycle binding, registry and helpers.
class LifecycleHostPorts {
  public:
    virtual ~LifecycleHostPorts() = default;
    virtual std::uint32_t startup_gate() = 0;
    virtual ModeLifecycleStatus complete_startup(GameModeRuntime& owner,
                                                 ModeLifecyclePorts& ports) = 0;
};

struct ConfigurationStartupContext {
    config::DriverContext& driver;
    config::BlockStore& store;
    config::SectionParserState& parser;
    config::DriverPortSet ports;
};

// Compose the existing 9B40C0/9B4060 and 98E960 bodies with the controller.
// Native AL false is distinct from a modern refusal before a native return.
class StartupLifecycleBinding final : public ModeLifecyclePorts {
  public:
    StartupLifecycleBinding(BootServices& services, HelperContextSlot& global,
                            AllocatorPort& allocator,
                            ConfigurationStartupContext configuration,
                            LifecycleHostPorts& host)
        : services_(services), global_(global), allocator_(allocator),
          configuration_(configuration), host_(host) {}

    std::uint32_t startup_gate() override;
    ModeSubsystemPtr boot_subsystem(std::size_t index) override;
    ModeCallOutcome begin_initialization(ModeSubsystem& member) override;
    ModeCallOutcome end_initialization(ModeSubsystem& member) override;
    ModeCallOutcome configure_startup(GameModeRuntime& owner,
                                      ModeSubsystem& fifth_service) override;
    ModeLifecycleStatus complete_startup(GameModeRuntime& owner) override;

    [[nodiscard]] const std::optional<HelperOutcome>& last_begin() const {
        return last_begin_;
    }
    [[nodiscard]] const std::optional<HelperOutcome>& last_end() const {
        return last_end_;
    }
    [[nodiscard]] const std::optional<config::DriverResult>&
    last_configuration() const {
        return last_configuration_;
    }

  private:
    BootServices& services_;
    HelperContextSlot& global_;
    AllocatorPort& allocator_;
    ConfigurationStartupContext configuration_;
    LifecycleHostPorts& host_;
    std::optional<HelperOutcome> last_begin_;
    std::optional<HelperOutcome> last_end_;
    std::optional<config::DriverResult> last_configuration_;
};

} // namespace tdur::game::startup

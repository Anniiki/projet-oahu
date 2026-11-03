#include "tdur/game/startup_lifecycle_binding.hpp"

namespace tdur::game::startup {
namespace {
ModeCallOutcome returned(HelperOutcome outcome) {
    return outcome == HelperOutcome::inactive ||
                   outcome == HelperOutcome::filled
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}
} // namespace

std::uint32_t StartupLifecycleBinding::startup_gate() {
    return host_.startup_gate();
}

ModeSubsystemPtr StartupLifecycleBinding::boot_subsystem(std::size_t index) {
    return index < services_.size() ? services_[index] : nullptr;
}

ModeCallOutcome
StartupLifecycleBinding::begin_initialization(ModeSubsystem& member) {
    auto* service = dynamic_cast<StartupServiceBase*>(&member);
    last_begin_.reset();
    if (!service)
        return ModeCallOutcome::modern_refusal;
    last_begin_ = initialization_helper_9b40c0(*service, global_, allocator_);
    return returned(*last_begin_);
}

ModeCallOutcome
StartupLifecycleBinding::end_initialization(ModeSubsystem& member) {
    auto* service = dynamic_cast<StartupServiceBase*>(&member);
    last_end_.reset();
    if (!service)
        return ModeCallOutcome::modern_refusal;
    last_end_ = initialization_helper_9b4060(*service, global_, allocator_);
    return returned(*last_end_);
}

ModeCallOutcome
StartupLifecycleBinding::configure_startup(GameModeRuntime& owner,
                                           ModeSubsystem& fifth_service) {
    auto* service = dynamic_cast<StartupServiceBase*>(&fifth_service);
    last_configuration_.reset();
    if (!service)
        return ModeCallOutcome::modern_refusal;
    auto& context = configuration_;
    last_configuration_ = config::run_configuration_driver_98e960(
        *service, owner, context.driver, context.store, context.parser,
        context.ports);
    return last_configuration_->outcome == config::DriverOutcome::completed
               ? ModeCallOutcome::native_returned
               : ModeCallOutcome::modern_refusal;
}

ModeLifecycleStatus
StartupLifecycleBinding::complete_startup(GameModeRuntime& owner) {
    return host_.complete_startup(owner, *this);
}

} // namespace tdur::game::startup

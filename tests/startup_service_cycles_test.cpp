#include "tdur/game/startup_service_cycles.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

using namespace tdur::game;
using namespace tdur::game::startup;
namespace {
int checks = 0, failures = 0;
void check(bool value, const char* text, int line) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %d: %s\n", line, text);
    }
}
#define CHECK(value) check((value), #value, __LINE__)

static_assert(std::is_abstract_v<StartupCycleService>);
static_assert(!std::is_abstract_v<Service9b3f80>);
static_assert(!std::is_abstract_v<Service982c40>);

struct CyclePorts final : ServiceCyclePorts {
    std::vector<int> trace;
    std::vector<Allocation> freed;
    std::vector<Prepare708d50Call> first;
    std::vector<Prepare7053e0Call> second;
    std::function<void(int)> effect;
    int refuse = 0;
    ModeCallOutcome at(int site) {
        trace.push_back(site);
        if (effect)
            effect(site);
        return refuse == site ? ModeCallOutcome::modern_refusal
                              : ModeCallOutcome::native_returned;
    }
    ModeCallOutcome prepare_708d50(const Prepare708d50Call& call) override {
        first.push_back(call);
        return at(1);
    }
    ModeCallOutcome prepare_7053e0(const Prepare7053e0Call& call) override {
        second.push_back(call);
        return at(2);
    }
    ModeCallOutcome free_674e0c(Allocation allocation) override {
        freed.push_back(allocation);
        return at(3);
    }
};

void reset_bytes() {
    // Primary 6FC0F0 writes 9 DWORDs, bytes 1 and 0, then guard 2.
    // All other bytes are canaries, including gaps inside each record.
    for (unsigned guard : {0U, 1U, 0x80U, 0xFFU}) {
        CycleResetState state;
        state.bytes.fill(std::byte{0xA7});
        state.bytes[2] = static_cast<std::byte>(guard);
        auto expected = state.bytes;
        if (guard == 0) {
            for (std::size_t index :
                 {0U,     1U,     4U,     5U,     6U,     7U,     0x32CU,
                  0x32DU, 0x32EU, 0x32FU, 0x330U, 0x331U, 0x332U, 0x333U,
                  0x334U, 0x335U, 0x336U, 0x337U, 0x65CU, 0x65DU, 0x65EU,
                  0x65FU, 0x660U, 0x661U, 0x662U, 0x663U, 0x664U, 0x665U,
                  0x666U, 0x667U, 0x98CU, 0x98DU, 0x98EU, 0x98FU, 0x990U,
                  0x991U, 0x992U, 0x993U})
                expected[index] = std::byte{0};
            expected[2] = std::byte{1};
        }
        reset_cycle_state_6fc0f0(state);
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK(state.bytes[i] == expected[i]);
        state.bytes[0] = std::byte{0xDD};
        reset_cycle_state_6fc0f0(state);
        CHECK(state.bytes[0] == std::byte{0xDD});
    }
}

void forward_capture() {
    ForwardCycleObject first, replacement;
    first.member_20.bytes.fill(std::byte{0x27});
    first.member_20.bytes[2] = std::byte{0};
    ForwardCycleGlobals globals{&first};
    CyclePorts ports;
    ports.effect = [&](int site) {
        CHECK(first.ready_8a3b1 == 0);
        if (site == 1)
            globals.object_fa48b0 = &replacement;
        if (site == 2)
            first.member_20.bytes[3] = std::byte{0x91};
    };
    CHECK(forward_service_9b4120(globals, ports) ==
          ServiceCycleOutcome::native_return);
    CHECK((ports.trace == std::vector<int>{1, 2}));
    CHECK((ports.first[0].eax == CycleMemberRef{&first, 8}));
    CHECK((ports.first[0].esi == CycleMemberRef{&first, 0x76FD0}));
    CHECK((ports.first[0].arguments[0] == CycleMemberRef{&first, 0}));
    CHECK((ports.first[0].arguments[1] == CycleMemberRef{&first, 0x7A750}));
    CHECK((ports.second[0].edi == CycleMemberRef{&first, 0x9C0}));
    CHECK((ports.second[0].arguments[0] == CycleMemberRef{&first, 0}));
    CHECK((ports.second[0].arguments[1] == CycleMemberRef{&first, 8}));
    CHECK((ports.second[0].arguments[2] == CycleMemberRef{&first, 0x7A750}));
    CHECK(first.member_20.bytes[2] == std::byte{1});
    CHECK(first.member_20.bytes[3] == std::byte{0x91});
    CHECK(first.ready_8a3b1 == 1);
    CHECK(replacement.ready_8a3b1 == 0);
    CHECK(globals.object_fa48b0 == &replacement);

    // A nonzero guard set by a callback suppresses only 6FC0F0's writes,
    // not the caller's final ready store.
    CyclePorts guarded;
    globals.object_fa48b0 = &first;
    first.member_20.bytes.fill(std::byte{0x39});
    first.ready_8a3b1 = 0;
    guarded.effect = [&](int site) {
        if (site == 2)
            first.member_20.bytes[2] = std::byte{0x80};
    };
    CHECK(forward_service_9b4120(globals, guarded) ==
          ServiceCycleOutcome::native_return);
    CHECK(first.member_20.bytes[0] == std::byte{0x39});
    CHECK(first.member_20.bytes[2] == std::byte{0x80});
    CHECK(first.ready_8a3b1 == 1);

    for (int failure : {1, 2}) {
        CyclePorts rejected;
        rejected.refuse = failure;
        first.ready_8a3b1 = 0xCC;
        const auto before = first.member_20.bytes;
        CHECK(forward_service_9b4120(globals, rejected) ==
              (failure == 1 ? ServiceCycleOutcome::refused_prepare_708d50
                            : ServiceCycleOutcome::refused_prepare_7053e0));
        CHECK(rejected.trace.size() == static_cast<std::size_t>(failure));
        CHECK(first.member_20.bytes == before);
        CHECK(first.ready_8a3b1 == 0xCC);
    }
    globals.object_fa48b0 = nullptr;
    CyclePorts missing;
    CHECK(forward_service_9b4120(globals, missing) ==
          ServiceCycleOutcome::refused_missing_global);
    CHECK(missing.trace.empty());
}

void cleanup() {
    ForwardCycleGlobals globals;
    CyclePorts ports;
    Service9b3f80 service{{globals, ports}};
    std::array<std::byte, 16> a{}, b{}, replacement{}, overwritten{};
    service.allocation_04 = {a.data(), a.size()};
    service.allocation_08 = {b.data(), b.size()};
    ports.effect = [&](int) {
        if (ports.freed.size() == 1) {
            service.allocation_04 = {overwritten.data(), overwritten.size()};
            service.allocation_08 = {replacement.data(), replacement.size()};
        }
    };
    CHECK(service.on_shutdown() == ModeCallOutcome::native_returned);
    CHECK(ports.freed.size() == 2);
    CHECK(ports.freed[0].data == a.data());
    CHECK(ports.freed[1].data == replacement.data());
    CHECK(service.allocation_04.is_null() && service.allocation_08.is_null());
    CHECK(service.allocation_04.capacity == 0);
    CHECK(service.on_shutdown() == ModeCallOutcome::native_returned);
    CHECK(ports.freed.size() == 2);

    for (int fail_at : {1, 2}) {
        CyclePorts rejected;
        Service982c40 other{{globals, rejected}};
        other.allocation_04 = {a.data(),
                               0}; // Nonnull, zero capacity still free.
        other.allocation_08 = {b.data(), b.size()};
        rejected.effect = [&](int) {
            if (rejected.freed.size() == static_cast<std::size_t>(fail_at))
                rejected.refuse = 3;
        };
        CHECK(other.on_shutdown() == ModeCallOutcome::modern_refusal);
        CHECK(rejected.freed.size() == static_cast<std::size_t>(fail_at));
        CHECK(other.last_shutdown ==
              (fail_at == 1 ? ServiceCycleOutcome::refused_free_04
                            : ServiceCycleOutcome::refused_free_08));
        CHECK(other.allocation_04.data == (fail_at == 1 ? a.data() : nullptr));
        CHECK(other.allocation_08.data == b.data());
    }
}

struct Allocator final : AllocatorPort {
    std::deque<std::array<std::byte, 8>> blocks;
    Allocation allocate_slot20(std::uint32_t size, std::uint32_t a,
                               std::uint32_t b, std::uint32_t c) override {
        CHECK(size == 8 && a == 0 && b == 0 && c == 0);
        blocks.emplace_back();
        return {blocks.back().data(), blocks.back().size()};
    }
};
struct Helpers final : ModeLifecyclePorts, HelperContextSlot {
    HelperContext context{1, 8};
    Allocator allocator;
    HelperContext* load() override { return &context; }
    std::uint32_t startup_gate() override {
        CHECK(false);
        return 0;
    }
    ModeSubsystemPtr boot_subsystem(std::size_t) override {
        CHECK(false);
        return nullptr;
    }
    ModeCallOutcome begin_initialization(ModeSubsystem& member) override {
        auto* service = dynamic_cast<StartupServiceBase*>(&member);
        CHECK(service != nullptr);
        return initialization_helper_9b40c0(*service, *this, allocator) ==
                       HelperOutcome::filled
                   ? ModeCallOutcome::native_returned
                   : ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome end_initialization(ModeSubsystem& member) override {
        auto* service = dynamic_cast<StartupServiceBase*>(&member);
        CHECK(service != nullptr);
        return initialization_helper_9b4060(*service, *this, allocator) ==
                       HelperOutcome::filled
                   ? ModeCallOutcome::native_returned
                   : ModeCallOutcome::modern_refusal;
    }
    ModeCallOutcome configure_startup(GameModeRuntime&,
                                      ModeSubsystem&) override {
        CHECK(false);
        return ModeCallOutcome::modern_refusal;
    }
    ModeLifecycleStatus complete_startup(GameModeRuntime&) override {
        CHECK(false);
        return ModeLifecycleStatus::modern_refusal;
    }
};

void composed_services() {
    ForwardCycleObject object;
    ForwardCycleGlobals globals{&object};
    CyclePorts ports;
    auto first =
        std::make_shared<Service982c40>(ServiceCycleContext{globals, ports});
    auto second =
        std::make_shared<Service9b3f80>(ServiceCycleContext{globals, ports});
    Helpers helpers;
    GameModeRuntime runtime;
    CHECK(register_mode_subsystem(runtime, first, helpers) ==
          ModeLifecycleStatus::success);
    CHECK(register_mode_subsystem(runtime, second, helpers) ==
          ModeLifecycleStatus::success);
    CHECK(runtime.subsystem_count == 2);
    CHECK(first->owner_10 == &runtime && second->owner_10 == &runtime);
    CHECK(first->fields.field_28 == 1 && first->fields.field_2c == 1);
    CHECK(first->fields.field_30 == 0 && first->fields.field_34 == 0 &&
          first->fields.field_38 == 0);
    CHECK(helpers.allocator.blocks.size() == 4);
    CHECK(helpers.allocator.blocks[0].front() == std::byte{0xBD});
    CHECK(helpers.allocator.blocks[1].back() == std::byte{0xAD});
    CHECK(forward_mode_subsystems(runtime) == ModeLifecycleStatus::success);
    CHECK((ports.trace == std::vector<int>{1, 2, 1, 2}));
    CHECK(object.ready_8a3b1 == 1);
    CHECK(reset_mode_subsystems(runtime) == ModeLifecycleStatus::success);
    CHECK((ports.trace == std::vector<int>{1, 2, 1, 2, 1, 2, 1, 2}));
    const auto second04 = second->allocation_04.data;
    const auto first04 = first->allocation_04.data;
    CHECK(shutdown_mode_subsystems(runtime) == ModeLifecycleStatus::success);
    CHECK(runtime.subsystem_count == closed_subsystem_count);
    CHECK(ports.freed.size() == 4);
    CHECK(ports.freed[0].data == second04 && ports.freed[2].data == first04);
}

void refusal_propagation() {
    ForwardCycleObject object;
    ForwardCycleGlobals globals; // Missing global is a real modern refusal.
    CyclePorts ports;
    auto first =
        std::make_shared<Service9b3f80>(ServiceCycleContext{globals, ports});
    auto second =
        std::make_shared<Service9b3f80>(ServiceCycleContext{globals, ports});
    GameModeRuntime runtime;
    runtime.subsystem_slots = {first, second};
    runtime.subsystem_count = 2;
    CHECK(forward_mode_subsystems(runtime) ==
          ModeLifecycleStatus::modern_refusal);
    CHECK(first->last_forward == ServiceCycleOutcome::refused_missing_global);
    CHECK(!second->last_forward);
    CHECK(runtime.subsystem_count == 2);

    // A failed free must not drain another service or write the terminal
    // UINT32_MAX count. The native pre-callback decrement is retained.
    globals.object_fa48b0 = &object;
    std::array<std::byte, 8> data{};
    second->allocation_04 = {data.data(), data.size()};
    ports.refuse = 3;
    CHECK(shutdown_mode_subsystems(runtime) ==
          ModeLifecycleStatus::modern_refusal);
    CHECK(runtime.subsystem_count == 1);
    CHECK(!first->last_shutdown);
    CHECK(second->allocation_04.data == data.data());
    CHECK(ports.freed.size() == 1);
}
} // namespace

int main() {
    reset_bytes();
    forward_capture();
    cleanup();
    composed_services();
    refusal_propagation();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

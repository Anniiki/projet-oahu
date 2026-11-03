#include "tdur/game/startup_9b1df0.hpp"

#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace tdur::game;
using namespace tdur::game::startup;

namespace {

int failures = 0;
int checks = 0;

static_assert(sizeof(void*) == 8);

void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

using Log = std::vector<std::string>;
using Hook = std::function<void(const std::string&)>;

struct FakeAllocator final : AllocatorPort {
    Log* log = nullptr;
    Hook* hook = nullptr;
    std::byte storage[16]{};
    bool null_result = false;
    Allocation allocate_slot20(std::uint32_t size, std::uint32_t a2,
                               std::uint32_t a3, std::uint32_t a4) override {
        log->push_back(std::format("alloc:{:x}:{}{}{}", size, a2, a3, a4));
        (*hook)("alloc");
        if (null_result)
            return {};
        return {storage, sizeof storage};
    }
};

struct Object1699104 final : AllocatorObject1699104 {
    Log* log = nullptr;
    OpaqueWord result = 0;
    OpaqueWord slot20(std::uint32_t, std::uint32_t, std::uint32_t,
                      std::uint32_t) override {
        log->push_back("tail-alloc");
        return result;
    }
};

struct FakeTail final : Tail9b1df0Ports {
    Log* log = nullptr;
    Object1699104 object;
    bool null_object = false;
    unsigned constructions = 0;
    AllocatorObject1699104* load_1699104() override {
        log->push_back("load-1699104");
        return null_object ? nullptr : &object;
    }
    OpaqueWord call_402370(OpaqueWord edi, std::uint32_t value) override {
        ++constructions;
        check(value == 9, "tail first constructor selector");
        return edi;
    }
    OpaqueWord call_402320(OpaqueWord edi, std::uint32_t value) override {
        ++constructions;
        check(value == 0x14, "tail second constructor selector");
        return edi;
    }
    OpaqueWord call_401ea0(OpaqueWord edi, std::uint32_t value) override {
        ++constructions;
        check(value == 0x13, "tail third constructor selector");
        return edi;
    }
};

constexpr OpaqueWord big = OpaqueWord{1} << 40;

struct FakeCalls final : Init9b1df0Ports {
    Log* log = nullptr;
    Hook* hook = nullptr;
    Subobject9b1df0At1c* seen_1c = nullptr;
    Object6107C0 object_6107c0;
    bool null_6107c0 = false;
    std::uint32_t result_610840 = 0;
    std::map<std::uint32_t, bool> null_block; // keyed by call index 0..3
    unsigned blocks = 0;
    Object610EA0 first_610ea0{0x77};
    Object610EA0 second_610ea0{}; // All data zero: still a real object.
    bool null_first_610ea0 = false;
    bool null_second_610ea0 = false;
    unsigned calls_610ea0 = 0;
    unsigned calls_611f90_object = 0;
    Object610EA0* last_receiver = &first_610ea0; // overwritten by the call

    void note(const std::string& entry) {
        log->push_back(entry);
        (*hook)(entry.substr(0, entry.find(':')));
    }
    void call_606bc0(Subobject9b1df0At1c& self, NativeIdentity a1) override {
        seen_1c = &self;
        note(std::format("606bc0:{:x}", a1.va));
    }
    Object6107C0* call_6107c0(Object100FC80&) override {
        note("6107c0");
        return null_6107c0 ? nullptr : &object_6107c0;
    }
    void call_611300(Allocation a1, std::uint32_t a2) override {
        note(
            std::format("611300:{}:{:x}", a1.is_null() ? "null" : "block", a2));
    }
    void call_610e80(Object100FC80&, OpaqueWord a1, std::uint32_t a2) override {
        note(std::format("610e80:{:x}:{}", a1, a2));
    }
    std::uint32_t call_610840(Object100FC80&) override {
        note("610840");
        return result_610840;
    }
    void call_610900(Object100FC80&) override { note("610900"); }
    OpaqueWord call_6113c0(std::uint32_t size) override {
        const unsigned index = blocks++;
        note(std::format("6113c0:{:x}", size));
        return null_block[index] ? 0 : big + 0x100 * (index + 1);
    }
    OpaqueWord call_612430(OpaqueWord self, std::uint32_t a1) override {
        note(std::format("612430:{:x}:{}", self, a1));
        return self + 1;
    }
    OpaqueWord call_612500(OpaqueWord self, std::uint32_t a1) override {
        note(std::format("612500:{:x}:{}", self, a1));
        return self + 2;
    }
    OpaqueWord call_6126b0(OpaqueWord self, std::uint32_t a1,
                           OpaqueWord a2) override {
        note(std::format("6126b0:{:x}:{}:{:x}", self, a1, a2));
        return self + 3;
    }
    void call_6120f0(OpaqueWord self, NativeIdentity a1) override {
        note(std::format("6120f0:{:x}:{:x}", self, a1.va));
    }
    void call_611f90(OpaqueWord self, OpaqueWord a1) override {
        note(std::format("611f90:{:x}:{:x}", self, a1));
    }
    void call_611f90_object(Object610EA0* receiver,
                            OpaqueWord argument) override {
        ++calls_611f90_object;
        last_receiver = receiver;
        const char* name = receiver == nullptr          ? "null"
                           : receiver == &second_610ea0 ? "second"
                           : receiver == &first_610ea0  ? "first"
                                                        : "other";
        note(std::format("611f90-object:{}:{:x}", name, argument));
    }
    Object610EA0* call_610ea0(Object100FC80&) override {
        const bool null =
            calls_610ea0++ == 0 ? null_first_610ea0 : null_second_610ea0;
        note("610ea0");
        if (null)
            return nullptr;
        return calls_610ea0 == 1 ? &first_610ea0 : &second_610ea0;
    }
};

struct Rig {
    Log log;
    Hook hook = [](const std::string&) {};
    StartupGlobals globals;
    Object143F520 object_143f520;
    Object143F520 other_143f520;
    Globals9b1df0 globals_9b1df0;
    FakeAllocator allocator;
    FakeCalls calls;
    FakeTail tail;
    Fields9b1df0 fields;
    GameModeRuntime owner;
    struct Self final : StartupServiceBase {
        bool initialize(GameModeRuntime&) override { return false; }
        void on_forward_pass() override {}
        void on_reverse_pass() override {}
        void on_shutdown() override {}
    } self;

    Rig() {
        allocator.log = calls.log = tail.log = tail.object.log = &log;
        allocator.hook = calls.hook = &hook;
        globals.global_143f520 = &object_143f520;
        globals.block_1699070.field_4c = big + 0xBC;
        self.flag_18 = 0x55;
        fields.field_78 = 0x55;
        fields.field_8c = fields.field_90 = 0x55;
        fields.field_94 = fields.field_98 = 0x55;
        fields.tail.field_130 = 0x55;
    }
    Startup9b1df0Context context() {
        return {globals, allocator, globals_9b1df0, calls, tail};
    }
    Init9b1df0Outcome run() {
        return initialize_9b1df0(self, fields, owner, context());
    }
};

std::string hex(OpaqueWord value) { return std::format("{:x}", value); }

const Log prefix_log{"606bc0:f04a50",        "6107c0",
                     "alloc:300000:000",     "611300:block:300000",
                     "610e80:100000000bc:0", "610840"};

void test_nominal_order_and_writes() {
    Rig r;
    check(r.run() == Init9b1df0Outcome::completed, "nominal completes");
    const std::string b1 = hex(big + 0x100), b2 = hex(big + 0x200);
    const std::string b3 = hex(big + 0x300), b4 = hex(big + 0x400);
    const std::string c1 = hex(big + 0x101), c2 = hex(big + 0x202);
    const std::string c3 = hex(big + 0x301), c4 = hex(big + 0x403);
    Log expected = prefix_log;
    const Log rest{"6113c0:50",
                   "612430:" + b1 + ":5",
                   "6120f0:" + c1 + ":f04a34",
                   "6113c0:4c",
                   "612500:" + b2 + ":5",
                   "6120f0:" + c2 + ":f04a1c",
                   "611f90:" + c2 + ":" + c1,
                   "6113c0:50",
                   "612430:" + b3 + ":5",
                   "6120f0:" + c3 + ":f04a04",
                   "611f90:" + c3 + ":" + c2,
                   "6113c0:14c",
                   "610ea0",
                   "6126b0:" + b4 + ":5:77",
                   "6120f0:" + c4 + ":f049e8",
                   "611f90:" + c4 + ":" + c3,
                   "610ea0",
                   "611f90-object:second:" + c4,
                   "load-1699104",
                   "tail-alloc",
                   "load-1699104",
                   "tail-alloc",
                   "load-1699104",
                   "tail-alloc"};
    expected.insert(expected.end(), rest.begin(), rest.end());
    check(r.log == expected, "exact callee order and arguments");
    check(r.self.owner_10 == &r.owner && r.self.flag_18 == 0, "+10 and +18");
    check(r.calls.seen_1c == &r.fields.field_1c, "606BC0 receives this+1C");
    check(r.calls.object_6107c0.field_04 == 5, "6107C0 object +4 = 5");
    check(r.fields.field_78 == 0x30'0000 &&
              r.fields.field_7c.data == r.allocator.storage,
          "+78 and +7C");
    check(r.fields.field_8c == big + 0x101 &&
              r.fields.field_90 == big + 0x202 &&
              r.fields.field_94 == big + 0x301 &&
              r.fields.field_98 == big + 0x403,
          "constructor results stored as x64 values");
    check(r.globals_9b1df0.object_100fc80.field_2c == &r.object_143f520 &&
              r.globals_9b1df0.global_faa598 == &r.object_143f520,
          "143F520 copied to 100FCAC and FAA598");
    check(r.fields.tail.field_130 == 0, "tail ran after the prefix");
}

void test_rejection_whole_dword() {
    for (std::uint32_t value : {1U, 0x100U, 0x8000'0000U, 0xFFFF'FF00U}) {
        Rig r;
        r.calls.result_610840 = value;
        check(r.run() == Init9b1df0Outcome::rejected_610840,
              "nonzero DWORD rejects even with AL = 0");
        Log expected = prefix_log;
        expected.push_back("610900");
        check(r.log == expected, "610900 then return, no allocation, no tail");
        check(r.globals_9b1df0.global_faa598 == nullptr &&
                  r.globals_9b1df0.object_100fc80.field_2c == nullptr,
              "rejection precedes the 143F520 copies");
        check(r.fields.field_8c == 0x55 && r.fields.tail.field_130 == 0x55,
              "later fields untouched");
        check(r.calls.object_6107c0.field_04 == 5 && r.self.flag_18 == 0,
              "earlier effects kept");
    }
}

void test_null_allocation_sites() {
    for (unsigned site = 0; site < 4; ++site) {
        Rig r;
        r.calls.null_block[site] = true;
        check(r.run() == Init9b1df0Outcome::completed,
              "null allocation does not return early");
        const std::vector<std::string> identity{"f04a34", "f04a1c", "f04a04",
                                                "f049e8"};
        const std::string expected = "6120f0:0:" + identity[site];
        bool found = false;
        unsigned constructors = 0;
        for (const auto& entry : r.log) {
            found = found || entry == expected;
            constructors +=
                entry.starts_with("612") && !entry.starts_with("6120f0");
        }
        check(found, "6120F0 still called with null");
        check(constructors == 3, "only the null site skips its constructor");
        const OpaqueWord* stored[] = {&r.fields.field_8c, &r.fields.field_90,
                                      &r.fields.field_94, &r.fields.field_98};
        check(*stored[site] == 0, "null stored in the member");
        check(r.calls.blocks == 4, "following allocations still happen");
        if (site == 3)
            check(r.calls.calls_610ea0 == 1,
                  "0x14C null: 610EA0 only at 0x9B1F9C");
        if (site < 3) {
            const std::string next = "611f90:" + hex(*stored[site + 1]) + ":0";
            found = false;
            for (const auto& entry : r.log)
                found = found || entry == next;
            check(found, "null member is read live as 611F90 argument");
        }
    }
}

void test_refusal_null_6107c0() {
    Rig r;
    r.calls.null_6107c0 = true;
    check(r.run() == Init9b1df0Outcome::refused_null_6107c0, "refused 6107C0");
    check(r.log == Log(prefix_log.begin(), prefix_log.begin() + 3),
          "stops after the allocation");
    check(r.fields.field_78 == 0x30'0000 && !r.fields.field_7c.is_null() &&
              r.self.owner_10 == &r.owner,
          "earlier writes kept");
}

void test_refusal_null_610ea0() {
    Rig r;
    r.calls.null_first_610ea0 = true;
    check(r.run() == Init9b1df0Outcome::refused_null_610ea0, "refused 610EA0");
    check(r.log.back() == "610ea0" && r.fields.field_98 == 0x55,
          "no 6126B0 and +98 unwritten");
    check(r.fields.field_94 == big + 0x301, "earlier members kept");

    Rig s;
    s.calls.null_second_610ea0 = true;
    check(s.run() == Init9b1df0Outcome::completed,
          "second 610EA0 result is not dereferenced");
    bool found = false;
    for (const auto& entry : s.log)
        found = found || entry == "611f90-object:null:" + hex(big + 0x403);
    check(found && s.calls.last_receiver == nullptr &&
              s.calls.calls_611f90_object == 1,
          "null last receiver forwarded as nullptr, not refused");
}

void test_typed_last_receiver() {
    Rig r;
    check(r.calls.second_610ea0.field_04 == 0, "receiver data all zero");
    check(r.run() == Init9b1df0Outcome::completed, "zero object completes");
    check(r.calls.calls_611f90_object == 1 &&
              r.calls.last_receiver == &r.calls.second_610ea0,
          "the same typed object is the receiver, even with zero data");
    check(r.calls.last_receiver != nullptr,
          "a zero-data object is not confused with nullptr");
    Rig s;
    s.calls.null_second_610ea0 = true;
    s.run();
    check(s.calls.last_receiver == nullptr &&
              s.calls.last_receiver != r.calls.last_receiver,
          "nullptr receiver distinct from a non-null object");
    unsigned untyped = 0;
    for (const auto& entry : r.log)
        untyped += entry.starts_with("611f90:");
    check(untyped == 3, "untyped 611F90 entry used at the other three sites");
}

void test_tail_refusal_keeps_prefix() {
    Rig r;
    r.tail.null_object = true;
    check(r.run() == Init9b1df0Outcome::refused_null_1699104, "tail refusal");
    check(r.fields.field_98 == big + 0x403 && r.fields.tail.field_130 == 0x55,
          "prefix kept, tail incomplete");
}

void test_writes_around_callbacks() {
    Rig r;
    r.hook = [&](const std::string& name) {
        if (name == "606bc0")
            check(r.self.owner_10 == &r.owner && r.self.flag_18 == 0,
                  "+10/+18 before 606BC0");
        if (name == "6107c0")
            check(r.fields.field_78 == 0x55, "+78 written after 6107C0");
        if (name == "alloc") {
            check(r.fields.field_78 == 0x30'0000 &&
                      r.calls.object_6107c0.field_04 == 0,
                  "+78 before allocation, +4 after it");
            r.fields.field_78 = 0x1234;
        }
        if (name == "611300") {
            check(r.calls.object_6107c0.field_04 == 5, "+4 before 611300");
            r.globals.block_1699070.field_4c = 0xABC;
        }
        if (name == "610840")
            r.globals.global_143f520 = &r.other_143f520;
        if (name == "6120f0" && r.calls.blocks == 1) {
            check(r.fields.field_8c == big + 0x101, "+8C stored before 6120F0");
            r.fields.field_8c = 0xC8C;
        }
        if (name == "610ea0" && r.calls.calls_610ea0 == 2)
            r.fields.field_98 = 0xD98;
    };
    check(r.run() == Init9b1df0Outcome::completed, "mutating run completes");
    check(r.log[3] == "611300:block:1234", "+78 read after allocator");
    check(r.log[4] == "610e80:abc:0", "16990BC read after 611300");
    check(r.globals_9b1df0.global_faa598 == &r.other_143f520,
          "143F520 read after 610840");
    bool found = false;
    for (const auto& entry : r.log)
        found = found || entry == "611f90:" + hex(big + 0x202) + ":c8c";
    check(found, "+8C read after 6120F0");
    found = false;
    for (const auto& entry : r.log)
        found = found || entry == "611f90-object:second:" + hex(big + 0x403);
    check(found && r.fields.field_98 == 0xD98,
          "+98 captured before the last 610EA0, mutation not forwarded");
}

struct TestService final : Service9b1df0 {
    using Service9b1df0::Service9b1df0;
    void on_forward_pass() override {}
    void on_reverse_pass() override {}
    void on_shutdown() override {}
};

struct Lifecycle final : ModeLifecyclePorts {
    std::uint32_t startup_gate() override { return 1; }
    ModeSubsystemPtr boot_subsystem(std::size_t) override { return {}; }
    void begin_initialization(ModeSubsystem&) override {}
    void end_initialization(ModeSubsystem&) override {}
    void configure_startup(GameModeRuntime&, ModeSubsystem&) override {}
    bool complete_startup(GameModeRuntime&) override { return true; }
};

void test_controller_registration() {
    for (std::uint32_t gate : {0U, 0x100U}) {
        Rig r;
        r.calls.result_610840 = gate;
        auto service = std::make_shared<TestService>(r.context());
        check(!service->last_outcome.has_value(),
              "no outcome before the first initialize");
        Lifecycle ports;
        GameModeRuntime state;
        const auto status = register_mode_subsystem(state, service, ports);
        check(state.subsystem_count == 1, "member counted in all cases");
        check(status == (gate == 0 ? ModeLifecycleStatus::success
                                   : ModeLifecycleStatus::rejected),
              "rejected member reported as rejected");
        check(service->owner_10 == &state, "owner is the runtime");
        check(service->last_outcome.has_value() &&
                  *service->last_outcome ==
                      (gate == 0 ? Init9b1df0Outcome::completed
                                 : Init9b1df0Outcome::rejected_610840),
              "outcome engaged after initialize");
    }
}

void test_full_driver_allocator_edges() {
    Rig empty;
    empty.allocator.null_result = true;
    check(empty.run() == Init9b1df0Outcome::completed &&
              empty.fields.field_7c.is_null() &&
              empty.log[3] == "611300:null:300000",
          "main allocation null is forwarded without an invented early return");
    Rig populated;
    populated.tail.object.result = big + 0x199;
    check(populated.run() == Init9b1df0Outcome::completed &&
              populated.tail.constructions == 3,
          "the complete driver invokes the three nonnull tail callees");
    check(populated.fields.tail.field_9c == big + 0x199 &&
              populated.fields.tail.field_a8 == big + 0x199 &&
              populated.fields.tail.field_ac == big + 0x199,
          "the complete driver retains the tail's opaque x64 outputs");
}

} // namespace

int main() {
    test_nominal_order_and_writes();
    test_rejection_whole_dword();
    test_null_allocation_sites();
    test_refusal_null_6107c0();
    test_refusal_null_610ea0();
    test_tail_refusal_keeps_prefix();
    test_typed_last_receiver();
    test_writes_around_callbacks();
    test_controller_registration();
    test_full_driver_allocator_edges();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

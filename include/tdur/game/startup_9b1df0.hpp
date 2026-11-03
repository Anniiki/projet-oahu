#pragma once

#include "tdur/game/startup_9b1df0_tail.hpp"
#include "tdur/game/startup_services.hpp"

#include <cstdint>
#include <optional>

// Complete initialization driver 0x9B1DF0 (controller index 9, candidate
// table 0xFA4320): 186 instructions, two RET 4 (0x9B1E81 and 0x9B2063).
// The prefix 0x9B1DF0..0x9B1FA3 is reconstructed here; the final sub-block
// 0x9B1FA8..0x9B2063 is the existing run_9b1df0_tail. Every direct callee
// and every indirect call stays an explicit port: no callee body is known.
// The role of the service is not established; names follow native offsets.
namespace tdur::game::startup {

// Member +0x1C, passed as ECX to 0x606BC0. Its contents are unknown.
struct Subobject9b1df0At1c {};

// Object returned by 0x6107C0. The driver writes DWORD +0x04 = 5 into it.
// The port implementer owns it; the driver never frees or replaces it.
struct Object6107C0 {
    std::uint32_t field_04 = 0;
};

// Object returned by 0x610EA0. DWORD +0x04 is read on the 0x14C path; at
// 0x9B1FA3 the object itself is the ECX receiver of 0x611F90. Owned by the
// port implementer, never freed here.
struct Object610EA0 {
    OpaqueWord field_04 = 0;
};

// Object 0x100FC80 (ECX of 0x6107C0, 0x610E80, 0x610840, 0x610900 and
// 0x610EA0). Address 0x100FCAC is held as its +0x2C, following the block
// convention of startup_services; the grouping is not demonstrated.
struct Object100FC80 {
    Object143F520* field_2c = nullptr; // 0x100FCAC
};

// Owned modern data for the globals written by this driver that are not in
// StartupGlobals. Globals 0x143F520 and 0x16990BC stay in StartupGlobals.
struct Globals9b1df0 {
    Object100FC80 object_100fc80;
    Object143F520* global_faa598 = nullptr;
};

// Service members written by the driver, named by native offset. +0x10
// and +0x18 are StartupServiceBase::owner_10 and flag_18.
struct Fields9b1df0 {
    Subobject9b1df0At1c field_1c;
    std::uint32_t field_78 = 0;
    Allocation field_7c; // Allocator 0xFA9CB0 result; never freed here.
    OpaqueWord field_8c = 0;
    OpaqueWord field_90 = 0;
    OpaqueWord field_94 = 0;
    OpaqueWord field_98 = 0;
    Fields9b1df0Tail tail; // +0x9C..+0x3EF
};

// Unresolved direct callees of the prefix. Arguments are the demonstrated
// pushes in order (first argument = last push); ECX is "self". Results are
// typed only where the driver consumes them. A self of 0 is the native null
// ECX value and is forwarded as is.
class Init9b1df0Ports {
  public:
    virtual ~Init9b1df0Ports() = default;
    // 0x9B1E07: ECX = this+0x1C, push 0xF04A50.
    virtual void call_606bc0(Subobject9b1df0At1c& self,
                             NativeIdentity arg1) = 0;
    // 0x9B1E11: ECX = 0x100FC80. Null result: see refused_null_6107c0.
    virtual Object6107C0* call_6107c0(Object100FC80& self) = 0;
    // 0x9B1E4A: pushes +0x78 then +0x7C, caller cleans 8 bytes.
    virtual void call_611300(Allocation arg1, std::uint32_t arg2) = 0;
    // 0x9B1E60: ECX = 0x100FC80, pushes 0 then DWORD [0x16990BC].
    virtual void call_610e80(Object100FC80& self, OpaqueWord arg1,
                             std::uint32_t arg2) = 0;
    // 0x9B1E6A: the whole DWORD is tested (TEST EAX,EAX at 0x9B1E6F).
    virtual std::uint32_t call_610840(Object100FC80& self) = 0;
    // 0x9B1E78: called only when 0x610840 returned nonzero.
    virtual void call_610900(Object100FC80& self) = 0;
    // 0x9B1E95, 0x9B1EC2, 0x9B1F01, 0x9B1F43: one push, caller cleans.
    virtual OpaqueWord call_6113c0(std::uint32_t size) = 0;
    // 0x9B1EA5, 0x9B1F11: ECX = nonzero 0x6113C0 result, push 5.
    virtual OpaqueWord call_612430(OpaqueWord self, std::uint32_t arg1) = 0;
    // 0x9B1ED2: ECX = nonzero 0x6113C0 result, push 5.
    virtual OpaqueWord call_612500(OpaqueWord self, std::uint32_t arg1) = 0;
    // 0x9B1F63: ECX = nonzero 0x6113C0 result, pushes [R+4] then 5.
    virtual OpaqueWord call_6126b0(OpaqueWord self, std::uint32_t arg1,
                                   OpaqueWord arg2) = 0;
    // 0x9B1EBB, 0x9B1EE8, 0x9B1F27, 0x9B1F79: ECX may be 0.
    virtual void call_6120f0(OpaqueWord self, NativeIdentity arg1) = 0;
    // 0x9B1EFA, 0x9B1F39, 0x9B1F8B: one push, ECX = a stored member.
    virtual void call_611f90(OpaqueWord self, OpaqueWord arg1) = 0;
    // 0x9B1FA3 only: same callee, ECX = the pointer 0x610EA0 returned at
    // 0x9B1F9C. A null receiver is forwarded as nullptr, never refused.
    virtual void call_611f90_object(Object610EA0* receiver,
                                    OpaqueWord argument) = 0;
    // 0x9B1F56, 0x9B1F9C: ECX = 0x100FC80.
    virtual Object610EA0* call_610ea0(Object100FC80& self) = 0;
};

// Everything the driver reads or writes outside the service. Single source
// of truth: the references are read live at each native access.
struct Startup9b1df0Context {
    StartupGlobals& globals;  // 0x143F520, 0x16990BC.
    AllocatorPort& allocator; // Object 0xFA9CB0, slot +0x20 (0x9B1E35).
    Globals9b1df0& globals_9b1df0;
    Init9b1df0Ports& calls;
    Tail9b1df0Ports& tail; // 0x1699104 and the three non-ordinary callees.
};

enum class Init9b1df0Outcome {
    completed,       // Native AL = 1 at 0x9B2063: this init only.
    rejected_610840, // Native AL = 0 at 0x9B1E81, after 0x610900.
    // Modern refusals where the native would dereference null. Effects
    // already made stay made; none of them is a native result.
    refused_null_6107c0,  // [EDI+4] at 0x9B1E3B.
    refused_null_610ea0,  // [EAX+4] at 0x9B1F5B.
    refused_null_1699104, // Tail: [[0x1699104]] at 0x9B1FAE/E4/2010.
};

Init9b1df0Outcome initialize_9b1df0(StartupServiceBase& self,
                                    Fields9b1df0& fields,
                                    GameModeRuntime& owner,
                                    Startup9b1df0Context context);

// Service exposing the driver as native slot +0x08. Cycle slots +0x0C,
// +0x18 and +0x1C stay pure virtual: they are not reconstructed.
class Service9b1df0 : public StartupServiceBase {
  public:
    explicit Service9b1df0(Startup9b1df0Context context) : context_(context) {}
    Fields9b1df0 fields;
    // Empty until the first initialize(); then the last driver outcome.
    std::optional<Init9b1df0Outcome> last_outcome;
    bool initialize(GameModeRuntime& owner) override;

  private:
    Startup9b1df0Context context_;
};

} // namespace tdur::game::startup

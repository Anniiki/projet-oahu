#include "tdur/game/startup_9b1df0_tail.hpp"

namespace tdur::game::startup {
namespace {

using Construct = OpaqueWord (Tail9b1df0Ports::*)(OpaqueWord, std::uint32_t);

// One allocation then, when it is non zero, the opaque call with EAX set.
// Returns false when the object at 0x1699104 is missing.
bool allocate_then_construct(Tail9b1df0Ports& ports, Construct construct,
                             std::uint32_t eax_value, OpaqueWord& stored) {
    AllocatorObject1699104* object = ports.load_1699104();
    if (object == nullptr) {
        return false;
    }
    const OpaqueWord edi = object->slot20(0x18U, 0U, 0U, 0U);
    stored = edi == 0 ? OpaqueWord{0} : (ports.*construct)(edi, eax_value);
    return true;
}

} // namespace

TailOutcome run_9b1df0_tail(Fields9b1df0Tail& self, Tail9b1df0Ports& ports) {
    OpaqueWord value = 0;
    if (!allocate_then_construct(ports, &Tail9b1df0Ports::call_402370, 0x9U,
                                 value)) {
        return TailOutcome::refused_null_1699104;
    }
    self.field_9c = value;
    self.field_a0 = 0;
    self.field_a4 = 0;
    if (!allocate_then_construct(ports, &Tail9b1df0Ports::call_402320, 0x14U,
                                 value)) {
        return TailOutcome::refused_null_1699104;
    }
    self.field_a8 = value;
    if (!allocate_then_construct(ports, &Tail9b1df0Ports::call_401ea0, 0x13U,
                                 value)) {
        return TailOutcome::refused_null_1699104;
    }
    self.field_ac = value;
    self.field_130 = 0;
    self.field_134.fill(0);
    self.field_374.fill(0);
    return TailOutcome::completed;
}

} // namespace tdur::game::startup

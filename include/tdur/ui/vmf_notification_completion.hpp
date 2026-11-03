#pragma once

#include <cstdint>
#include <expected>

namespace tdur::ui {

enum class VmfNotificationCompletionError {
    reserved_precision,
    unmasked_exceptions,
};
struct VmfNotificationCompletion {
    std::uint32_t binary32_bits;
    std::int32_t integer;
    bool operator==(const VmfNotificationCompletion&) const = default;
};

// Value path717A53..717A8C: signed FILD, conditional+2^32, two FMULs,
// binary32 store, floor then integer conversion. Input is the uint32 sum
// from real89BD90, not an already scaled percent. Use an established x87
// control word: PC24/53/64, all four RC modes, masked exceptions. PC01 and
// unmasked traps are explicitly outside this value model. Integer arithmetic
// reproduces each precision/store rounding without using ambient host FP.
// No allocation or FP-environment change. Original x87 status/exception flags
// and general674EB4 behavior outside this bounded path are not modeled.
[[nodiscard]] std::expected<VmfNotificationCompletion,
                            VmfNotificationCompletionError>
compute_vmf_notification_completion(std::uint32_t sum,
                                    std::uint16_t x87_control_word) noexcept;

} // namespace tdur::ui

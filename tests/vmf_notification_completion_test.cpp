#include "vmf_notification_completion_cases.hpp"

#include <cfenv>
#include <cstdlib>
#include <iostream>

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << text << '\n';
    }
}
void instruction_vectors() {
    for (const auto& row : tdur::test::completion_goldens) {
        const auto result =
            compute_vmf_notification_completion(row.sum, row.control);
        check(result && *result == row.expected,
              "value matches independent x86 instruction and historical floor "
              "oracle");
    }
}
void refused_environments() {
    using Error = VmfNotificationCompletionError;
    for (unsigned mode = 0; mode < 4; ++mode) {
        const auto reserved = compute_vmf_notification_completion(
            10, static_cast<std::uint16_t>(0x17fU | (mode << 10)));
        check(!reserved && reserved.error() == Error::reserved_precision,
              "reserved PC01 has no inferred arithmetic semantics");
        for (const unsigned pc : {0U, 2U, 3U}) {
            for (unsigned exception = 0; exception < 6; ++exception) {
                const auto control = static_cast<std::uint16_t>(
                    ((pc << 8) | (mode << 10) | 0x7fU) & ~(1U << exception));
                const auto unmasked =
                    compute_vmf_notification_completion(10, control);
                check(!unmasked &&
                          unmasked.error() == Error::unmasked_exceptions,
                      "unmasked native traps are not silently converted into a "
                      "value");
            }
        }
    }
}
void ambient_environment() {
    std::fenv_t saved{};
    check(std::fegetenv(&saved) == 0, "capture owned test's FP environment");
    for (const auto mode :
         {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        check(std::fesetround(mode) == 0,
              "set independent ambient rounding mode");
        std::feclearexcept(FE_ALL_EXCEPT);
        check(std::feraiseexcept(FE_INVALID | FE_INEXACT) == 0,
              "establish ambient exception flags");
        const auto flags = std::fetestexcept(FE_ALL_EXCEPT);
        const auto result = compute_vmf_notification_completion(10, 0x027f);
        check(result && *result == VmfNotificationCompletion{0x3f800000, 1},
              "explicit control word determines result independently of "
              "ambient FP");
        check(std::fegetround() == mode &&
                  std::fetestexcept(FE_ALL_EXCEPT) == flags,
              "integer-only computation preserves ambient rounding and flags");
    }
    check(std::fesetenv(&saved) == 0, "restore owned test's FP environment");
}
} // namespace

int main() {
    instruction_vectors();
    refused_environments();
    ambient_environment();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

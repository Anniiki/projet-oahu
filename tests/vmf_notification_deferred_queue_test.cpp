#include "tdur/ui/vmf_notification_deferred_queue.hpp"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>
#include <type_traits>

namespace {
using namespace tdur::ui;
using Code = VmfNotificationDeferredErrorCode;
std::size_t checks{}, failures{};
void check(bool value, const char* text) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << text << '\n';
    }
}
bool available_from_other_thread(VmfNotificationDeferredQueue& queue) {
    auto result = std::async(std::launch::async, [&] {
        const auto end =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            if (queue.synchronization_318.try_lock()) {
                queue.synchronization_318.unlock();
                return true;
            }
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < end);
        return false;
    });
    return result.get();
}
void capacity_and_order() {
    VmfNotificationDeferredOwner canary{0x7f};
    std::array<VmfNotificationDeferredOwner*, 64> initial;
    initial.fill(&canary);
    VmfNotificationDeferredQueue queue{0, initial};
    std::array<VmfNotificationDeferredOwner, 64> owners;
    for (auto& owner : owners)
        owner.flag_04 = 0xa5;
    for (std::uint32_t index = 0; index < 63; ++index) {
        const auto result =
            enqueue_vmf_notification_deferred(queue, &owners[index]);
        check(result && result->result == 1 && result->before_count == index &&
                  result->after_count == index + 1 && result->slot == index,
              "Normal enqueue reports the actual old slot and new count");
        check(queue.slots_210[index] == &owners[index] &&
                  owners[index].flag_04 == 1,
              "Enqueue stores the owner identity and flag");
        check(queue.slots_210[63] == &canary && canary.flag_04 == 0x7f,
              "Reserved native slot63 and its owner stay untouched");
    }
    const auto before = queue.slots_210;
    const auto full = enqueue_vmf_notification_deferred(queue, &owners[63]);
    check(full && full->result == 0 && !full->slot && queue.count_314 == 63 &&
              queue.slots_210 == before && owners[63].flag_04 == 0xa5,
          "64th normal request is full without any slot/count/flag write");
    const auto null = enqueue_vmf_notification_deferred(queue, nullptr);
    check(null && null->result == 0 && queue.slots_210 == before,
          "Full path never dereferences an unused NULL owner");
    check(available_from_other_thread(queue),
          "Full return releases the actual manager mutex");
}
void signed_guard_and_errors() {
    constexpr std::array full_counts{63U, 64U, 65U, 0x100U, 0x7ffffffeU};
    for (const auto count : full_counts) {
        VmfNotificationDeferredQueue queue{count, {}};
        const auto result = enqueue_vmf_notification_deferred(queue, nullptr);
        check(result && result->result == 0 && result->before_count == count &&
                  result->after_count == count && queue.count_314 == count,
              "Capacity guard precedes index/owner validation on full paths");
    }
    constexpr std::array invalid_counts{0x7fffffffU, 0x80000000U, 0x8000003fU,
                                        0xfffffffeU, 0xffffffffU};
    for (const auto count : invalid_counts) {
        VmfNotificationDeferredOwner owner{0x5a};
        VmfNotificationDeferredQueue queue{count, {}};
        const auto before = queue.slots_210;
        const auto result = enqueue_vmf_notification_deferred(queue, &owner);
        check(!result && result.error().code == Code::invalid_count,
              "Wrapped signed guard can pass before invalid native index");
        check(queue.count_314 == count && queue.slots_210 == before &&
                  owner.flag_04 == 0x5a,
              "Modern invalid-index refusal preserves owned state");
        check(available_from_other_thread(queue),
              "Range failure releases the actual manager mutex");
    }
    VmfNotificationDeferredQueue queue{0, {}};
    const auto result = enqueue_vmf_notification_deferred(queue, nullptr);
    check(!result && result.error().code == Code::invalid_owner &&
              queue.count_314 == 0 && queue.slots_210[0] == nullptr,
          "Reached NULL owner is a modern refusal, not AL0/full");
    check(available_from_other_thread(queue),
          "NULL-owner refusal does not leak the manager lock");
}
void recursive_independent_managers() {
    VmfNotificationDeferredOwner owner;
    VmfNotificationDeferredQueue queue{0, {}};
    {
        const std::lock_guard outer(queue.synchronization_318);
        const auto first = enqueue_vmf_notification_deferred(queue, &owner);
        const auto second = enqueue_vmf_notification_deferred(queue, &owner);
        check(
            first && second && queue.count_314 == 2 &&
                queue.slots_210[0] == &owner && queue.slots_210[1] == &owner,
            "Recursive acquisition is real and duplicate owners are retained");
    }
    check(available_from_other_thread(queue),
          "Recursive calls balance every acquired level");

    VmfNotificationDeferredQueue independent{0, {}};
    VmfNotificationDeferredOwner other;
    std::unique_lock hold(queue.synchronization_318);
    auto result = std::async(std::launch::async, [&] {
        return enqueue_vmf_notification_deferred(independent, &other);
    });
    const bool ready =
        result.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    hold.unlock();
    const auto inserted = result.get();
    check(ready && inserted && inserted->result == 1 &&
              independent.count_314 == 1 && other.flag_04 == 1,
          "Another manager is independent of the held owner's manager lock");
}
} // namespace
int main() {
    static_assert(!std::is_copy_constructible_v<VmfNotificationDeferredQueue>);
    static_assert(!std::is_move_constructible_v<VmfNotificationDeferredQueue>);
    capacity_and_order();
    signed_guard_and_errors();
    recursive_independent_managers();
    std::cout << checks << " deferred-queue checks, " << failures
              << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

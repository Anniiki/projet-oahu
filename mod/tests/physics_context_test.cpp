#include "tdur/mod/observation_hooks.hpp"
#include "tdur/mod/retail_physics_context.hpp"

#include <intrin.h>
#include <xmmintrin.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Callback = void(CALLBACK*)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
extern "C" unsigned TdurAttachOwnedPhysicsContextGate(Callback);
extern "C" unsigned TdurDetachOwnedPhysicsContextGate();

namespace {
unsigned checks{};
std::atomic<unsigned> original_calls{}, invalid_arguments{}, callback_logs{};
thread_local bool in_callback{}; // Fixture only, never production capture.
std::vector<std::string> events;
std::uint8_t fixture_busy = 0x5a;

void require(bool value, const char* reason) {
    ++checks;
    if (!value)
        throw std::runtime_error(reason);
}
struct Words {
    std::uint16_t control;
    std::uint32_t mxcsr;
};
Words read_words() {
    std::uint16_t control;
    __asm { fnstcw control }
    return {control, _mm_getcsr()};
}
void write_words(Words words) {
    const auto control = words.control;
    __asm { fldcw control }
    _mm_setcsr(words.mxcsr);
}
struct RestoreWords {
    Words words = read_words();
    ~RestoreWords() { write_words(words); }
};
std::array<Words, 128> expected{};
std::array<std::uint32_t, 128> original_return_addresses{};

void CALLBACK owned_body(UINT id, UINT message, DWORD_PTR user, DWORD_PTR first,
                         DWORD_PTR second) {
    if (id < original_return_addresses.size())
        original_return_addresses[id] =
            std::bit_cast<std::uint32_t>(_ReturnAddress());
    if (message != (id ^ 0xaabbccddU) || user != (id ^ 0xdeadbeefU) ||
        first != (id ^ 0x80000000U) || second != (id ^ 0xffffffffU))
        invalid_arguments.fetch_add(1);
    original_calls.fetch_add(1);
}
// Same five-byte MOV AL,moffs8 shape, followed by a normal stdcall body.
__declspec(naked) void CALLBACK owned_entry(UINT, UINT, DWORD_PTR, DWORD_PTR,
                                            DWORD_PTR) {
    __asm {
        mov al, byte ptr [fixture_busy]
        jmp owned_body
    }
}
void invoke(unsigned id) {
    const auto before = read_words();
    expected[id] = before;
    volatile std::uint32_t canary = 0xfeedbabe;
    in_callback = true;
    owned_entry(id, id ^ 0xaabbccddU, id ^ 0xdeadbeefU, id ^ 0x80000000U,
                id ^ 0xffffffffU);
    in_callback = false;
    const auto after = read_words();
    if (canary != 0xfeedbabe || before.control != after.control ||
        before.mxcsr != after.mxcsr)
        invalid_arguments.fetch_add(1);
}
} // namespace

void TdurObservationRecord(const char* event, DWORD, const wchar_t*,
                           const char*, const void*) noexcept {
    if (in_callback)
        callback_logs.fetch_add(1);
    events.emplace_back(event);
}

int main() {
    try {
        const auto address = std::bit_cast<std::uintptr_t>(&owned_entry);
        std::array<std::uint8_t, 5> prefix{};
        std::memcpy(prefix.data(), reinterpret_cast<const void*>(address),
                    prefix.size());
        require(prefix[0] == 0xa0, "fixture has wrong MOV AL shape");
        require(TdurReadPhysicsContext().reserved_slots == 0,
                "capture not initially empty");
        require(TdurAttachOwnedPhysicsContextGate(&owned_entry) == 0,
                "owned detour/trampoline verification failed");
        {
            RestoreWords restore;
            auto words = restore.words;
            words.control = static_cast<std::uint16_t>(
                (words.control & ~0x0c00U) | 0x0400U | 0x003fU);
            words.mxcsr = (words.mxcsr & ~0x603fU) | 0x6000U | 0x1f80U | 0x21U;
            write_words(words);
            invoke(1);
            const auto captured = TdurReadPhysicsContext();
            require(captured.reserved_slots == 1 && captured.samples[0].ready &&
                        captured.samples[0].timer_id == 1 &&
                        captured.samples[0].x87_control == words.control &&
                        captured.samples[0].mxcsr == words.mxcsr,
                    "entry FP words changed or not captured");
        }
        std::atomic<bool> finish_reader{}, reader_started{}, coherent{true};
        std::thread reader([&] {
            do {
                const auto snapshot = TdurReadPhysicsContext();
                unsigned ready_count{};
                for (const auto& sample : snapshot.samples)
                    ready_count += sample.ready ? 1U : 0U;
                if (ready_count > snapshot.reserved_slots ||
                    snapshot.reserved_slots > 32)
                    coherent = false;
                reader_started = true;
            } while (!finish_reader.load());
        });
        while (!reader_started.load())
            std::this_thread::yield();
        std::vector<std::thread> workers;
        for (unsigned group = 0; group < 4; ++group)
            workers.emplace_back([group] {
                for (unsigned i = 0; i < 16; ++i)
                    invoke(2 + group * 16 + i);
            });
        for (auto& worker : workers)
            worker.join();
        finish_reader = true;
        reader.join();
        require(coherent, "concurrent ready count exceeded reservation count");
        const auto filled = TdurReadPhysicsContext();
        require(filled.reserved_slots == 32, "slot CAS did not saturate");
        std::array<bool, 128> seen{};
        for (const auto& sample : filled.samples) {
            require(sample.ready && sample.timer_id < expected.size() &&
                        !seen[sample.timer_id],
                    "unpublished, torn or reused slot");
            seen[sample.timer_id] = true;
            require(sample.x87_control == expected[sample.timer_id].control &&
                        sample.mxcsr == expected[sample.timer_id].mxcsr,
                    "concurrent capture differs from caller context");
            HMODULE caller_module{};
            require(
                sample.caller_code_address != 0 &&
                    sample.caller_code_address ==
                        original_return_addresses[sample.timer_id] &&
                    GetModuleHandleExW(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCWSTR>(sample.caller_code_address),
                        &caller_module) &&
                    caller_module == GetModuleHandleW(nullptr),
                "original caller return address was not preserved");
        }
        for (unsigned i = 0; i < 1000; ++i)
            invoke(100);
        require(TdurReadPhysicsContext() == filled,
                "full storage modified/reused");
        require(original_calls == 1065 && invalid_arguments == 0,
                "five-DWORD forwarding, stack or FPU changed");
        TdurFlushPhysicsContext();
        const auto logged = events.size();
        require(logged == 33 && callback_logs == 0,
                "logging occurred in callback or wrong count");
        require(events.front().find("caller_module=host_image") !=
                    std::string::npos,
                "caller module was not classified outside callback");
        TdurFlushPhysicsContext();
        require(events.size() == logged, "immutable slot logged twice");
        require(TdurDetachOwnedPhysicsContextGate() == 0,
                "owned detour did not detach");
        require(std::memcmp(prefix.data(),
                            reinterpret_cast<const void*>(address),
                            prefix.size()) == 0,
                "owned code not restored");
        invoke(101);
        require(original_calls == 1066 && TdurReadPhysicsContext() == filled,
                "detached callback still captured");
        TdurObservePhysicsTimerBinding();
        require(
            events.back().find("physics-context-timer-binding readable=0") == 0,
            "owned fixture falsely attributed a retail timer call cell");
        std::cout << checks << " physics entry context checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

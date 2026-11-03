#include "tdur/mod/retail_background.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

using Initializer = bool(__thiscall*)(void*, void*);
extern "C" bool TdurExerciseRendererGate(Initializer, void*, void*);
extern "C" unsigned TdurExerciseOwnedRendererDetour(void*, void*, void*);

namespace {
unsigned checks = 0;
unsigned enters = 0;
unsigned leaves = 0;
thread_local unsigned depth = 0;
constexpr DWORD fixture_exception = 0xe0424242;

void Require(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}

struct Renderer {
    void* seen_parent = nullptr;
    Renderer* seen_self = nullptr;
    unsigned calls = 0;
    bool answer = false;
    bool nested = false;
    bool nested_scope_restored = false;
    bool raise = false;

    virtual bool initialize(void* parent) {
        seen_self = this;
        seen_parent = parent;
        ++calls;
        if (nested) {
            nested = false;
            const auto code = (*reinterpret_cast<void***>(this))[0];
            const auto init = std::bit_cast<Initializer>(code);
            TdurExerciseRendererGate(init, this, parent);
            nested_scope_restored = depth == 1;
        }
        if (raise)
            RaiseException(fixture_exception, 0, 0, nullptr);
        return answer;
    }
};

unsigned ExceptionThroughGate(Initializer initializer, void* renderer,
                              void* parent) {
    __try {
        TdurExerciseRendererGate(initializer, renderer, parent);
        return 0;
    } __except (GetExceptionCode() == fixture_exception
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
        return 1;
    }
}

} // namespace

bool TdurEnterInitialRendererPhase(void*, void*, void*) noexcept {
    ++enters;
    ++depth;
    return depth == 1;
}

void TdurLeaveInitialRendererPhase() noexcept {
    ++leaves;
    if (depth)
        --depth;
}

int main() {
    try {
        Renderer renderer;
        const auto code = (*reinterpret_cast<void***>(&renderer))[0];
        const auto initializer = std::bit_cast<Initializer>(code);
        std::uint32_t parent = 0x12345678;
        volatile std::uint32_t stack_canary = 0xfeedbabe;
        Require(!TdurExerciseRendererGate(initializer, &renderer, &parent),
                "AL false return was changed");
        Require(renderer.seen_self == &renderer &&
                    renderer.seen_parent == &parent && renderer.calls == 1,
                "ECX this or the stack parent argument was not forwarded");
        renderer.answer = true;
        Require(TdurExerciseRendererGate(initializer, &renderer, &parent),
                "AL true return was changed");
        Require(depth == 0 && enters == 2 && leaves == 2,
                "ordinary return did not close both gate scopes");
        renderer.nested = true;
        Require(TdurExerciseRendererGate(initializer, &renderer, &parent) &&
                    renderer.nested_scope_restored && depth == 0 &&
                    enters == 4 && leaves == 4,
                "refused nested admission did not balance its own scope");
        renderer.raise = true;
        Require(ExceptionThroughGate(initializer, &renderer, &parent) == 1 &&
                    depth == 0 && enters == 5 && leaves == 5,
                "Windows SEH unwind left a stale admission scope");
        renderer.raise = false;
        Require(TdurExerciseRendererGate(initializer, &renderer, &parent) &&
                    depth == 0 && enters == 6 && leaves == 6,
                "gate did not recover after the owned SEH fixture");
        Require(stack_canary == 0xfeedbabe && parent == 0x12345678,
                "RET4 adaptation damaged stack arguments or canary");
        // Own machine-code fixture with the same seven-byte prologue shape.
        // It records its sole stack argument through ECX and returns true.
        constexpr std::array<std::uint8_t, 17> code_bytes = {
            0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x2c, 0x89, 0x01,
            0x83, 0xc4, 0x2c, 0xb0, 0x01, 0xc2, 0x04, 0x00};
        auto* owned = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
        Require(owned != nullptr, "allocate owned trampoline fixture");
        std::memcpy(owned, code_bytes.data(), code_bytes.size());
        DWORD previous = 0;
        Require(VirtualProtect(owned, 4096, PAGE_EXECUTE_READ, &previous) &&
                    FlushInstructionCache(GetCurrentProcess(), owned,
                                          code_bytes.size()),
                "seal owned trampoline fixture");
        void* recorded_parent = nullptr;
        const auto detour =
            TdurExerciseOwnedRendererDetour(owned, &recorded_parent, &parent);
        Require(detour == 0 && recorded_parent == &parent && depth == 0 &&
                    enters == 7 && leaves == 7,
                "real Detours trampoline/post-verification failed");
        MEMORY_BASIC_INFORMATION region{};
        Require(std::memcmp(owned, code_bytes.data(), code_bytes.size()) == 0 &&
                    VirtualQuery(owned, &region, sizeof(region)) ==
                        sizeof(region) &&
                    region.Protect == PAGE_EXECUTE_READ,
                "owned target code/protection was not restored after detach");
        VirtualFree(owned, 0, MEM_RELEASE);
        std::cout << "renderer initializer ABI: " << checks
                  << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once

// Shared crash channel between the observer DLL (inside the target),
// the owned launcher and the tests. All three are x86 builds. The launcher
// creates the section and the events before the target resumes; the mod only
// writes into this already mapped memory from the crash context.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace tdur::mod::crash {

inline constexpr char channel_magic[8] = {'T', 'D', 'U', 'R',
                                          'C', 'R', 'H', '1'};
inline constexpr std::uint32_t channel_layout = 1;
inline constexpr std::uint32_t channel_size = 128;
// S9: longest time the launcher gives one run of the dump helper.
inline constexpr DWORD helper_limit_ms = 20000;
// S8: bounded wait of the faulting thread inside the mod. A crash may arrive
// while a hang dump is running: one hang dump, then the crash dump, plus the
// launch of both helpers (review D9). Only matters if the launcher lives
// without answering; otherwise the done event ends the wait.
inline constexpr DWORD mod_wait_ms = 50000;
static_assert(mod_wait_ms >= 2 * helper_limit_ms + 5000);
// Exit code requested by the launcher when it stops a hung target.
inline constexpr DWORD hang_stop_exit_code = 0xE1480001UL;

enum class CrashState : LONG {
    idle = 0,
    claimed = 1,
    dumped = 2,
    dump_failed = 3,
    mod_timeout = 4,
    request_invalid = 5
};

struct alignas(8) Channel {
    char magic[8];                    //   0 launcher
    std::uint32_t size;               //   8 launcher: 128
    std::uint32_t layout;             //  12 launcher: 1
    std::uint32_t pid;                //  16 launcher
    std::uint32_t reserved0;          //  20
    std::uint64_t creation_filetime;  //  24 launcher
    volatile LONG mod_state;          //  32 mod: 1 = armed
    volatile LONG crash_state;        //  36 CrashState
    volatile LONG present_beats;      //  40 mod (present_heartbeat.hpp)
    volatile LONG crash_entries;      //  44 genuine last-chance calls
    volatile LONG image_calls;        //  48 calls from the main image
    std::uint32_t thread_id;          //  52
    std::uint32_t exception_pointers; //  56 address inside the target
    std::uint32_t exception_code;     //  60
    std::uint32_t exception_flags;    //  64
    std::uint32_t exception_address;  //  68
    std::uint32_t exception_info[2];  //  72 [0] access kind, [1] target
    std::uint32_t eip;                //  80
    std::uint32_t esp;                //  84
    std::uint32_t ebp;                //  88
    std::uint32_t caller;             //  92 return address of the detour
    std::uint32_t real_filter_result; //  96 valid once real_filter_done
    std::uint32_t image_last_code;    // 100
    std::uint64_t claimed_tick;       // 104 GetTickCount64 of the mod
    volatile LONG real_filter_done;   // 112 1 once real_filter_result is set
    std::uint32_t exception_params;   // 116 NumberParameters
    // Code of the latest genuine entry, whichever thread made it. The fields
    // from thread_id to real_filter_result belong to the claiming thread only
    // (review D8).
    std::uint32_t last_entry_code; // 120
    std::uint32_t reserved;        // 124
};
static_assert(sizeof(Channel) == channel_size);
static_assert(offsetof(Channel, magic) == 0);
static_assert(offsetof(Channel, size) == 8);
static_assert(offsetof(Channel, layout) == 12);
static_assert(offsetof(Channel, pid) == 16);
static_assert(offsetof(Channel, creation_filetime) == 24);
static_assert(offsetof(Channel, mod_state) == 32);
static_assert(offsetof(Channel, crash_state) == 36);
static_assert(offsetof(Channel, present_beats) == 40);
static_assert(offsetof(Channel, crash_entries) == 44);
static_assert(offsetof(Channel, image_calls) == 48);
static_assert(offsetof(Channel, thread_id) == 52);
static_assert(offsetof(Channel, exception_pointers) == 56);
static_assert(offsetof(Channel, exception_code) == 60);
static_assert(offsetof(Channel, exception_flags) == 64);
static_assert(offsetof(Channel, exception_address) == 68);
static_assert(offsetof(Channel, exception_info) == 72);
static_assert(offsetof(Channel, eip) == 80);
static_assert(offsetof(Channel, esp) == 84);
static_assert(offsetof(Channel, ebp) == 88);
static_assert(offsetof(Channel, caller) == 92);
static_assert(offsetof(Channel, real_filter_result) == 96);
static_assert(offsetof(Channel, image_last_code) == 100);
static_assert(offsetof(Channel, claimed_tick) == 104);
static_assert(offsetof(Channel, real_filter_done) == 112);
static_assert(offsetof(Channel, exception_params) == 116);
static_assert(offsetof(Channel, last_entry_code) == 120);

namespace detail {
inline bool append(std::span<wchar_t> out, std::size_t& used,
                   std::wstring_view text) noexcept {
    if (out.size() - used < text.size() + 1)
        return false;
    for (const wchar_t character : text)
        out[used++] = character;
    out[used] = L'\0';
    return true;
}

inline bool append_decimal(std::span<wchar_t> out, std::size_t& used,
                           std::uint64_t value) noexcept {
    wchar_t digits[20]{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value != 0);
    if (out.size() - used < count + 1)
        return false;
    while (count != 0)
        out[used++] = digits[--count];
    out[used] = L'\0';
    return true;
}
} // namespace detail

// "Local\TDUR-CRASH-<pid>-<creation>-<suffix>", suffix in {channel,
// request, done, image}. Returns false, and leaves an empty string when
// possible, if the buffer is too short. Never writes beyond out.
inline bool format_name(std::span<wchar_t> out, DWORD pid,
                        std::uint64_t creation,
                        std::wstring_view suffix) noexcept {
    if (out.empty())
        return false;
    out[0] = L'\0';
    std::size_t used = 0;
    const bool complete = detail::append(out, used, L"Local\\TDUR-CRASH-") &&
                          detail::append_decimal(out, used, pid) &&
                          detail::append(out, used, L"-") &&
                          detail::append_decimal(out, used, creation) &&
                          detail::append(out, used, L"-") &&
                          detail::append(out, used, suffix);
    if (!complete)
        out[0] = L'\0';
    return complete;
}

// Complete check of the header written by the launcher.
inline bool header_valid(const Channel& channel, DWORD pid,
                         std::uint64_t creation) noexcept {
    for (std::size_t index = 0; index < sizeof(channel_magic); ++index)
        if (channel.magic[index] != channel_magic[index])
            return false;
    return channel.size == channel_size && channel.layout == channel_layout &&
           channel.pid == pid && channel.creation_filetime == creation;
}

} // namespace tdur::mod::crash

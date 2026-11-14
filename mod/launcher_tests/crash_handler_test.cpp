// The real crash_handler.cpp armed inside this test process, with a
// fake launcher on a thread. One mode per process, since the detour stays for
// the process lifetime. Modes:
//   chain    genuine entries, claim, chaining, real filter result (D8),
//            calls from the main image, heartbeat.
//   absent   no TDUR_CRASH_CHANNEL: disabled, prologue intact.
//   invalid  TDUR_CRASH_CHANNEL=2: refused.
//   missing  variable set, no channel: refused, prologue intact.
//   foreign  channel prepared for another PID: refused, prologue intact.
// Only events written at installation are recorded; the detour logs nothing.
#include "tdur/mod/crash_channel.hpp"
#include "tdur/mod/crash_handler.hpp"
#include "tdur/mod/present_heartbeat.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace crash = tdur::mod::crash;

std::vector<std::string> records;
int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::printf("FAIL %s\n", message);
        ++failures;
    }
}

bool recorded(std::string_view event, std::string_view reason = {}) {
    for (const auto& line : records)
        if (line.starts_with(event) &&
            (reason.empty() || line.find(reason) != std::string::npos))
            return true;
    return false;
}

std::uint64_t creation_time() {
    FILETIME creation{}, exit_time{}, kernel_time{}, user_time{};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel_time,
                    &user_time);
    return (std::uint64_t(creation.dwHighDateTime) << 32) |
           creation.dwLowDateTime;
}

struct LauncherSide {
    HANDLE mapping = nullptr;
    crash::Channel* channel = nullptr;
    HANDLE request = nullptr;
    HANDLE done = nullptr;
    HANDLE image = nullptr;
};

// Same objects as the launcher creates before the target resumes.
LauncherSide prepare(DWORD header_pid) {
    LauncherSide side;
    const DWORD pid = GetCurrentProcessId();
    const auto creation = creation_time();
    std::array<wchar_t, 96> name{};
    crash::format_name(name, pid, creation, L"channel");
    side.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                      PAGE_READWRITE, 0, 4096, name.data());
    side.channel = static_cast<crash::Channel*>(
        MapViewOfFile(side.mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096));
    std::memcpy(side.channel->magic, crash::channel_magic,
                sizeof(side.channel->magic));
    side.channel->size = crash::channel_size;
    side.channel->layout = crash::channel_layout;
    side.channel->pid = header_pid;
    side.channel->creation_filetime = creation;
    crash::format_name(name, pid, creation, L"request");
    side.request = CreateEventW(nullptr, FALSE, FALSE, name.data());
    crash::format_name(name, pid, creation, L"done");
    side.done = CreateEventW(nullptr, TRUE, FALSE, name.data());
    crash::format_name(name, pid, creation, L"image");
    side.image = CreateEventW(nullptr, FALSE, FALSE, name.data());
    return side;
}

std::array<unsigned char, 16> prologue() {
    std::array<unsigned char, 16> bytes{};
    const auto module = GetModuleHandleW(L"kernelbase.dll");
    const auto target = reinterpret_cast<const unsigned char*>(
        GetProcAddress(module, "UnhandledExceptionFilter"));
    std::memcpy(bytes.data(), target, bytes.size());
    return bytes;
}

bool in_main_image(std::uintptr_t address) {
    const auto base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return address >= base && address < base + nt->OptionalHeader.SizeOfImage;
}

// Top-level filter of this process, called by the real last-chance filter.
std::atomic<LONG> top_calls{0};
std::atomic<LONG> top_seen_state{-1};
crash::Channel* observed = nullptr;

LONG WINAPI TopFilter(EXCEPTION_POINTERS*) {
    ++top_calls;
    top_seen_state = observed->crash_state;
    // RaiseException with flags 0 is continuable: the raising thread goes on.
    return EXCEPTION_CONTINUE_EXECUTION;
}

std::atomic<DWORD> launcher_saw_thread{0};
std::atomic<DWORD> launcher_saw_code{0};
std::atomic<LONG> launcher_requests{0};

DWORD WINAPI FakeLauncher(void* parameter) {
    auto* side = static_cast<LauncherSide*>(parameter);
    while (WaitForSingleObject(side->request, 10000) == WAIT_OBJECT_0) {
        ++launcher_requests;
        launcher_saw_thread = side->channel->thread_id;
        launcher_saw_code = side->channel->exception_code;
        if (side->channel->crash_state ==
            static_cast<LONG>(crash::CrashState::claimed))
            InterlockedExchange(&side->channel->crash_state,
                                static_cast<LONG>(crash::CrashState::dumped));
        SetEvent(side->done);
    }
    return 0;
}

struct Raise {
    DWORD code;
    ULONG_PTR access;
    ULONG_PTR target;
    std::atomic<bool> continued{false};
};

DWORD WINAPI Raiser(void* parameter) {
    auto* raise = static_cast<Raise*>(parameter);
    const ULONG_PTR values[2]{raise->access, raise->target};
    // No handler on this thread: the thread start filter (outside the main
    // image) calls the last-chance filter, as for a real crash.
    RaiseException(raise->code, 0, 2, values);
    raise->continued = true;
    return 0;
}

DWORD run_raiser(Raise& raise) {
    DWORD tid = 0;
    const HANDLE thread = CreateThread(nullptr, 0, Raiser, &raise, 0, &tid);
    WaitForSingleObject(thread, 20000);
    CloseHandle(thread);
    return tid;
}

LONG image_filter_result = 0;
int image_call_filter(EXCEPTION_POINTERS* info) {
    image_filter_result = UnhandledExceptionFilter(info);
    return image_filter_result == EXCEPTION_CONTINUE_EXECUTION
               ? EXCEPTION_CONTINUE_EXECUTION
               : EXCEPTION_EXECUTE_HANDLER;
}

void image_call(DWORD code) {
    __try {
        RaiseException(code, 0, 0, nullptr);
    } __except (image_call_filter(GetExceptionInformation())) {
    }
}

int chain() {
    auto side = prepare(GetCurrentProcessId());
    observed = side.channel;
    SetEnvironmentVariableW(L"TDUR_CRASH_CHANNEL", L"1");
    const auto before = prologue();
    check(TdurInstallCrashHandler(), "installation succeeds");
    check(recorded("crash-handler-armed"), "armed record");
    check(recorded("crash-handler-target"), "target record");
    check(side.channel->mod_state == 1, "mod_state armed");
    check(prologue() != before, "prologue detoured");
    tdur::mod::beat_present();
    check(side.channel->present_beats == 1, "heartbeat bound to channel");

    SetUnhandledExceptionFilter(TopFilter);
    const HANDLE launcher =
        CreateThread(nullptr, 0, FakeLauncher, &side, 0, nullptr);

    // First genuine crash: claimed, signalled, chained, result recorded.
    Raise first{0xE0481001UL, 8, 0x13DB27F0};
    const DWORD first_tid = run_raiser(first);
    check(first.continued, "first raiser continued (result chained)");
    check(top_calls == 1, "real filter called once");
    check(top_seen_state == static_cast<LONG>(crash::CrashState::dumped),
          "real filter runs after the launcher answered");
    check(launcher_requests == 1, "one request");
    check(launcher_saw_thread == first_tid, "request names the thread");
    check(launcher_saw_code == 0xE0481001UL, "request names the code");
    auto* ch = side.channel;
    check(ch->crash_entries == 1, "one genuine entry");
    check(ch->thread_id == first_tid, "claimer thread");
    check(ch->exception_code == 0xE0481001UL, "claimer code");
    check(ch->exception_params == 2, "parameter count");
    check(ch->exception_info[0] == 8 && ch->exception_info[1] == 0x13DB27F0,
          "access kind and target");
    check(ch->exception_pointers != 0 && ch->caller != 0,
          "pointers and caller");
    check(!in_main_image(ch->caller), "genuine caller outside the image");
    check(ch->real_filter_done == 1, "real filter result published");
    check(ch->real_filter_result ==
              static_cast<std::uint32_t>(EXCEPTION_CONTINUE_EXECUTION),
          "real filter result unchanged");
    check(ch->last_entry_code == 0xE0481001UL, "last entry code");

    // Review D8: a later genuine entry must not overwrite the claimer's
    // fields nor the published result.
    ch->real_filter_done = 0;
    ch->real_filter_result = 0x5A5A5A5AUL;
    Raise second{0xE0481002UL, 1, 0x1E4D92D0};
    run_raiser(second);
    check(second.continued, "second raiser continued");
    check(top_calls == 2, "real filter called for the second entry");
    check(ch->crash_entries == 2, "two genuine entries");
    check(ch->last_entry_code == 0xE0481002UL, "second code kept apart");
    check(ch->thread_id == first_tid && ch->exception_code == 0xE0481001UL,
          "claimer fields kept");
    check(ch->real_filter_done == 0 && ch->real_filter_result == 0x5A5A5A5AUL,
          "second entry did not overwrite the claimer result (D8)");
    check(launcher_requests == 1, "no second request");

    // A deliberate call from the main image: counted, never claimed.
    ResetEvent(side.image);
    image_call(0xE0481003UL);
    check(image_filter_result == EXCEPTION_CONTINUE_EXECUTION,
          "image call result unchanged");
    check(ch->image_calls == 1, "image call counted");
    check(ch->image_last_code == 0xE0481003UL, "image call code");
    check(WaitForSingleObject(side.image, 0) == WAIT_OBJECT_0,
          "image event signalled");
    check(ch->crash_entries == 2, "image call is not a genuine entry");
    check(top_calls == 3, "image call chained");

    // The fake launcher ends with the process.
    CloseHandle(launcher);
    return 0;
}

int refused(const wchar_t* variable, bool channel, DWORD header_pid,
            bool expected, std::string_view event, std::string_view reason) {
    LauncherSide side;
    if (channel)
        side = prepare(header_pid);
    if (variable != nullptr)
        SetEnvironmentVariableW(L"TDUR_CRASH_CHANNEL", variable);
    else
        SetEnvironmentVariableW(L"TDUR_CRASH_CHANNEL", nullptr);
    const auto before = prologue();
    check(TdurInstallCrashHandler() == expected, "installation result");
    check(recorded(event, reason), "expected record");
    check(prologue() == before, "prologue intact");
    check(!recorded("crash-handler-armed"), "never armed");
    if (side.channel != nullptr)
        check(side.channel->mod_state == 0, "mod_state untouched");
    return 0;
}
} // namespace

// Records only what installation writes; the detour never calls this.
void TdurObservationRecord(const char* event, DWORD error, const wchar_t*,
                           const char* reason, const void*) noexcept {
    try {
        records.push_back(std::string(event) +
                          " error=" + std::to_string(error) +
                          " reason=" + (reason != nullptr ? reason : ""));
    } catch (...) {
    }
}

int main(int argc, char* argv[]) {
    // No error box and no WER report from this test process.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode == "chain")
        chain();
    else if (mode == "absent")
        refused(nullptr, false, 0, true, "crash-handler-disabled",
                "no-channel");
    else if (mode == "invalid")
        refused(L"2", false, 0, false, "crash-handler-install-failed",
                "invalid-variable");
    else if (mode == "missing")
        refused(L"1", false, 0, false, "crash-handler-install-failed",
                "channel-missing");
    else if (mode == "foreign")
        refused(L"1", true, GetCurrentProcessId() + 4, false,
                "crash-handler-install-failed", "channel-header");
    else {
        std::printf("usage: chain|absent|invalid|missing|foreign\n");
        return 2;
    }
    for (const auto& line : records)
        std::printf("record %s\n", line.c_str());
    if (failures != 0) {
        std::printf("%d failure(s) in %.*s\n", failures,
                    static_cast<int>(mode.size()), mode.data());
        return 1;
    }
    std::printf("PASS crash handler %.*s\n", static_cast<int>(mode.size()),
                mode.data());
    return 0;
}

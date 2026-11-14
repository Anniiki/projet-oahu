// TH2: layout of the crash channel, bounded name formatting, header
// check and Present heartbeat. Pure checks, no process and no file.
#include "tdur/mod/crash_channel.hpp"
#include "tdur/mod/present_heartbeat.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
namespace crash = tdur::mod::crash;

int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::printf("FAIL %s\n", message);
        ++failures;
    }
}

crash::Channel valid_channel(DWORD pid, std::uint64_t creation) {
    crash::Channel channel{};
    std::memcpy(channel.magic, crash::channel_magic, sizeof(channel.magic));
    channel.size = crash::channel_size;
    channel.layout = crash::channel_layout;
    channel.pid = pid;
    channel.creation_filetime = creation;
    return channel;
}

void names() {
    constexpr std::wstring_view expected =
        L"Local\\TDUR-CRASH-1234-133000000000000000-request";
    std::array<wchar_t, 64> wide{};
    wide.fill(L'#');
    check(crash::format_name(wide, 1234, 133000000000000000ULL, L"request"),
          "name fits a large buffer");
    check(std::wstring_view(wide.data()) == expected, "exact name");

    // Exactly large enough: text plus terminator. One wchar_t guard after.
    std::array<wchar_t, expected.size() + 2> exact{};
    exact.fill(L'#');
    check(crash::format_name({exact.data(), expected.size() + 1}, 1234,
                             133000000000000000ULL, L"request"),
          "name fits an exact buffer");
    check(std::wstring_view(exact.data()) == expected, "exact buffer text");
    check(exact[expected.size() + 1] == L'#', "no write after exact buffer");

    // One character short: refused, empty string, guard intact.
    std::array<wchar_t, expected.size() + 1> short_buffer{};
    short_buffer.fill(L'#');
    check(!crash::format_name({short_buffer.data(), expected.size()}, 1234,
                              133000000000000000ULL, L"request"),
          "short buffer refused");
    check(short_buffer[0] == L'\0', "short buffer left empty");
    check(short_buffer[expected.size()] == L'#', "no write after short buffer");
    check(!crash::format_name({}, 1, 1, L"channel"), "empty span refused");

    std::array<wchar_t, 64> zero{};
    check(crash::format_name(zero, 0, 0, L"channel") &&
              std::wstring_view(zero.data()) ==
                  L"Local\\TDUR-CRASH-0-0-channel",
          "zero values formatted");
}

void headers() {
    const DWORD pid = 4321;
    const std::uint64_t creation = 133111111111111111ULL;
    auto channel = valid_channel(pid, creation);
    check(crash::header_valid(channel, pid, creation), "valid header");
    check(!crash::header_valid(channel, pid + 1, creation), "wrong pid");
    check(!crash::header_valid(channel, pid, creation + 1), "wrong creation");
    channel.magic[7] = '2';
    check(!crash::header_valid(channel, pid, creation), "wrong magic");
    channel = valid_channel(pid, creation);
    channel.size = 64;
    check(!crash::header_valid(channel, pid, creation), "wrong size");
    channel = valid_channel(pid, creation);
    channel.layout = 2;
    check(!crash::header_valid(channel, pid, creation), "wrong layout");
}

void heartbeat() {
    tdur::mod::present_heartbeat.store(nullptr);
    tdur::mod::beat_present(); // Must not crash without a channel.
    crash::Channel channel{};
    tdur::mod::present_heartbeat.store(&channel.present_beats);
    tdur::mod::beat_present();
    tdur::mod::beat_present();
    check(channel.present_beats == 2, "two beats counted");
    tdur::mod::present_heartbeat.store(nullptr);
    tdur::mod::beat_present();
    check(channel.present_beats == 2, "no beat after reset");
}

void thresholds() {
    // Review D9: the faulting thread must outlive a hang dump already running
    // followed by the crash dump.
    check(crash::mod_wait_ms >= 2 * crash::helper_limit_ms + 5000,
          "mod wait covers two helper runs");
    check(crash::hang_stop_exit_code == 0xE1480001UL, "hang stop exit code");
    check(offsetof(crash::Channel, last_entry_code) == 120,
          "last entry code offset");
}
} // namespace

int main() {
    names();
    headers();
    heartbeat();
    thresholds();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("PASS crash channel layout, names, header, heartbeat\n");
    return 0;
}

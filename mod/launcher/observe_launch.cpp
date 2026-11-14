// Diagnostic launcher for one private x86 copy, not a game implementation.
// Detours changes the child's in-memory imports before its entry point. The
// original executable on disk and all activation/protection APIs are untouched.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
// clang-format on
#include <detours.h>

#include "tdur/game/installation.hpp"
#include "tdur/mod/crash_channel.hpp"
#include "watchdog_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;

class Handle {
  public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept
        : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    void reset() noexcept {
        if (*this) {
            CloseHandle(value_);
        }
        value_ = nullptr;
    }

  private:
    HANDLE value_;
};

[[noreturn]] void win_error(std::string_view action) {
    const auto error = GetLastError();
    throw std::runtime_error(std::string(action) + ": Win32 " +
                             std::to_string(error));
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto size = static_cast<int>(text.size());
    const int required =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
                            nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        win_error("UTF-8 conversion");
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
                            result.data(), required, nullptr, nullptr) == 0) {
        win_error("UTF-8 conversion");
    }
    return result;
}

std::string json(std::string_view text) {
    std::ostringstream stream;
    stream << '"';
    for (const unsigned char value : text) {
        switch (value) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (value < 32) {
                stream << "\\u00" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<unsigned int>(value)
                       << std::dec;
            } else {
                stream << static_cast<char>(value);
            }
        }
    }
    stream << '"';
    return stream.str();
}

void write_new_file(const fs::path& path, const std::string& content) {
    Handle output(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                              nullptr));
    if (!output) {
        win_error("Create diagnostic output");
    }
    DWORD written = 0;
    if (!WriteFile(output.get(), content.data(),
                   static_cast<DWORD>(content.size()), &written, nullptr) ||
        written != content.size() || !FlushFileBuffers(output.get())) {
        win_error("Write diagnostic output");
    }
}

std::uint64_t filetime_value(FILETIME value) noexcept {
    return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

// Job notifications are supplementary observations, not an authoritative
// process-liveness test. Windows does not guarantee delivery of these messages.
// The owned process handle remains open through every exit-code query.
class JobNotifications {
  public:
    JobNotifications()
        : port_(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1)) {
        if (!port_)
            win_error("Create private job completion port");
    }
    JobNotifications(const JobNotifications&) = delete;
    JobNotifications& operator=(const JobNotifications&) = delete;
    JobNotifications(JobNotifications&&) = delete;
    JobNotifications& operator=(JobNotifications&&) = delete;
    void associate(HANDLE job) {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT information{this, port_.get()};
        if (!SetInformationJobObject(
                job, JobObjectAssociateCompletionPortInformation, &information,
                sizeof(information)))
            win_error("Associate empty private job completion port");
    }
    void poll(HANDLE process, DWORD pid) noexcept {
        if (read_error_)
            return;
        // No wait and no unbounded drain, even under repeated process attempts.
        for (std::size_t attempt = 0; attempt < events_.size(); ++attempt) {
            DWORD message = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* value = nullptr;
            if (!GetQueuedCompletionStatus(port_.get(), &message, &key, &value,
                                           0)) {
                const auto error = GetLastError();
                if (error != WAIT_TIMEOUT)
                    read_error_ = error;
                return;
            }
            if (key != reinterpret_cast<ULONG_PTR>(this)) {
                read_error_ = ERROR_INVALID_DATA;
                return;
            }
            Event event{};
            event.message = message;
            event.pid = reinterpret_cast<ULONG_PTR>(value);
            FILETIME now{};
            GetSystemTimePreciseAsFileTime(&now);
            event.observed_filetime = filetime_value(now);
            event.observed_tick = GetTickCount64();
            if (event.pid == pid &&
                (message == JOB_OBJECT_MSG_EXIT_PROCESS ||
                 message == JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS) &&
                WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                event.exit_code_known =
                    GetExitCodeProcess(process, &event.exit_code) != FALSE;
                if (!event.exit_code_known)
                    event.query_error = GetLastError();
            }
            if (count_ < events_.size())
                events_[count_++] = event;
            else
                ++dropped_;
        }
    }
    void write_json(std::ostream& output) const {
        output << "{\"method\":\"completion-port\","
                  "\"delivery_guaranteed\":false,\"dropped\":"
               << dropped_ << ",\"read_error\":" << read_error_
               << ",\"events\":[";
        for (std::size_t index = 0; index < count_; ++index) {
            const auto& event = events_[index];
            output << (index ? "," : "") << "{\"message\":" << event.message
                   << ",\"kind\":\"" << name(event.message)
                   << "\",\"pid\":" << event.pid
                   << ",\"observed_utc_filetime\":\"" << event.observed_filetime
                   << "\",\"observed_tick_ms\":" << event.observed_tick
                   << ",\"exit_code\":";
            if (event.exit_code_known)
                output << event.exit_code;
            else
                output << "null";
            output << ",\"query_error\":" << event.query_error << '}';
        }
        output << "]}";
    }

  private:
    static std::string_view name(DWORD message) noexcept {
        switch (message) {
        case JOB_OBJECT_MSG_NEW_PROCESS:
            return "new_process";
        case JOB_OBJECT_MSG_EXIT_PROCESS:
            return "exit_process";
        case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:
            return "abnormal_exit_process";
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
            return "active_process_zero";
        case JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT:
            return "active_process_limit";
        default:
            return "other";
        }
    }
    struct Event {
        DWORD message{}, exit_code{}, query_error{};
        ULONG_PTR pid{};
        std::uint64_t observed_filetime{}, observed_tick{};
        bool exit_code_known{};
    };
    Handle port_;
    std::array<Event, 64> events_{};
    std::size_t count_{}, dropped_{};
    DWORD read_error_{};
};

struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    ~Algorithm() {
        if (value != nullptr) {
            BCryptCloseAlgorithmProvider(value, 0);
        }
    }
};
struct Hash {
    BCRYPT_HASH_HANDLE value = nullptr;
    ~Hash() {
        if (value != nullptr) {
            BCryptDestroyHash(value);
        }
    }
};

std::string sha256(HANDLE file) {
    Algorithm algorithm;
    if (BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0) {
        throw std::runtime_error("Cannot open CNG SHA-256 provider");
    }
    DWORD size = 0;
    DWORD returned = 0;
    if (BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&size), sizeof(size),
                          &returned, 0) < 0 ||
        returned != sizeof(size)) {
        throw std::runtime_error("Cannot query CNG SHA-256 object size");
    }
    std::vector<UCHAR> object(size);
    Hash hash;
    if (BCryptCreateHash(algorithm.value, &hash.value, object.data(), size,
                         nullptr, 0, 0) < 0) {
        throw std::runtime_error("Cannot create CNG SHA-256 state");
    }
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        win_error("Seek locked save");
    }
    std::array<UCHAR, 65536> buffer{};
    while (true) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &read, nullptr)) {
            win_error("Read locked save");
        }
        if (read == 0) {
            break;
        }
        if (BCryptHashData(hash.value, buffer.data(), read, 0) < 0) {
            throw std::runtime_error("Cannot hash locked save bytes");
        }
    }
    std::array<UCHAR, 32> digest{};
    if (BCryptFinishHash(hash.value, digest.data(),
                         static_cast<ULONG>(digest.size()), 0) < 0) {
        throw std::runtime_error("Cannot finish SHA-256");
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : digest) {
        stream << std::setw(2) << static_cast<unsigned int>(value);
    }
    return stream.str();
}

using Manifest = std::map<std::wstring, std::string>;
struct LockedSave {
    std::wstring relative;
    Handle file;
    std::string digest;
};

std::vector<fs::path> save_files(const fs::path& root) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::runtime_error(
                "A save entry is unreadable or a reparse point");
        }
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<LockedSave> lock_saves(const fs::path& root) {
    std::vector<LockedSave> result;
    for (const auto& path : save_files(root)) {
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (!file) {
            win_error("Lock original save against writes and deletion");
        }
        const auto digest = sha256(file.get());
        result.push_back(
            {path.lexically_relative(root).wstring(), std::move(file), digest});
    }
    return result;
}

bool same_saves(const fs::path& root, const std::vector<LockedSave>& locked) {
    const auto files = save_files(root);
    if (files.size() != locked.size()) {
        return false;
    }
    for (std::size_t index = 0; index < locked.size(); ++index) {
        if (files[index].lexically_relative(root).wstring() !=
                locked[index].relative ||
            sha256(locked[index].file.get()) != locked[index].digest) {
            return false;
        }
    }
    return true;
}

bool inside(const fs::path& child, const fs::path& parent) {
    auto part = child.begin();
    for (const auto& required : parent) {
        if (part == child.end() ||
            _wcsicmp(part->c_str(), required.c_str()) != 0) {
            return false;
        }
        ++part;
    }
    return true;
}

struct Options {
    fs::path executable;
    fs::path dll;
    fs::path private_documents;
    fs::path source_documents;
    fs::path source_saves;
    fs::path log;
    fs::path report;
    fs::path pid_file;
    fs::path resume_file;
    unsigned long seconds = 30;
    fs::path system_config;
    bool play_session = false;
    // Crash and hang diagnostics.
    fs::path helper;
    fs::path dump_dir;
    tdur::launcher::watchdog::Policy hang_action =
        tdur::launcher::watchdog::Policy::dump_only;
    bool notify_dialog = false;
    bool test_thresholds = false;
    tdur::launcher::watchdog::Thresholds thresholds{};
};

fs::path helper_path() {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        win_error("Locate the launcher executable");
    return fs::path(path.data()).parent_path() / L"tdur_dump_helper.exe";
}

// Hang options. Review D6: production floors are S4 (question,
// 60 s), S5 (stop, 120 s) and S6 (beat, 120 s); lower values need
// --hang-test-thresholds 1, which a normal session never passes and the
// report records. The automatic stop is for sessions without a player.
void hang_options(const std::map<std::wstring, std::wstring>& values,
                  Options& result) {
    namespace watchdog = tdur::launcher::watchdog;
    const auto seconds = [&](const wchar_t* name, unsigned long low,
                             unsigned long high, std::uint32_t& out) {
        if (!values.contains(name))
            return;
        std::size_t consumed = 0;
        const auto& text = values.at(name);
        const auto value = std::stoul(text, &consumed);
        if (consumed != text.size() || value < low || value > high)
            throw std::runtime_error("Hang threshold out of range");
        out = static_cast<std::uint32_t>(value * 1000);
    };
    if (values.contains(L"--hang-test-thresholds")) {
        if (values.at(L"--hang-test-thresholds") != L"1")
            throw std::runtime_error("Test thresholds flag must be 1");
        result.test_thresholds = true;
    }
    const bool test = result.test_thresholds;
    auto& limits = result.thresholds;
    seconds(L"--hang-dump-after", 6, 600, limits.dump_after_ms);
    seconds(L"--hang-ask-after", test ? 7 : 60, 1800, limits.ask_after_ms);
    seconds(L"--hang-stop-after", test ? 7 : 120, 3600, limits.stop_after_ms);
    seconds(L"--beat-dump-after", test ? 7 : 120, 3600,
            limits.beat_dump_after_ms);
    if (limits.ask_after_ms <= limits.dump_after_ms ||
        limits.stop_after_ms <= limits.dump_after_ms)
        throw std::runtime_error("Question and stop must follow the dump");
    if (values.contains(L"--hang-action")) {
        const auto& action = values.at(L"--hang-action");
        if (action == L"ask")
            result.hang_action = watchdog::Policy::ask;
        else if (action == L"stop")
            result.hang_action = watchdog::Policy::stop;
        else if (action != L"dump-only")
            throw std::runtime_error("Hang action must be dump-only|ask|stop");
    }
    if (values.contains(L"--notify")) {
        const auto& notify = values.at(L"--notify");
        if (notify != L"console" && notify != L"dialog")
            throw std::runtime_error("Notify must be console or dialog");
        result.notify_dialog = notify == L"dialog";
    }
    if (result.hang_action == watchdog::Policy::ask && !result.notify_dialog)
        throw std::runtime_error("A question needs --notify dialog");
    if (result.hang_action == watchdog::Policy::stop && result.play_session)
        throw std::runtime_error(
            "Automatic hang stop is for sessions without a player");
}

Options options(int argc, wchar_t* argv[]) {
    std::map<std::wstring, std::wstring> values;
    const std::set<std::wstring> allowed{L"--exe",
                                         L"--dll",
                                         L"--private-documents",
                                         L"--source-documents",
                                         L"--source-saves",
                                         L"--log",
                                         L"--report",
                                         L"--pid-file",
                                         L"--resume-file",
                                         L"--max-seconds",
                                         L"--system-config",
                                         L"--session-mode",
                                         L"--hang-action",
                                         L"--notify",
                                         L"--hang-dump-after",
                                         L"--hang-ask-after",
                                         L"--hang-stop-after",
                                         L"--beat-dump-after",
                                         L"--hang-test-thresholds"};
    if (argc < 21 || argc % 2 == 0) {
        throw std::runtime_error(
            "Expected ten options, then optional ones once each");
    }
    for (int index = 1; index + 1 < argc; index += 2) {
        if (!allowed.contains(argv[index]) ||
            !values.emplace(argv[index], argv[index + 1]).second) {
            throw std::runtime_error("Unknown or repeated launcher option");
        }
    }
    for (const auto* required :
         {L"--exe", L"--dll", L"--private-documents", L"--source-documents",
          L"--source-saves", L"--log", L"--report", L"--pid-file",
          L"--resume-file", L"--max-seconds"})
        if (!values.contains(required))
            throw std::runtime_error("Missing required launcher option");
    const auto path = [&](const wchar_t* name) {
        fs::path value(values.at(name));
        if (!value.is_absolute()) {
            throw std::runtime_error("All diagnostic paths must be absolute");
        }
        return fs::weakly_canonical(value);
    };
    Options result{path(L"--exe"),
                   path(L"--dll"),
                   path(L"--private-documents"),
                   path(L"--source-documents"),
                   path(L"--source-saves"),
                   path(L"--log"),
                   path(L"--report"),
                   path(L"--pid-file"),
                   path(L"--resume-file")};
    if (values.contains(L"--session-mode")) {
        const auto& mode = values.at(L"--session-mode");
        if (mode != L"diagnostic" && mode != L"play")
            throw std::runtime_error("Session mode must be diagnostic or play");
        result.play_session = mode == L"play";
    }
    if (values.contains(L"--system-config")) {
        result.system_config = path(L"--system-config");
        if (!inside(result.system_config, result.private_documents) ||
            !fs::is_regular_file(result.system_config) ||
            fs::file_size(result.system_config) > 65536 ||
            result.system_config.wstring().size() > 254)
            throw std::runtime_error(
                "System INI must be a bounded private file");
        for (const auto character : result.system_config.wstring())
            if (character < 32 || character > 126 || character == L'"')
                throw std::runtime_error(
                    "Retail INI path must be quoted ASCII");
    }
    hang_options(values, result);
    std::size_t consumed = 0;
    result.seconds = std::stoul(values.at(L"--max-seconds"), &consumed);
    if (consumed != values.at(L"--max-seconds").size() || result.seconds == 0 ||
        result.seconds > 120) {
        throw std::runtime_error("Diagnostic deadline must be 1..120 seconds");
    }
    if (!fs::is_regular_file(result.executable) ||
        !fs::is_regular_file(result.dll) ||
        !fs::is_directory(result.source_saves) ||
        !fs::is_directory(result.private_documents) ||
        !fs::is_directory(result.source_documents) ||
        !inside(result.source_saves, result.source_documents) ||
        inside(result.private_documents, result.source_documents) ||
        inside(result.source_documents, result.private_documents) ||
        inside(result.executable, result.source_documents) ||
        !inside(result.log, result.private_documents)) {
        throw std::runtime_error("Unsafe or unavailable diagnostic paths");
    }
    for (const auto& output :
         {result.report, result.pid_file, result.resume_file, result.log}) {
        if (fs::exists(output)) {
            throw std::runtime_error("Diagnostic output already exists");
        }
    }
    // Review D7: dumps go to the session folder only, the folder
    // of the private documents or its parent, checked before the launch.
    result.dump_dir = result.report.parent_path();
    if (!fs::is_directory(result.dump_dir) ||
        inside(result.dump_dir, result.source_documents) ||
        (_wcsicmp(result.dump_dir.c_str(), result.private_documents.c_str()) !=
             0 &&
         _wcsicmp(result.dump_dir.c_str(),
                  result.private_documents.parent_path().c_str()) != 0))
        throw std::runtime_error(
            "Dump folder must be the private session folder");
    result.helper = helper_path();
    if (!fs::is_regular_file(result.helper))
        throw std::runtime_error(
            "Dump helper missing next to the launcher; nothing launched");
    // Detours' import descriptor is a byte string; require an unambiguous
    // ASCII DLL path rather than silently using an ACP-corrupted filename.
    for (const auto character : result.dll.wstring()) {
        if (character > 0x7f) {
            throw std::runtime_error("Detours DLL path must be ASCII");
        }
    }
    return result;
}

// Crash and hang dumps of the owned child. Rule of the safety
// review (D1): no failure of this diagnostic may leave launch() by an
// exception, since closing the job would kill the game. Every step below
// is noexcept or catches locally, and records its error in the report.
namespace crash = tdur::mod::crash;
namespace watchdog = tdur::launcher::watchdog;

constexpr DWORD helper_self_check_ms = 10000; // S10
constexpr DWORD watchdog_poll_ms = 1000;      // S1
constexpr std::size_t helper_summary_limit = 65536;

std::uint64_t unbiased_ms() noexcept {
    ULONGLONG value = 0;
    QueryUnbiasedInterruptTime(&value);
    return value / 10000;
}

std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "\"0x" << std::hex << value << '"';
    return stream.str();
}

// Accepts a bounded JSON object written by the helper: balanced braces and
// brackets outside strings, valid string escapes, no raw control character.
bool json_object_shape(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.remove_suffix(1);
    if (text.size() < 2 || text.front() != '{' || text.back() != '}')
        return false;
    int depth = 0;
    bool in_string = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto value = static_cast<unsigned char>(text[index]);
        if (value < 32)
            return false;
        if (in_string) {
            if (value == '\\') {
                if (++index >= text.size() ||
                    std::string_view("\"\\/bfnrtu").find(text[index]) ==
                        std::string_view::npos)
                    return false;
            } else if (value == '"') {
                in_string = false;
            }
            continue;
        }
        if (value == '"')
            in_string = true;
        else if (value == '{' || value == '[')
            ++depth;
        else if ((value == '}' || value == ']') && --depth < 0)
            return false;
        if (depth == 0 && index + 1 != text.size())
            return false;
    }
    return depth == 0 && !in_string;
}

struct HelperRun {
    std::string file;
    int exit_code = -1;
    bool started = false;
    bool timed_out = false;
    DWORD error = 0;
    std::string error_step;
    std::uint64_t ms = 0;
    std::uint64_t bytes = 0;
    std::string sha256;
    std::string summary = "null";

    void write_json(std::ostream& output) const {
        output << "{\"file\":" << json(file) << ",\"helper_exit\":" << exit_code
               << ",\"started\":" << (started ? "true" : "false")
               << ",\"timed_out\":" << (timed_out ? "true" : "false")
               << ",\"error\":" << error
               << ",\"error_step\":" << json(error_step)
               << ",\"helper_ms\":" << ms << ",\"bytes\":" << bytes
               << ",\"sha256\":" << json(sha256) << ",\"summary\":" << summary
               << '}';
    }
};

class CrashSession {
  public:
    CrashSession(const fs::path& helper, const fs::path& dump_dir,
                 watchdog::Thresholds thresholds, watchdog::Policy policy,
                 bool dialog, bool test_thresholds)
        : helper_(helper), dump_dir_(dump_dir), dog_(thresholds, policy),
          dialog_enabled_(dialog), test_thresholds_(test_thresholds) {}
    CrashSession(const CrashSession&) = delete;
    CrashSession& operator=(const CrashSession&) = delete;
    ~CrashSession() { close_dialog(); }

    // Before the game exists: a failure here refuses the launch.
    void self_check() {
        Handle job(CreateJobObjectW(nullptr, nullptr));
        if (!job)
            win_error("Create dump helper self-check job");
        limit_helper_job(job.get());
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE read_raw = nullptr, write_raw = nullptr;
        if (!CreatePipe(&read_raw, &write_raw, &inherit, 0))
            win_error("Create dump helper self-check pipe");
        Handle read_end(read_raw), write_end(write_raw);
        SetHandleInformation(read_end.get(), HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write_end.get();
        startup.hStdError = write_end.get();
        std::wstring command = L"\"" + helper_.wstring() + L"\" --self-check";
        PROCESS_INFORMATION created{};
        if (!CreateProcessW(helper_.c_str(), command.data(), nullptr, nullptr,
                            TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                            nullptr, &startup, &created))
            win_error("Start dump helper self-check");
        Handle process(created.hProcess), thread(created.hThread);
        if (!AssignProcessToJobObject(job.get(), process.get())) {
            TerminateProcess(process.get(), ERROR_CANCELLED);
            win_error("Confine dump helper self-check");
        }
        ResumeThread(thread.get());
        write_end.reset();
        const DWORD wait =
            WaitForSingleObject(process.get(), helper_self_check_ms);
        DWORD code = 0;
        if (wait != WAIT_OBJECT_0) {
            TerminateJobObject(job.get(), ERROR_TIMEOUT);
            throw std::runtime_error(
                "Dump helper self-check exceeded 10 seconds; nothing "
                "launched");
        }
        GetExitCodeProcess(process.get(), &code);
        std::string output;
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        while (output.size() < helper_summary_limit &&
               ReadFile(read_end.get(), buffer.data(),
                        static_cast<DWORD>(buffer.size()), &read, nullptr) &&
               read != 0)
            output.append(buffer.data(), read);
        if (code != 0 || !json_object_shape(output))
            throw std::runtime_error(
                "Dump helper self-check failed with code " +
                std::to_string(code) + "; nothing launched");
        while (!output.empty() &&
               (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        self_check_ = output;
        helper_sha256_ = file_sha256(helper_);
    }

    // Before resume, while the suspended child sits in its job.
    void create_channel(DWORD pid, std::uint64_t creation) {
        pid_ = pid;
        const auto name = [&](std::wstring_view suffix) {
            std::array<wchar_t, 96> text{};
            if (!crash::format_name(text, pid, creation, suffix))
                throw std::runtime_error("Crash channel name too long");
            return std::wstring(text.data());
        };
        mapping_ = Handle(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                             PAGE_READWRITE, 0, 4096,
                                             name(L"channel").c_str()));
        const DWORD mapping_error = GetLastError();
        if (!mapping_ || mapping_error == ERROR_ALREADY_EXISTS)
            throw std::runtime_error(
                "Crash channel already exists or cannot be created");
        channel_ = static_cast<crash::Channel*>(
            MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, 4096));
        if (channel_ == nullptr)
            win_error("Map crash channel");
        const auto event = [&](std::wstring_view suffix, BOOL manual) {
            Handle value(
                CreateEventW(nullptr, manual, FALSE, name(suffix).c_str()));
            if (!value || GetLastError() == ERROR_ALREADY_EXISTS)
                throw std::runtime_error(
                    "Crash channel event already exists or failed");
            return value;
        };
        request_ = event(L"request", FALSE);
        done_ = event(L"done", TRUE);
        image_ = event(L"image", FALSE);
        std::memcpy(channel_->magic, crash::channel_magic,
                    sizeof(channel_->magic));
        channel_->size = crash::channel_size;
        channel_->layout = crash::channel_layout;
        channel_->pid = pid;
        channel_->creation_filetime = creation;
        channel_->real_filter_result = 0xFFFFFFFFUL;
        MemoryBarrier();
        created_ = true;
    }

    void bind(HANDLE process, HANDLE job) noexcept {
        process_ = process;
        game_job_ = job;
    }

    // Handles for the launcher wait: process, crash request, image call.
    [[nodiscard]] HANDLE request_event() const noexcept {
        return request_.get();
    }
    [[nodiscard]] HANDLE image_event() const noexcept { return image_.get(); }

    void on_wait_failed(DWORD error) noexcept {
        record_error("wait-multiple", error);
        events_disabled_ = true;
    }
    [[nodiscard]] bool events_disabled() const noexcept {
        return events_disabled_;
    }

    // Index 1 of the wait: the mod claimed a crash and waits for "done".
    void on_crash_request() noexcept {
        try {
            handle_crash();
        } catch (...) {
            record_error("crash-request", ERROR_INVALID_FUNCTION);
        }
        // Whatever happened, the faulting thread must go on to the real
        // filter: never leave it waiting.
        if (done_)
            SetEvent(done_.get());
    }

    // Called on every loop turn; acts at most once per second.
    void tick(bool process_alive) noexcept {
        try {
            if (!process_alive || channel_ == nullptr)
                return;
            poll_dialog();
            if (crash_dumped_ && !process_end_seen_)
                crash_alive_ms_ = elapsed_ms(crash_dump_end_);
            const auto now = unbiased_ms();
            if (have_tick_ && now - last_tick_ < watchdog_poll_ms)
                return;
            have_tick_ = true;
            last_tick_ = now;
            sample(now);
        } catch (...) {
            record_error("watchdog", ERROR_INVALID_FUNCTION);
        }
    }

    void on_process_end() noexcept {
        process_end_seen_ = true;
        episode_open_at_exit_ = dog_.in_episode();
        close_dialog();
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_.has_value();
    }

    std::string end_reason(bool timed_out, DWORD exit_code) const {
        const bool armed = channel_ != nullptr && channel_->mod_state == 1;
        if (timed_out)
            return "deadline";
        if (stop_)
            return "hang-stopped";
        if (crash_claimed_) {
            if (channel_->real_filter_done == 1 &&
                channel_->real_filter_result ==
                    static_cast<std::uint32_t>(EXCEPTION_CONTINUE_EXECUTION))
                return "crash-recovered";
            return crash_run_ && crash_run_->exit_code == 0
                       ? "crash-dumped"
                       : "crash-not-dumped";
        }
        if (episode_open_at_exit_ || exit_code == 0xCFFFFFFFUL)
            return "closed-during-hang";
        if (exit_code == 0xC0000142UL && !armed)
            return "startup-failed";
        if (exit_code >= 0xC0000000UL)
            return "abnormal-exit-no-signal";
        return "exit";
    }

    void write_json(std::ostream& output, bool timed_out,
                    DWORD exit_code) const {
        const auto* ch = channel_;
        output << ",\n  \"helper\":{\"path\":" << json(utf8(helper_.wstring()))
               << ",\"sha256\":" << json(helper_sha256_) << ",\"self_check\":"
               << (self_check_.empty() ? "null" : self_check_) << '}';
        output << ",\n  \"crash_channel\":{\"created\":"
               << (created_ ? "true" : "false") << ",\"mod_armed\":"
               << (ch != nullptr && ch->mod_state == 1 ? "true" : "false")
               << ",\"present_beats\":" << (ch ? ch->present_beats : 0)
               << ",\"crash_entries\":" << (ch ? ch->crash_entries : 0)
               << ",\"image_calls\":" << (ch ? ch->image_calls : 0)
               << ",\"image_last_code\":"
               << (ch != nullptr && ch->image_calls > 0
                       ? hex32(ch->image_last_code)
                       : std::string("null"))
               << ",\"last_entry_code\":"
               << (ch != nullptr && ch->crash_entries > 0
                       ? hex32(ch->last_entry_code)
                       : std::string("null"))
               << '}';
        output << ",\n  \"crash\":";
        write_crash(output);
        const auto& limits = dog_.thresholds();
        output << ",\n  \"watchdog\":{\"poll_ms\":" << watchdog_poll_ms
               << ",\"policy\":" << json(policy_name(dog_.policy()))
               << ",\"notify\":" << json(dialog_enabled_ ? "dialog" : "console")
               << ",\"test_thresholds\":"
               << (test_thresholds_ ? "true" : "false")
               << ",\"thresholds_ms\":{\"dump\":" << limits.dump_after_ms
               << ",\"ask\":" << limits.ask_after_ms
               << ",\"stop\":" << limits.stop_after_ms
               << ",\"beat_dump\":" << limits.beat_dump_after_ms
               << ",\"max_step\":" << limits.max_step_ms
               << "},\"window_seen\":" << (window_seen_ ? "true" : "false")
               << ",\"window_messages_sent\":false"
               << ",\"samples\":" << samples_
               << ",\"longest_hung_ms\":" << dog_.longest_hung_ms()
               << ",\"beat_longest_stall_ms\":" << dog_.beat_longest_stall_ms()
               << ",\"deferred_iconic_ms\":" << dog_.deferred_iconic_ms()
               << ",\"answers_after_episode\":" << dog_.answers_after_episode()
               << ",\"episode_open_at_exit\":"
               << (episode_open_at_exit_ ? "true" : "false")
               << ",\"episodes\":[";
        bool first = true;
        const auto episode = [&](const watchdog::Episode& value) {
            output << (first ? "" : ",") << "{\"start_ms\":" << value.start_ms
                   << ",\"hung_ms\":" << value.hung_ms << ",\"dumps\":[";
            for (std::uint32_t index = 0;
                 index < std::min<std::uint32_t>(value.dump_count, 4); ++index)
                output << (index ? "," : "") << value.dumps[index];
            output << "],\"asked\":" << (value.asked ? "true" : "false")
                   << ",\"answer\":" << json(answer_name(value.answer))
                   << ",\"answer_late\":"
                   << (value.answer_late ? "true" : "false")
                   << ",\"stopped\":" << (value.stopped ? "true" : "false")
                   << '}';
            first = false;
        };
        for (const auto& value : dog_.episodes())
            episode(value);
        if (dog_.in_episode())
            episode(dog_.current());
        output << "],\"episodes_dropped\":" << dog_.episodes_dropped()
               << ",\"beat_dump\":" << dog_.beat_dump() << '}';
        output << ",\n  \"hang_dumps\":[";
        for (std::size_t index = 0; index < hang_runs_.size(); ++index) {
            output << (index ? "," : "");
            hang_runs_[index].write_json(output);
        }
        output << "]";
        output << ",\n  \"stop\":";
        if (stop_)
            output << *stop_;
        else
            output << "null";
        output << ",\n  \"diagnostic_errors\":[";
        for (std::size_t index = 0; index < errors_.size(); ++index)
            output << (index ? "," : "") << errors_[index];
        output << "]";
        output << ",\n  \"end_reason\":"
               << json(end_reason(timed_out, exit_code));
    }

  private:
    static std::string policy_name(watchdog::Policy value) {
        return value == watchdog::Policy::ask    ? "ask"
               : value == watchdog::Policy::stop ? "stop"
                                                 : "dump-only";
    }
    static std::string answer_name(watchdog::Answer value) {
        switch (value) {
        case watchdog::Answer::yes:
            return "yes";
        case watchdog::Answer::no:
            return "no";
        case watchdog::Answer::auto_closed:
            return "auto-closed";
        default:
            return "none";
        }
    }
    static std::uint64_t
    elapsed_ms(std::chrono::steady_clock::time_point since) noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - since)
                .count());
    }
    static void limit_helper_job(HANDLE job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)))
            win_error("Limit dump helper job");
    }
    static std::string file_sha256(const fs::path& path) {
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                nullptr));
        if (!file)
            win_error("Open dump helper for hashing");
        return sha256(file.get());
    }

    void record_error(std::string_view step, DWORD error) noexcept {
        try {
            if (errors_.size() < 32)
                errors_.push_back("{\"step\":" + json(step) +
                                  ",\"error\":" + std::to_string(error) + '}');
        } catch (...) {
        }
    }

    // Writes one dump through the helper. Never throws.
    HelperRun run_helper(std::wstring_view kind, std::wstring_view stem,
                         const std::wstring& extra) noexcept {
        HelperRun run;
        try {
            run.file = utf8(std::wstring(stem) + L".dmp");
            const auto fail = [&](std::string_view step) {
                run.error = GetLastError();
                run.error_step = step;
                record_error(step, run.error);
                return run;
            };
            SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
            const auto create = [&](std::wstring_view suffix) {
                const auto path =
                    dump_dir_ / (std::wstring(stem) + std::wstring(suffix));
                return Handle(CreateFileW(
                    path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                    &inherit, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            };
            Handle dump = create(L".dmp");
            if (!dump)
                return fail("helper-dump-file");
            Handle summary = create(L".json");
            if (!summary)
                return fail("helper-summary-file");
            HANDLE duplicate_raw = nullptr;
            // Query and read only: no terminate, write or duplicate right.
            if (!DuplicateHandle(GetCurrentProcess(), process_,
                                 GetCurrentProcess(), &duplicate_raw,
                                 PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                     SYNCHRONIZE,
                                 TRUE, 0))
                return fail("helper-process-handle");
            Handle duplicate(duplicate_raw);
            std::array<HANDLE, 3> inherited{duplicate.get(), dump.get(),
                                            summary.get()};
            SIZE_T size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
            std::vector<std::byte> storage(size);
            auto* attributes =
                reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
            if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size))
                return fail("helper-attributes");
            struct Cleanup {
                LPPROC_THREAD_ATTRIBUTE_LIST value;
                ~Cleanup() { DeleteProcThreadAttributeList(value); }
            } cleanup{attributes};
            if (!UpdateProcThreadAttribute(
                    attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    inherited.data(), sizeof(inherited), nullptr, nullptr))
                return fail("helper-handle-list");
            const auto number = [](auto value) {
                std::wostringstream text;
                text << L"0x" << std::hex
                     << reinterpret_cast<std::uintptr_t>(value);
                return text.str();
            };
            std::wstring command =
                L"\"" + helper_.wstring() + L"\" --kind " + std::wstring(kind) +
                L" --process " + number(duplicate.get()) + L" --dump " +
                number(dump.get()) + L" --summary " + number(summary.get()) +
                L" --pid " + std::to_wstring(pid_) + L" --creation-filetime " +
                std::to_wstring(channel_->creation_filetime) + extra;
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attributes;
            PROCESS_INFORMATION created{};
            Handle job(CreateJobObjectW(nullptr, nullptr));
            if (!job)
                return fail("helper-job");
            limit_helper_job(job.get());
            const auto start = std::chrono::steady_clock::now();
            if (!CreateProcessW(
                    helper_.c_str(), command.data(), nullptr, nullptr, TRUE,
                    EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED |
                        CREATE_NO_WINDOW,
                    nullptr, dump_dir_.c_str(), &startup.StartupInfo, &created))
                return fail("helper-start");
            Handle helper(created.hProcess), thread(created.hThread);
            if (!AssignProcessToJobObject(job.get(), helper.get())) {
                TerminateProcess(helper.get(), ERROR_CANCELLED);
                return fail("helper-confine");
            }
            run.started = true;
            ResumeThread(thread.get());
            // Measured (review D2): stopping the helper mid-dump lets the
            // target resume, so the S9 limit may end it.
            if (WaitForSingleObject(helper.get(), crash::helper_limit_ms) !=
                WAIT_OBJECT_0) {
                run.timed_out = true;
                TerminateJobObject(job.get(), ERROR_TIMEOUT);
                WaitForSingleObject(helper.get(), 5000);
                record_error("helper-timeout", ERROR_TIMEOUT);
            }
            DWORD code = 0;
            if (GetExitCodeProcess(helper.get(), &code))
                run.exit_code = static_cast<int>(code);
            run.ms = elapsed_ms(start);
            duplicate.reset();
            LARGE_INTEGER bytes{};
            if (GetFileSizeEx(dump.get(), &bytes))
                run.bytes = static_cast<std::uint64_t>(bytes.QuadPart);
            if (run.bytes != 0)
                run.sha256 = sha256(dump.get());
            LARGE_INTEGER begin{};
            std::string text;
            std::array<char, 4096> buffer{};
            DWORD read = 0;
            if (SetFilePointerEx(summary.get(), begin, nullptr, FILE_BEGIN))
                while (text.size() < helper_summary_limit &&
                       ReadFile(summary.get(), buffer.data(),
                                static_cast<DWORD>(buffer.size()), &read,
                                nullptr) &&
                       read != 0)
                    text.append(buffer.data(), read);
            if (json_object_shape(text)) {
                while (!text.empty() &&
                       (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                run.summary = text;
            } else if (!text.empty()) {
                record_error("helper-summary-invalid", ERROR_INVALID_DATA);
            }
        } catch (...) {
            run.error_step = "helper-exception";
            record_error("helper-exception", ERROR_INVALID_FUNCTION);
        }
        return run;
    }

    void handle_crash() {
        auto* ch = channel_;
        if (ch == nullptr ||
            ch->crash_state != static_cast<LONG>(crash::CrashState::claimed))
            return record_error("crash-request-unclaimed", ERROR_INVALID_STATE);
        crash_claimed_ = true;
        crash_thread_ = ch->thread_id;
        crash_code_ = ch->exception_code;
        Handle thread(
            OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, ch->thread_id));
        if (!thread || GetProcessIdOfThread(thread.get()) != pid_) {
            InterlockedExchange(
                &ch->crash_state,
                static_cast<LONG>(crash::CrashState::request_invalid));
            return record_error("crash-thread-foreign", ERROR_INVALID_DATA);
        }
        thread.reset();
        // Main image range of the child, for RVA-only reporting (C11).
        HMODULE first = nullptr;
        DWORD needed = 0;
        MODULEINFO module{};
        if (EnumProcessModulesEx(process_, &first, sizeof(first), &needed,
                                 LIST_MODULES_32BIT) &&
            first != nullptr &&
            GetModuleInformation(process_, first, &module, sizeof(module))) {
            image_base_ = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll));
            image_end_ = image_base_ + module.SizeOfImage;
        }
        std::wostringstream extra;
        extra << L" --thread " << ch->thread_id << L" --exception-pointers 0x"
              << std::hex << ch->exception_pointers;
        crash_run_ = run_helper(L"crash", L"tdur-crash-1", extra.str());
        InterlockedExchange(
            &ch->crash_state,
            static_cast<LONG>(crash_run_->exit_code == 0
                                  ? crash::CrashState::dumped
                                  : crash::CrashState::dump_failed));
        crash_dumped_ = true;
        crash_dump_end_ = std::chrono::steady_clock::now();
    }

    struct Window {
        DWORD pid = 0;
        HWND chosen = nullptr;
        long long area = -1;
    };
    static BOOL CALLBACK collect(HWND window, LPARAM parameter) {
        auto* state = reinterpret_cast<Window*>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != state->pid || GetWindow(window, GW_OWNER) != nullptr ||
            !IsWindowVisible(window))
            return TRUE;
        RECT rect{};
        GetWindowRect(window, &rect);
        const long long area = static_cast<long long>(rect.right - rect.left) *
                               (rect.bottom - rect.top);
        if (area > state->area) {
            state->area = area;
            state->chosen = window;
        }
        return TRUE;
    }

    // Window metadata only: owned top-level HWND, visibility, iconic,
    // foreground and IsHungAppWindow. Never text, class, nor any message.
    void sample(std::uint64_t now) {
        ++samples_;
        DWORD owner = 0;
        if (window_ != nullptr) {
            GetWindowThreadProcessId(window_, &owner);
            if (!IsWindow(window_) || owner != pid_ ||
                !(IsWindowVisible(window_) || IsHungAppWindow(window_)))
                window_ = nullptr;
        }
        if (window_ == nullptr) {
            Window state{pid_};
            EnumWindows(collect, reinterpret_cast<LPARAM>(&state));
            window_ = state.chosen;
        }
        watchdog::Sample value;
        value.now_ms = now;
        value.window_known = window_ != nullptr;
        if (value.window_known) {
            window_seen_ = true;
            value.window_hung = IsHungAppWindow(window_) != FALSE;
            value.window_iconic = IsIconic(window_) != FALSE;
            owner = 0;
            const HWND foreground = GetForegroundWindow();
            if (foreground != nullptr)
                GetWindowThreadProcessId(foreground, &owner);
            value.window_foreground = owner == pid_;
        }
        const LONG beats = channel_->present_beats;
        value.beats_known = beats > 0;
        value.beats = static_cast<std::uint32_t>(beats);
        value.crash_in_progress = channel_->crash_state ==
                                  static_cast<LONG>(crash::CrashState::claimed);
        const auto actions = dog_.step(value);
        const std::vector<watchdog::Action> copy(actions.begin(),
                                                 actions.end());
        for (const auto action : copy) {
            switch (action) {
            case watchdog::Action::hang_dump:
            case watchdog::Action::beat_dump: {
                const auto stem =
                    L"tdur-hang-" + std::to_wstring(hang_runs_.size() + 1);
                hang_runs_.push_back(run_helper(L"hang", stem, L""));
                std::cout << "hang-dump file=" << utf8(stem)
                          << ".dmp exit=" << hang_runs_.back().exit_code
                          << (action == watchdog::Action::beat_dump
                                  ? " cause=beat"
                                  : " cause=window")
                          << std::endl;
                break;
            }
            case watchdog::Action::ask:
                open_dialog();
                break;
            case watchdog::Action::stop:
                stop_for_hang();
                break;
            case watchdog::Action::episode_ended:
                close_dialog();
                break;
            }
        }
    }

    void stop_for_hang() {
        const auto hung = dog_.current().hung_ms;
        stop_ = "{\"by\":\"launcher\",\"reason\":\"hang\",\"policy\":" +
                json(policy_name(dog_.policy())) +
                ",\"hung_ms\":" + std::to_string(hung) +
                ",\"answer\":" + json(answer_name(dog_.current().answer)) +
                ",\"exit_code_requested\":\"0xe1480001\"}";
        std::cout << "game-stopped reason=hang hung_ms=" << hung << std::endl;
        if (!TerminateJobObject(game_job_, crash::hang_stop_exit_code)) {
            // The stop failed: the game keeps running and the report says so.
            record_error("hang-stop", GetLastError());
            stop_.reset();
        }
    }

    struct Question {
        std::wstring text;
        std::wstring title;
        std::atomic<int> result{-1};
        std::atomic<bool> auto_closing{false};
        DWORD thread_id = 0;
    };
    static DWORD WINAPI question_thread(void* parameter) {
        auto* owner = static_cast<std::shared_ptr<Question>*>(parameter);
        const auto question = *owner;
        delete owner;
#ifdef TDUR_CRASH_TEST_ANSWER
        // Review F1, test variants only (crash_tests.cmake): the answer is
        // fixed at compile time and no box is opened, so the launcher fixture
        // runs the question path without a window.
        question->result = TDUR_CRASH_TEST_ANSWER;
#else
        // Review D3: no MB_SETFOREGROUND, so the hung game is not made to
        // lose the focus by this box; MB_TOPMOST keeps it visible above a
        // borderless window. Default button: No.
        const int result = MessageBoxW(
            nullptr, question->text.c_str(), question->title.c_str(),
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST);
        question->result = result;
#endif
        return 0;
    }
    void open_dialog() {
        if (!dialog_enabled_ || question_)
            return;
        auto question = std::make_shared<Question>();
        question->title = L"TDU Recompilation: game not responding (PID " +
                          std::to_wstring(pid_) + L")";
        question->text =
            L"The game has not responded for " +
            std::to_wstring(dog_.current().hung_ms / 1000) +
            L" seconds. A dump was written for diagnosis.\n\n"
            L"Close the game now?\n\nYes: the game is closed, the "
            L"private copy of the save may remain incomplete, your "
            L"original saves are not touched.\nNo: keep waiting.";
        auto* parameter = new std::shared_ptr<Question>(question);
        HANDLE thread = CreateThread(nullptr, 0, question_thread, parameter, 0,
                                     &question->thread_id);
        if (thread == nullptr) {
            delete parameter;
            record_error("question-thread", GetLastError());
            return;
        }
        question_thread_ = Handle(thread);
        question_ = question;
        question_episode_ = dog_.current().start_ms;
        std::cout << "hang-question-open episode_ms=" << question_episode_
                  << std::endl;
    }
    void poll_dialog() {
        if (!question_)
            return;
        const int result = question_->result;
        if (result < 0)
            return;
        if (question_->auto_closing)
            dog_.record_auto_closed();
        else if (!dog_.record_answer(question_episode_, result == IDYES))
            record_error("answer-after-episode", ERROR_TIMEOUT);
        std::cout << "hang-question-answer answer="
                  << (question_->auto_closing ? "auto-closed"
                      : result == IDYES       ? "yes"
                                              : "no")
                  << std::endl;
        question_.reset();
        question_thread_.reset();
    }
    static BOOL CALLBACK close_box(HWND window, LPARAM) {
        PostMessageW(window, WM_COMMAND, IDNO, 0);
        return TRUE;
    }
    void close_dialog() noexcept {
        try {
            close_question();
        } catch (...) {
            record_error("question-close", ERROR_INVALID_FUNCTION);
        }
    }
    void close_question() {
        if (!question_)
            return;
        question_->auto_closing = true;
        EnumThreadWindows(question_->thread_id, close_box, 0);
        if (question_thread_)
            WaitForSingleObject(question_thread_.get(), 2000);
        if (question_->result < 0) {
            // Still open: the box ends with this process; never block here.
            record_error("question-not-closed", ERROR_TIMEOUT);
            dog_.record_auto_closed();
            question_.reset();
            question_thread_.reset();
            return;
        }
        poll_dialog();
    }

    void write_crash(std::ostream& output) const {
        if (!crash_claimed_) {
            output << "null";
            return;
        }
        const auto* ch = channel_;
        const auto main_rva = [&](std::uint32_t address) {
            // Addresses inside the main image are reported as RVAs only.
            const auto base = image_base_;
            if (base != 0 && address >= base && address < image_end_)
                return "{\"main_rva\":" + hex32(address - base) + '}';
            return hex32(address);
        };
        const auto filter = [&]() -> std::string {
            if (ch->real_filter_done != 1)
                return "unknown";
            switch (static_cast<LONG>(ch->real_filter_result)) {
            case EXCEPTION_EXECUTE_HANDLER:
                return "execute_handler";
            case EXCEPTION_CONTINUE_SEARCH:
                return "continue_search";
            case EXCEPTION_CONTINUE_EXECUTION:
                return "continue_execution";
            default:
                return "other";
            }
        };
        const auto state = ch->crash_state;
        output << "{\"thread_id\":" << crash_thread_
               << ",\"exception_code\":" << hex32(crash_code_)
               << ",\"exception_flags\":" << ch->exception_flags
               << ",\"exception_address\":" << main_rva(ch->exception_address)
               << ",\"access\":{\"kind\":" << ch->exception_info[0]
               << ",\"target\":" << main_rva(ch->exception_info[1])
               << "},\"parameters\":" << ch->exception_params
               << ",\"real_filter_result\":" << json(filter())
               << ",\"mod_wait\":"
               << json(state ==
                               static_cast<LONG>(crash::CrashState::mod_timeout)
                           ? "timeout"
                           : "done")
               << ",\"later_entries\":"
               << (ch->crash_entries > 0 ? ch->crash_entries - 1 : 0)
               << ",\"process_alive_after_dump_ms\":" << crash_alive_ms_
               << ",\"dump\":";
        if (crash_run_)
            crash_run_->write_json(output);
        else
            output << "null";
        output << '}';
    }

    fs::path helper_;
    fs::path dump_dir_;
    watchdog::Watchdog dog_;
    bool dialog_enabled_ = false;
    bool test_thresholds_ = false;
    std::string self_check_;
    std::string helper_sha256_;
    Handle mapping_;
    crash::Channel* channel_ = nullptr;
    Handle request_, done_, image_;
    bool created_ = false;
    bool events_disabled_ = false;
    DWORD pid_ = 0;
    HANDLE process_ = nullptr;
    HANDLE game_job_ = nullptr;
    bool crash_claimed_ = false;
    bool crash_dumped_ = false;
    DWORD crash_thread_ = 0;
    DWORD crash_code_ = 0;
    std::optional<HelperRun> crash_run_;
    std::chrono::steady_clock::time_point crash_dump_end_{};
    std::uint64_t crash_alive_ms_ = 0;
    bool process_end_seen_ = false;
    std::uint32_t image_base_ = 0;
    std::uint32_t image_end_ = 0;
    std::vector<HelperRun> hang_runs_;
    std::optional<std::string> stop_;
    std::vector<std::string> errors_;
    HWND window_ = nullptr;
    bool window_seen_ = false;
    std::uint64_t samples_ = 0;
    bool have_tick_ = false;
    std::uint64_t last_tick_ = 0;
    bool episode_open_at_exit_ = false;
    std::shared_ptr<Question> question_;
    Handle question_thread_;
    std::uint64_t question_episode_ = 0;
};

std::set<DWORD> job_processes(HANDLE job) {
    // The observer refuses unconfined process creation. Still enumerate the
    // entire job so any unexpected children remain owned and diagnosable.
    std::array<ULONG_PTR, 260> storage{};
    auto* list =
        reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list,
                                   static_cast<DWORD>(sizeof(storage)),
                                   nullptr)) {
        win_error("Enumerate owned job processes");
    }
    std::set<DWORD> result;
    for (DWORD index = 0; index < list->NumberOfProcessIdsInList; ++index) {
        result.insert(static_cast<DWORD>(list->ProcessIdList[index]));
    }
    return result;
}

void configure_environment(const Options& value) {
    const std::pair<const wchar_t*, const fs::path*> entries[]{
        {L"TDUR_PRIVATE_DOCUMENTS", &value.private_documents},
        {L"TDUR_SOURCE_DOCUMENTS", &value.source_documents},
        {L"TDUR_OBSERVATION_LOG", &value.log}};
    for (const auto& [name, path] : entries) {
        if (!SetEnvironmentVariableW(name, path->c_str())) {
            win_error("Configure child observation environment");
        }
    }
    // The observer maps the crash channel this launcher prepares.
    if (!SetEnvironmentVariableW(L"TDUR_CRASH_CHANNEL", L"1"))
        win_error("Configure child crash channel");
}

int launch(const Options& value) {
    CrashSession cs(value.helper, value.dump_dir, value.thresholds,
                    value.hang_action, value.notify_dialog,
                    value.test_thresholds);
    // Before any lock or process: also lets antivirus scan the helper now.
    cs.self_check();
    auto locked = lock_saves(value.source_saves);
    // Keep the exact image immutable between hash verification and execution.
    // FILE_SHARE_READ permits the loader but refuses writes and replacement.
    Handle locked_executable(
        CreateFileW(value.executable.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!locked_executable)
        win_error("Lock private executable identity");
    const auto executable_sha256 = sha256(locked_executable.get());
    const bool retail_verified =
        value.executable.filename() == L"TestDriveUnlimited.exe" &&
        executable_sha256 == tdur::game::audited_executable_sha256;
    // This is a launcher-local provenance marker, not an authentication token.
    // Always overwrite an inherited value, including for source-owned fixtures.
    if (!SetEnvironmentVariableW(L"TDUR_RETAIL_EXECUTABLE_VERIFIED",
                                 retail_verified ? L"1" : L"0"))
        win_error("Set verified child image marker");
    // The port outlives the job, including any failure before child resume.
    JobNotifications notifications;
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        win_error("Create private job");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        win_error("Set private job lifetime limits");
    }
    notifications.associate(job.get());
    configure_environment(value);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION created{};
    std::wstring command = L"\"" + value.executable.wstring() + L"\"";
    if (!value.system_config.empty()) {
        const auto config = value.system_config.wstring();
        command += L" -system ";
        command += config.find_first_of(L" \t") == std::wstring::npos
                       ? config
                       : L"\"" + config + L"\"";
    }
    const std::string dll = value.dll.string();
    if (!DetourCreateProcessWithDllExW(
            value.executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED, nullptr, value.executable.parent_path().c_str(),
            &startup, &created, dll.c_str(), nullptr)) {
        win_error("Create observed private process");
    }
    Handle process(created.hProcess);
    Handle thread(created.hThread);
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const auto error = GetLastError();
        TerminateProcess(process.get(), ERROR_CANCELLED);
        WaitForSingleObject(process.get(), 5000);
        SetLastError(error);
        win_error("Confine suspended process to private job");
    }
    notifications.poll(process.get(), created.dwProcessId);
    FILETIME creation{}, exit_time{}, kernel_time{}, user_time{};
    if (!GetProcessTimes(process.get(), &creation, &exit_time, &kernel_time,
                         &user_time))
        win_error("Read owned process creation time before resume");
    const auto creation_filetime =
        (static_cast<unsigned long long>(creation.dwHighDateTime) << 32) |
        creation.dwLowDateTime;
    try {
        cs.create_channel(created.dwProcessId, creation_filetime);
    } catch (...) {
        // Never resumed: stop the suspended child before reporting.
        TerminateProcess(process.get(), ERROR_CANCELLED);
        WaitForSingleObject(process.get(), 5000);
        throw;
    }
    cs.bind(process.get(), job.get());
    write_new_file(
        value.pid_file,
        "{\"pid\":" + std::to_string(created.dwProcessId) +
            ",\"creation_filetime\":" + std::to_string(creation_filetime) +
            ",\"inherited_error_mode\":" + std::to_string(GetErrorMode()) +
            ",\"suspended\":true}\n");
    // The parent process registers this exact PID before it publishes the
    // resume marker. No game code runs before that step.
    const auto registration_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!fs::exists(value.resume_file)) {
        if (std::chrono::steady_clock::now() >= registration_deadline) {
            throw std::runtime_error("No resume marker within 15 seconds");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        win_error("Resume registered process");
    }
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(value.seconds);
    bool timed_out = false;
    std::set<DWORD> all_pids{created.dwProcessId};
    while (true) {
        auto pids = job_processes(job.get());
        all_pids.insert(pids.begin(), pids.end());
        // Window metadata only: owned top-level HWND, visibility, iconic,
        // foreground and IsHungAppWindow. Never window text, class, or any
        // message sent to the target.
        DWORD wait = WAIT_FAILED;
        if (!cs.events_disabled()) {
            const std::array<HANDLE, 3> handles{
                process.get(), cs.request_event(), cs.image_event()};
            wait = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                          handles.data(), FALSE, 100);
            if (wait == WAIT_FAILED)
                cs.on_wait_failed(GetLastError());
            else if (wait == WAIT_OBJECT_0 + 1)
                cs.on_crash_request();
        }
        if (cs.events_disabled())
            wait = WaitForSingleObject(process.get(), 100);
        const bool exited =
            WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0;
        cs.tick(!exited);
        notifications.poll(process.get(), created.dwProcessId);
        if (exited && pids.empty()) {
            break;
        }
        if (wait == WAIT_FAILED) {
            win_error("Wait for private process");
        }
        if (!value.play_session &&
            std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            if (!TerminateJobObject(job.get(), ERROR_TIMEOUT)) {
                win_error("Stop owned job after diagnostic deadline");
            }
            break;
        }
    }
    if (WaitForSingleObject(process.get(), 5000) != WAIT_OBJECT_0) {
        throw std::runtime_error("Owned target did not stop");
    }
    cs.on_process_end();
    // Confirm every owned child has stopped before hashing original saves.
    const auto stop_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!job_processes(job.get()).empty()) {
        if (std::chrono::steady_clock::now() >= stop_deadline) {
            throw std::runtime_error("Owned child did not stop");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.get(), &exit_code)) {
        win_error("Read owned process exit code");
    }
    if (!GetProcessTimes(process.get(), &creation, &exit_time, &kernel_time,
                         &user_time))
        win_error("Read owned process exit timestamp");
    notifications.poll(process.get(), created.dwProcessId);
    const bool unchanged = same_saves(value.source_saves, locked);
    std::ostringstream report;
    report << "{\n  \"instrumented_launch\":true,\n"
           << "  \"diagnostic_child_process_limit\":1,\n"
           << "  \"executable\":" << json(utf8(value.executable.wstring()))
           << ",\n  \"executable_sha256\":" << json(executable_sha256)
           << ",\n  \"executable_locked\":true"
           << ",\n  \"retail_executable_verified\":"
           << (retail_verified ? "true" : "false") << ",\n  \"system_config\":"
           << (value.system_config.empty()
                   ? "null"
                   : json(utf8(value.system_config.wstring())))
           << ",\n  \"pid\":" << created.dwProcessId
           << ",\n  \"creation_filetime\":" << creation_filetime
           << ",\n  \"session_mode\":"
           << json(value.play_session ? "play" : "diagnostic")
           << ",\n  \"window_text_collected\":false"
           << ",\n  \"window_messages_sent\":false"
           << ",\n  \"timed_out\":" << (timed_out ? "true" : "false")
           << ",\n  \"exit_code\":" << exit_code
           << ",\n  \"process_exit_filetime\":\"" << filetime_value(exit_time)
           << "\",\n  \"job_notifications\":";
    notifications.write_json(report);
    report << ",\n  \"seconds\":"
           << std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                            start)
                  .count()
           << ",\n  \"locked_save_files\":" << locked.size()
           << ",\n  \"source_saves_unchanged\":"
           << (unchanged ? "true" : "false") << ",\n  \"owned_pids\":[";
    bool first = true;
    for (const auto pid : all_pids) {
        report << (first ? "" : ",") << pid;
        first = false;
    }
    // Keep the legacy field empty for existing report readers.
    report << "],\n  \"visible_windows\":[],\n  \"save_sha256_before\":{";
    first = true;
    for (const auto& save : locked) {
        report << (first ? "" : ",") << json(utf8(save.relative)) << ':'
               << json(save.digest);
        first = false;
    }
    report << "}";
    cs.write_json(report, timed_out, exit_code);
    report << "\n}\n";
    write_new_file(value.report, report.str());
    std::cout << "game-ended reason=" << cs.end_reason(timed_out, exit_code)
              << " exit=" << exit_code << '\n';
    std::cout << "Observed PID " << created.dwProcessId << ", exit "
              << exit_code << ", timed_out=" << timed_out
              << ", source_saves_unchanged=" << unchanged << '\n';
    return unchanged ? 0 : 3;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    // Keep loader failures and crashes noninteractive in this owned process
    // tree. CREATE_DEFAULT_ERROR_MODE would undo this for the child.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS |
                 SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        return launch(options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "Diagnostic launcher: " << error.what() << '\n';
        return 2;
    }
}

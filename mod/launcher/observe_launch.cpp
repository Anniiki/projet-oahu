// Diagnostic launcher for one private x86 copy, not a game implementation.
// Detours changes the child's in-memory imports before its entry point. The
// original executable on disk and all activation/protection APIs are untouched.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#include <detours.h>

#include "tdur/game/installation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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
};

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
                                         L"--session-mode"};
    if (argc != 21 && argc != 23 && argc != 25) {
        throw std::runtime_error(
            "Expected ten options, optional private INI and session mode");
    }
    for (int index = 1; index + 1 < argc; index += 2) {
        if (!allowed.contains(argv[index]) ||
            !values.emplace(argv[index], argv[index + 1]).second) {
            throw std::runtime_error("Unknown or repeated launcher option");
        }
    }
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
    // Detours' import descriptor is a byte string; require an unambiguous
    // ASCII DLL path rather than silently using an ACP-corrupted filename.
    for (const auto character : result.dll.wstring()) {
        if (character > 0x7f) {
            throw std::runtime_error("Detours DLL path must be ASCII");
        }
    }
    return result;
}

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
}

int launch(const Options& value) {
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
            CREATE_SUSPENDED | CREATE_DEFAULT_ERROR_MODE, nullptr,
            value.executable.parent_path().c_str(), &startup, &created,
            dll.c_str(), nullptr)) {
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
    write_new_file(
        value.pid_file,
        "{\"pid\":" + std::to_string(created.dwProcessId) +
            ",\"creation_filetime\":" + std::to_string(creation_filetime) +
            ",\"suspended\":true}\n");
    // The Python wrapper registers this exact PID with coop-anything gameslot
    // before publishing the resume marker. No game code runs before that step.
    const auto registration_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!fs::exists(value.resume_file)) {
        if (std::chrono::steady_clock::now() >= registration_deadline) {
            throw std::runtime_error(
                "No gameslot resume marker within 15 seconds");
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
        // Both session modes use process metadata only. Do not enumerate
        // windows or send messages to the target's UI, including diagnostics.
        const auto wait = WaitForSingleObject(process.get(), 100);
        notifications.poll(process.get(), created.dwProcessId);
        if (wait == WAIT_OBJECT_0 && pids.empty()) {
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
    report << "}\n}\n";
    write_new_file(value.report, report.str());
    std::cout << "Observed PID " << created.dwProcessId << ", exit "
              << exit_code << ", timed_out=" << timed_out
              << ", source_saves_unchanged=" << unchanged << '\n';
    return unchanged ? 0 : 3;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        return launch(options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "Diagnostic launcher: " << error.what() << '\n';
        return 2;
    }
}

#pragma once
// Private adapter for the fixed UI diagnostic; no address-taking CLI.
#include "runtime_readonly_image.hpp"
#include "tdur/diagnostics/ui_service_binding.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>

namespace tdur::diagnostics::ui_detail {
inline std::uint64_t parse_decimal(std::wstring_view text) {
    if (text.empty())
        throw std::runtime_error(
            "Expected a nonempty unsigned decimal integer");
    std::uint64_t value{};
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9')
            throw std::runtime_error(
                "Only unsigned decimal integers are accepted");
        const auto digit = static_cast<unsigned>(c - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            throw std::runtime_error("Unsigned integer is out of range");
        value = value * 10 + digit;
    }
    return value;
}

inline std::uint64_t creation_time(HANDLE process) noexcept {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        return 0;
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) |
           creation.dwLowDateTime;
}

struct IdentityHeaderWord {
    std::uint32_t rva{};
    std::uint32_t value{};
};

struct PE32IdentityHeader {
    std::uint32_t dos_word{};
    std::uint32_t nt_offset{};
    std::uint32_t signature{};
    std::uint32_t machine_sections{};
    std::uint32_t magic_linker{};
    std::uint32_t image_base{};
    std::uint32_t image_size{};
};

// Exact read-only profile measured in official trial22. The runtime header
// describes a smaller extent than Toolhelp's loader entry. Neither arbitrary
// smaller headers nor the secondary executable's five-section header match.
inline bool official_runtime_header(const PE32IdentityHeader& header,
                                    std::uint64_t loader_base,
                                    std::uint64_t loader_size) noexcept {
    return loader_base == 0x00400000 && loader_size == 0x01bb0000 &&
           header.dos_word == 0x00905a4d && header.nt_offset == 0x138 &&
           header.signature == 0x4550 &&
           header.machine_sections == 0x0004014c &&
           header.magic_linker == 0x0a07010b &&
           header.image_base == loader_base && header.image_size == 0x012ad000;
}

inline bool within_image_extent(std::uint64_t address, std::uint64_t size,
                                std::uint64_t base,
                                std::uint64_t extent) noexcept {
    return address >= base && address - base <= extent &&
           size <= extent - (address - base);
}

template <class ReadWord>
bool inspect_identity_header(ReadWord read, std::uint64_t loader_base,
                             std::uint64_t loader_size, bool verified,
                             std::uint64_t& runtime_size) {
    runtime_size = 0;
    if (loader_size < 64 ||
        (verified && (loader_base != 0x400000 || loader_size != 0x01bb0000)))
        return false;
    PE32IdentityHeader header{};
    header.dos_word = read(0);
    if ((header.dos_word & 0xffffU) != IMAGE_DOS_SIGNATURE ||
        (verified && header.dos_word != 0x00905a4d))
        return false;
    const auto nt = header.nt_offset = read(0x3c);
    // Do not follow an unknown production e_lfanew, even within Toolhelp's
    // larger extent. Generic own fixtures cannot attest an official SHA.
    if ((verified && nt != 0x138) || nt < 64 ||
        static_cast<std::uint64_t>(nt) + 24 + 60 > loader_size)
        return false;
    header.signature = read(nt);
    header.machine_sections = read(nt + 4);
    header.magic_linker = read(nt + 24);
    header.image_base = read(nt + 24 + 28);
    runtime_size = header.image_size = read(nt + 24 + 56);
    if (verified)
        return official_runtime_header(header, loader_base, loader_size);
    return header.signature == IMAGE_NT_SIGNATURE &&
           (header.machine_sections & 0xffffU) == IMAGE_FILE_MACHINE_I386 &&
           (header.magic_linker & 0xffffU) == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           header.image_base == loader_base && header.image_size == loader_size;
}

class UiWindowsBindingReader final : public UiServiceBindingReader {
  public:
    // This private adapter is constructed with verified=true only after the
    // production CLI has locked and hashed the expected official image.
    // The self-test uses false and cannot pass collector admission.
    UiWindowsBindingReader(ReadOnlyImage& image,
                           UiServiceExpectedProcess expected, bool verified)
        : image_(image), expected_(expected), verified_(verified) {
        process_.reset(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                        FALSE, expected.pid));
        if (!process_ || GetProcessId(process_.get()) != expected.pid ||
            creation_time(process_.get()) != expected.creation_filetime ||
            image_.creation() != expected.creation_filetime)
            throw std::runtime_error(
                "Process creation does not match the owned launch");
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            throw std::runtime_error(
                "Cannot establish the observation QPC frequency");
        frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        maximum_user_address_ =
            std::min<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                                        system.lpMaximumApplicationAddress),
                                    UINT32_MAX);
        pe32_ = inspect_pe32();
    }

    UiServiceBindingIdentity identity() noexcept override {
        UiServiceBindingIdentity id{};
        if (GetProcessId(process_.get()) != expected_.pid)
            return id;
        id.pid = expected_.pid;
        id.creation_filetime = creation_time(process_.get());
        if (image_.base() <= UINT32_MAX)
            id.image_base = static_cast<std::uint32_t>(image_.base());
        id.image_size = runtime_image_size_;
        id.maximum_user_address = maximum_user_address_;
        id.qpc_frequency = frequency_;
        id.official_sha_verified = verified_;
        id.pe32_i386 = pe32_;
        return id;
    }

    bool timestamp(std::uint64_t& tick) noexcept override {
        LARGE_INTEGER value{};
        if (!QueryPerformanceCounter(&value) || value.QuadPart < 0)
            return false;
        tick = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }

    bool query(std::uint32_t address,
               UiServiceBindingPage& result) noexcept override {
        MEMORY_BASIC_INFORMATION page{};
        if (VirtualQueryEx(process_.get(),
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(address)),
                           &page, sizeof(page)) != sizeof(page))
            return false;
        result = {};
        result.begin = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        result.size = page.RegionSize;
        result.allocation_base =
            reinterpret_cast<std::uintptr_t>(page.AllocationBase);
        result.type = page.Type == MEM_IMAGE ? UiServiceMemoryType::image
                      : page.Type == MEM_PRIVATE
                          ? UiServiceMemoryType::private_memory
                          : UiServiceMemoryType::other;
        result.committed = page.State == MEM_COMMIT;
        result.guard = (page.Protect & PAGE_GUARD) != 0;
        const auto protection = page.Protect & 0xffU;
        result.readable =
            protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
        result.writable = protection == PAGE_READWRITE ||
                          protection == PAGE_WRITECOPY ||
                          protection == PAGE_EXECUTE_READWRITE ||
                          protection == PAGE_EXECUTE_WRITECOPY;
        result.executable = protection == PAGE_EXECUTE ||
                            protection == PAGE_EXECUTE_READ ||
                            protection == PAGE_EXECUTE_READWRITE ||
                            protection == PAGE_EXECUTE_WRITECOPY;
        return true;
    }

    UiServiceBindingTransfer
    read(std::uint32_t address,
         std::span<std::byte> destination) noexcept override {
        if (destination.size() != 4 || payload_attempted_bytes_ > 96U - 4U)
            return {};
        payload_attempted_bytes_ += 4;
        const auto end = static_cast<std::uint64_t>(address) + 4;
        if (address == 0 || end > maximum_user_address_ + 1)
            return {};
        const bool image_range =
            within_image_extent(address, 4, image_.base(), runtime_image_size_);
        for (auto cursor = static_cast<std::uint64_t>(address); cursor < end;) {
            UiServiceBindingPage page{};
            if (!query(static_cast<std::uint32_t>(cursor), page) ||
                !page.committed || page.guard || !page.readable ||
                page.executable || page.size == 0 || page.begin > cursor ||
                page.begin >= (std::uint64_t{1} << 32) ||
                page.size > (std::uint64_t{1} << 32) - page.begin ||
                page.begin + page.size <= cursor)
                return {};
            const bool main = page.type == UiServiceMemoryType::image &&
                              page.allocation_base == image_.base();
            if ((main && !image_range) || (image_range && !main) ||
                (!main && page.type != UiServiceMemoryType::private_memory))
                return {};
            cursor = page.begin + page.size;
        }
        SIZE_T received{};
        payload_rpm_requested_bytes_ += 4;
        const bool success =
            ReadProcessMemory(process_.get(),
                              reinterpret_cast<const void*>(
                                  static_cast<std::uintptr_t>(address)),
                              destination.data(), destination.size(),
                              &received) != FALSE;
        payload_received_bytes_ +=
            static_cast<std::uint32_t>(std::min<SIZE_T>(received, 4));
        return {success, received};
    }
    std::uint32_t payload_attempted_bytes() const noexcept {
        return payload_attempted_bytes_;
    }
    std::uint32_t payload_rpm_requested_bytes() const noexcept {
        return payload_rpm_requested_bytes_;
    }
    std::uint32_t payload_received_bytes() const noexcept {
        return payload_received_bytes_;
    }

    std::uint32_t identity_header_bytes() const noexcept {
        return header_bytes_;
    }
    std::uint64_t loader_image_size() const noexcept { return image_.size(); }
    std::span<const IdentityHeaderWord> identity_header_words() const noexcept {
        return std::span(header_words_).first(header_bytes_ / 4);
    }

  private:
    std::uint32_t image_word(std::uint32_t rva) {
        if (header_bytes_ / 4 >= header_words_.size())
            throw std::logic_error("PE identity header read budget exceeded");
        header_bytes_ += 4;
        const auto bytes = image_.scalar(rva, 4);
        std::uint32_t result{};
        std::memcpy(&result, bytes.data(), sizeof(result));
        header_words_[header_bytes_ / 4 - 1] = {rva, result};
        return result;
    }

    bool inspect_pe32() {
        return inspect_identity_header(
            [&](std::uint32_t rva) { return image_word(rva); }, image_.base(),
            image_.size(), verified_, runtime_image_size_);
    }

    ReadOnlyImage& image_;
    UiServiceExpectedProcess expected_;
    Handle process_;
    bool verified_{};
    bool pe32_{};
    std::uint64_t frequency_{};
    std::uint64_t maximum_user_address_{};
    std::uint64_t runtime_image_size_{};
    std::uint32_t header_bytes_{};
    std::uint32_t payload_attempted_bytes_{}, payload_rpm_requested_bytes_{},
        payload_received_bytes_{};
    std::array<IdentityHeaderWord, 7> header_words_{};
};

} // namespace tdur::diagnostics::ui_detail

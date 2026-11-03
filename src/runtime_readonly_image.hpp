#pragma once

// A read-only handle and bounded main-image reader for diagnostic processes.
// Callers supply fixed catalogues; command lines never accept an address.
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace tdur::diagnostics {

struct CloseHandleDeleter {
    void operator()(void* handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

class ReadOnlyImage {
  public:
    ReadOnlyImage(DWORD pid, const std::filesystem::path& expected) {
        namespace fs = std::filesystem;
        const auto canonical = fs::canonical(expected);
        process_.reset(OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));
        if (!process_)
            throw std::runtime_error("Cannot open target read-only");
        std::wstring image(32768, L'\0');
        DWORD length = static_cast<DWORD>(image.size());
        if (!QueryFullProcessImageNameW(process_.get(), 0, image.data(),
                                        &length))
            throw std::runtime_error("Cannot identify target image");
        image.resize(length);
        if (_wcsicmp(fs::canonical(image).c_str(), canonical.c_str()) != 0)
            throw std::runtime_error("Target is not the expected executable");
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process_.get(), &creation, &exit, &kernel, &user))
            throw std::runtime_error("Cannot identify target creation time");
        creation_ =
            (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) |
            creation.dwLowDateTime;
        Handle snapshot(CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (snapshot.get() == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot enumerate the target image");
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot.get(), &entry)) {
            do {
                if (_wcsicmp(entry.szExePath, canonical.c_str()) == 0) {
                    base_ = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                    size_ = entry.modBaseSize;
                    break;
                }
            } while (Module32NextW(snapshot.get(), &entry));
        }
        if (!base_ ||
            size_ > std::numeric_limits<std::uintptr_t>::max() - base_)
            throw std::runtime_error("Expected main image is not mapped");
    }

    std::vector<std::byte> code(std::uint32_t rva, std::size_t length) const {
        return read(rva, length, true);
    }
    std::vector<std::byte> scalar(std::uint32_t rva, std::size_t length) const {
        if (length != 1 && length != 4 && length != 8)
            throw std::runtime_error("Scalar read size must be 1, 4 or 8");
        return read(rva, length, false);
    }
    std::uintptr_t base() const { return base_; }
    std::size_t size() const { return size_; }
    std::uint64_t creation() const { return creation_; }

  private:
    std::vector<std::byte> read(std::uint32_t rva, std::size_t length,
                                bool want_code) const {
        if (length == 0 || length > 512 || length > size_ ||
            rva > size_ - length)
            throw std::runtime_error("Read is outside the bounded image");
        const auto address = base_ + rva;
        const auto limit = address + length;
        for (auto cursor = address; cursor < limit;) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQueryEx(process_.get(), reinterpret_cast<void*>(cursor),
                               &region, sizeof(region)) != sizeof(region))
                throw std::runtime_error("Cannot query image-page permissions");
            const auto protection = region.Protect & 0xffU;
            const bool executable = protection == PAGE_EXECUTE ||
                                    protection == PAGE_EXECUTE_READ ||
                                    protection == PAGE_EXECUTE_READWRITE ||
                                    protection == PAGE_EXECUTE_WRITECOPY;
            const bool readable_data = protection == PAGE_READONLY ||
                                       protection == PAGE_READWRITE ||
                                       protection == PAGE_WRITECOPY;
            if ((want_code ? !executable : !readable_data) ||
                region.State != MEM_COMMIT ||
                (region.Protect & PAGE_GUARD) != 0 ||
                reinterpret_cast<std::uintptr_t>(region.AllocationBase) !=
                    base_)
                throw std::runtime_error("Range has the wrong image-page type");
            const auto start =
                reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (region.RegionSize >
                    std::numeric_limits<std::uintptr_t>::max() - start ||
                start + region.RegionSize <= cursor)
                throw std::runtime_error("Invalid image-page range");
            cursor = start + region.RegionSize;
        }
        std::vector<std::byte> result(length);
        SIZE_T received = 0;
        if (!ReadProcessMemory(process_.get(), reinterpret_cast<void*>(address),
                               result.data(), result.size(), &received) ||
            received != result.size())
            throw std::runtime_error("Image bytes are not readable");
        return result;
    }

    Handle process_;
    std::uintptr_t base_ = 0;
    std::size_t size_ = 0;
    std::uint64_t creation_ = 0;
};

} // namespace tdur::diagnostics

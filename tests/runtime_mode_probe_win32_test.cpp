// Exercises the delivered Win32 page reader on this witness's own pages.
// No game process or production identity bypass is involved.
#define wmain delivered_entry_unused
#include "../src/runtime_mode_probe_main.cpp"
#undef wmain

int main() {
    Handle process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                    GetCurrentProcessId()));
    if (!process)
        return 2;
    void* allocation = VirtualAlloc(reinterpret_cast<void*>(0x20000000), 8192,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocation)
        return 3;
    auto* data = static_cast<unsigned char*>(allocation);
    for (unsigned i = 0; i < 8192; ++i)
        data[i] = static_cast<unsigned char>(i % 251);
    ProcessReader reader(process.get(), 0x400000, 0x1000000);
    const auto start = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(allocation));
    std::array<std::byte, 9> result{};
    int failures = 0;
    const auto check = [&failures](bool condition, const char* label) {
        std::cout << label << '=' << condition << '\n';
        if (!condition)
            ++failures;
    };
    check(reader.read(probe::Space::object, start, result) &&
              result[8] == std::byte{8},
          "readwrite_allowed");
    check(!reader.read(probe::Space::image, start, result),
          "outside_image_refused");
    std::array<std::byte, 10> too_big{};
    check(!reader.read(probe::Space::object, start, too_big),
          "oversize_refused");
    check(!reader.read(probe::Space::object, 0xFFFFFFFC, result),
          "overflow_refused");
    DWORD previous = 0;
    if (!VirtualProtect(data + 4096, 4096, PAGE_NOACCESS, &previous))
        return 4;
    check(!reader.read(probe::Space::object, start + 4092, result),
          "cross_unreadable_refused");
    if (!VirtualProtect(data, 4096, PAGE_READONLY | PAGE_GUARD, &previous))
        return 5;
    check(!reader.read(probe::Space::object, start, result), "guard_refused");
    MEMORY_BASIC_INFORMATION region{};
    check(VirtualQuery(allocation, &region, sizeof(region)) == sizeof(region) &&
              (region.Protect & PAGE_GUARD) != 0,
          "guard_unconsumed");
    if (!VirtualProtect(data, 4096, PAGE_EXECUTE_READ, &previous))
        return 6;
    check(!reader.read(probe::Space::object, start, result),
          "executable_refused");
    if (!VirtualProtect(data, 4096, PAGE_READONLY, &previous))
        return 7;
    check(reader.read(probe::Space::object, start, result), "readonly_allowed");
    VirtualFree(allocation, 0, MEM_RELEASE);
    return failures ? 1 : 0;
}

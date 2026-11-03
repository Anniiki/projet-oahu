#include "tdur/ui/vmf_menu_display_selection.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace tdur::ui;
using Profile = tdur::assets::VmfProfile;
using Bytes = std::vector<std::byte>;
std::size_t checks{};
void check(bool value, const char* detail) {
    ++checks;
    if (!value)
        throw std::runtime_error(detail);
}
template <class T, class E> T take(std::expected<T, E> value) {
    check(value.has_value(), "Synthetic VMF fixture construction failed");
    if constexpr (!std::is_void_v<T>)
        return *std::move(value);
}
void integer(Bytes& bytes, std::size_t offset, std::uint32_t value,
             std::size_t width, Profile profile) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto lane = profile == Profile::pc_2_8 ? i : width - i - 1;
        bytes.at(offset + i) =
            std::byte((value >> (8U * static_cast<unsigned>(lane))) & 255U);
    }
}
std::uint32_t crc(const Bytes& bytes) {
    std::uint32_t value = 0xffffffffU;
    for (const auto byte : bytes) {
        value ^= std::to_integer<std::uint32_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xedb88320U : 0U);
    }
    return value ^ 0xffffffffU;
}
VmfProgramPtr program(Profile profile) {
    // Minimal actual VMF root/two frames; no commercial asset or fake pointer.
    Bytes bytes(136);
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'M'};
    bytes[2] = std::byte{'F'};
    bytes[5] = std::byte{2};
    const auto word = [&](std::size_t at, std::uint32_t value) {
        integer(bytes, at, value, 2, profile);
    };
    const auto dword = [&](std::size_t at, std::uint32_t value) {
        integer(bytes, at, value, 4, profile);
    };
    word(6, profile == Profile::pc_2_8 ? 8U : 5U);
    bytes[8] = profile == Profile::pc_2_8 ? std::byte{2} : std::byte{1};
    dword(48, 0x44a00000U);
    dword(56, 0x44340000U);
    word(60, 30);
    word(62, 1);
    word(64, 32);
    dword(72, 39);
    dword(76, 32);
    word(84, 2);
    word(86, 32);
    dword(88, 56);
    dword(92, 32);
    dword(96, 40);
    dword(100, 48);
    dword(104, 1);
    dword(108, 8);
    dword(112, 1);
    dword(116, 8);
    dword(128, 0x54584958U);
    Bytes canonical(bytes.begin(), bytes.begin() + 68);
    if (profile == Profile::xbox_2_5) {
        for (const auto at : {6U, 10U, 60U, 62U, 64U, 66U})
            std::swap(canonical[at], canonical[at + 1]);
        for (const auto at : {44U, 48U, 52U, 56U})
            std::reverse(canonical.begin() + at, canonical.begin() + at + 4);
    }
    dword(68, crc(canonical));
    const auto movie = take(tdur::assets::VmfMovie::parse(bytes, profile));
    return take(VmfProgram::build(movie));
}
void selection(Profile profile) {
    const auto compiled = program(profile);
    auto first = take(VmfDisplayRuntime::create_with_variables(compiled));
    auto second = take(VmfDisplayRuntime::create_with_variables(compiled));
    std::array movies{
        VmfMenuMovie{false, 1, 1, first.get()},
        VmfMenuMovie{true, 2, 1, first.get()},
        VmfMenuMovie{true, 1, 7, first.get()},
        VmfMenuMovie{true, 1, 1, first.get()},
        VmfMenuMovie{true, 1, 1, second.get()},
    };
    check(find_vmf_active_display({}, 1, 0) == nullptr,
          "Known empty registry returns no display");
    check(find_vmf_active_display(movies, 3, 0) == nullptr,
          "Unmatched group returns no display");
    check(find_vmf_active_display(movies, 1, 0) == nullptr,
          "First wrong-kind match is rejected without searching onward");
    check(find_vmf_active_display(movies, 1, 1) == first.get(),
          "Ordinal counts active group matches, including wrong kinds");
    check(find_vmf_active_display(movies, 1, 2) == second.get(),
          "Second usable display retains its actual owned identity");
    check(find_vmf_active_display(movies, 1,
                                  std::numeric_limits<std::uint32_t>::max()) ==
              second.get(),
          "Excess ordinal uses actual selector last-match fallback");
    movies[4].kind = 0;
    check(find_vmf_active_display(movies, 1, 99) == nullptr,
          "Wrong-kind fallback does not fall back again to a usable movie");
    movies[4].kind = 1;
    movies[4].display = nullptr;
    check(find_vmf_active_display(movies, 1, 99) == nullptr,
          "Known NULL display field stays NULL");
    movies[4].active = false;
    check(find_vmf_active_display(movies, 1, 99) == first.get(),
          "Inactive movies are not fallback candidates");
    movies[2].kind = 1;
    check(find_vmf_active_display(movies, 1, 0) == first.get(),
          "First caller lookup sees current first display");
    movies[2].display = second.get();
    auto* selected = find_vmf_active_display(movies, 1, 0);
    check(selected == second.get(),
          "Second caller lookup re-reads changed registry without caching");
    take(write_vmf_host_integer(*selected, "/:selected", 1));
    const auto untouched =
        take(first->variable_dictionary()->find(0, "selected"));
    const auto written =
        take(second->variable_dictionary()->find(0, "selected"));
    check(!untouched && written &&
              written->value.kind == VmfVariableKind::owned_text &&
              std::string_view(written->value.text.data()) == "1",
          "Actual host setter publishes only to the display selected second");
}
} // namespace
int main() {
    try {
        selection(Profile::pc_2_8);
        selection(Profile::xbox_2_5);
        std::cout << checks << " menu-display checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

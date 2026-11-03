#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>

namespace tdur::ui {

enum class VmfNumberFormatError { negative_precision };

// Owns its NUL-terminated byte string, independently of every other result.
// The future VM must model the native pop-text shared scratch separately.
class VmfNumberText {
  public:
    [[nodiscard]] std::string_view text() const& noexcept {
        return {characters_.data(), length_};
    }
    std::string_view text() const&& = delete;
    [[nodiscard]] const char* c_str() const& noexcept {
        return characters_.data();
    }
    const char* c_str() const&& = delete;

  private:
    friend std::expected<VmfNumberText, VmfNumberFormatError>
    vmf_number_to_string(float, int) noexcept;

    std::array<char, 32> characters_{};
    std::uint8_t length_{};
};

// Reconstructs secondary PC 0x00442330; see the evidence document for scope.
// Uses explicit SSE binary32 operations and the caller's MXCSR. No locale,
// allocation, errno access or floating-point control change is performed.
// Precision >4 is capped to 4, including the native pop-text argument 12.
// Negative precision is explicitly refused: the native unchecked arithmetic
// can produce an unbounded write. Nonfinite/overflow conversions preserve
// SSE's INT32_MIN sentinel rather than relying on an undefined C++ cast.
[[nodiscard]] std::expected<VmfNumberText, VmfNumberFormatError>
vmf_number_to_string(float value, int precision = 12) noexcept;

} // namespace tdur::ui

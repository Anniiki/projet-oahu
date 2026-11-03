#pragma once

#include "tdur/ui/vmf_menu_finalizers.hpp"

namespace tdur::ui {

// Owned logical data, not a native ABI or a wire format. Offset names preserve
// provenance. Value initialization supplies owned storage; the native
// constructor is applied explicitly and preserves title tails/opaque bytes.
struct VmfNotificationRecord {
    std::uint32_t field_00{}, field_04{};
    std::uint16_t field_08{};
    std::array<char, 0x90> title_0a{};
    std::array<char, 0x800> body_9a{};
    std::uint32_t language_89c{};
    std::array<std::uint32_t, 0x28> descriptor_8a0{};
    std::uint32_t descriptor_940{};
    std::uint32_t field_948{}, field_94c{};
    std::uint32_t field_950{}, field_954{}, field_958{};
    std::array<char, 16> text_95c{};
    std::array<char, 32> text_96c{};
    std::uint8_t flag_98c{}, flag_98d{};
    std::uint32_t field_990{};
    // Native89A..89B,944..947,98E..98F,994..997, in that order.
    std::array<std::byte, 12> opaque{};
    bool operator==(const VmfNotificationRecord&) const = default;
};

enum class VmfNotificationRecordErrorCode {
    missing_language_source,
    language_dependency,
    language_reference,
    null_string,
    string_limit,
    overlapping_storage,
    descriptor_range,
};
struct VmfNotificationRecordError {
    VmfNotificationRecordErrorCode code;
    std::string_view detail;
    std::optional<VmfMenuFinalizerError> language_error{};
};

class VmfNotificationLanguageSource {
  public:
    virtual ~VmfNotificationLanguageSource() = default;
    // Actual898B40 service, including cache/units effects. Returned backing
    // must stay alive without relocation across BOTH calls. Its bytes may
    // change during the second invocation; no content snapshot is implied.
    [[nodiscard]] virtual std::expected<std::span<const char>,
                                        VmfNotificationRecordError>
    lookup() noexcept = 0;
};

// Shared sequence used by8956C0 and895880: two complete invocations, then
// signed-byte reads from the live first/second backing. No cache shortcut.
[[nodiscard]] std::expected<std::uint32_t, VmfNotificationRecordError>
resolve_vmf_notification_language(
    VmfNotificationLanguageSource& source) noexcept;

// Concrete implementation over the existing owned cache/units engine.
// Complete game/event/movie bindings must be supplied when cache is empty.
// Missing dependencies are errors preserving the real effects already made;
// no successful placeholder cache refresh is substituted.
class VmfNotificationLocaleLanguage final
    : public VmfNotificationLanguageSource {
  public:
    VmfNotificationLocaleLanguage(VmfMenuLocaleState& locale,
                                  const VmfMenuFinalizerInputs& inputs,
                                  VmfTargetLookupLimits limits = {}) noexcept;
    [[nodiscard]] std::expected<std::span<const char>,
                                VmfNotificationRecordError>
    lookup() noexcept override;

  private:
    VmfMenuLocaleState& locale_;
    const VmfMenuFinalizerInputs& inputs_;
    VmfTargetLookupLimits limits_;
    bool faulted_{};
};

struct VmfNotificationRecordArgs {
    // All scalar/view metadata are fixed call arguments. An absent optional
    // means a known NULL, never an unresolved input. String views supply
    // bounded C-string prefixes, including any embedded NUL.
    std::uint32_t field_00{}, field_04{}, field_08{};
    std::optional<std::string_view> title;
    std::optional<std::string_view> body;
    std::uint32_t language{};
    std::optional<std::span<const std::uint32_t>> descriptor;
    std::uint32_t descriptor_940{};
    std::uint32_t field_948{}, field_94c{};
    std::optional<std::string_view> text_96c, text_95c;
    std::uint32_t field_950{}, field_954{}, field_958{};
    std::uint32_t flag_98c{}, flag_98d{};
};

// 88E780: partial initialization, including940=FFFFFFFF. Not a whole-object
// memset. No pointer/register ABI is reproduced by these C++ entry points.
void initialize_vmf_notification_record(VmfNotificationRecord& record) noexcept;

// 8956C0: initializes again, resolves language, then writes fields/copies in
// native order. LanguageFFFFFFFF invokes the complete source TWICE and reads
// both signed bytes only AFTER the second call. Body NUL is forced at native
// 809, i.e. body[1903], not its last byte. NULL body/descriptor skip their
// conditional writes. Small strings must fit32/16 with their NUL; no invented
// truncation. Capped copies permit disjoint or identical source/destination;
// unsafe forward overlap of small copies is rejected. Errors preserve prefix
// effects; an error is not a suspended invocation to retry automatically.
// Keep record, sources, and services alive, nonrelocated and serialized.
[[nodiscard]] std::expected<void, VmfNotificationRecordError>
populate_vmf_notification_record(
    VmfNotificationRecord& record, VmfNotificationRecordArgs args,
    VmfNotificationLanguageSource* language_source = nullptr) noexcept;

// 895800: capture scalar arguments, keep live views into source fields, then
// invoke8956C0. It reconstructs the destination, rather than copying opaque
// bytes or field990. Source languageFFFFFFFF still performs language lookup.
// Self-copy captures old scalars but reads buffers AFTER reinitialization.
[[nodiscard]] std::expected<void, VmfNotificationRecordError>
copy_vmf_notification_record(
    VmfNotificationRecord& destination, const VmfNotificationRecord& source,
    VmfNotificationLanguageSource* language_source = nullptr) noexcept;

} // namespace tdur::ui

#include "tdur/ui/vmf_notification_record.hpp"

#include <algorithm>

namespace tdur::ui {
namespace {
VmfNotificationRecordError error(VmfNotificationRecordErrorCode code,
                                 std::string_view message) noexcept {
    return {code, message, {}};
}
bool overlap(const void* a, std::size_t as, const void* b,
             std::size_t bs) noexcept {
    if (!as || !bs)
        return false;
    const auto first = reinterpret_cast<std::uintptr_t>(a);
    const auto second = reinterpret_cast<std::uintptr_t>(b);
    return first <= second ? second - first < as : first - second < bs;
}
std::uint32_t signed_byte_word(char value) noexcept {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x80U ? byte : (0xffffff00U | byte);
}
template <std::size_t N>
std::expected<void, VmfNotificationRecordError>
capped_copy(std::array<char, N>& output, std::string_view input) noexcept {
    const auto window = input.substr(0, N);
    const auto nul = window.find('\0');
    const auto count = nul == std::string_view::npos ? window.size() : nul;
    const auto read_width = count + (count < N ? 1 : 0);
    if (input.data() != output.data() &&
        overlap(input.data(), read_width, output.data(), N))
        return std::unexpected(
            error(VmfNotificationRecordErrorCode::overlapping_storage,
                  "Capped string source overlaps its destination"));
    if (count && input.data() != output.data())
        std::copy_n(input.begin(), count, output.begin());
    std::fill(output.begin() + static_cast<std::ptrdiff_t>(count), output.end(),
              '\0');
    return {};
}
template <std::size_t N>
std::expected<void, VmfNotificationRecordError>
small_copy(std::array<char, N>& output,
           std::optional<std::string_view> input) noexcept {
    if (!input)
        return std::unexpected(
            error(VmfNotificationRecordErrorCode::null_string,
                  "Required small string is NULL"));
    const auto window = input->substr(0, N);
    const auto nul = window.find('\0');
    const auto count = nul == std::string_view::npos ? window.size() : nul;
    if (count == N)
        return std::unexpected(
            error(VmfNotificationRecordErrorCode::string_limit,
                  "Small string would exceed its field"));
    if (overlap(input->data(), count + 1, output.data(), N) &&
        reinterpret_cast<std::uintptr_t>(output.data()) >
            reinterpret_cast<std::uintptr_t>(input->data()))
        return std::unexpected(error(
            VmfNotificationRecordErrorCode::overlapping_storage,
            "Forward small-string overlap would overwrite its terminator"));
    // The original byte loop supports equal/backward overlapping storage.
    for (std::size_t i = 0; i < count; ++i)
        output[i] = (*input)[i];
    output[count] = '\0';
    return {};
}
} // namespace

std::expected<std::uint32_t, VmfNotificationRecordError>
resolve_vmf_notification_language(
    VmfNotificationLanguageSource& source) noexcept {
    const auto first = source.lookup();
    if (!first)
        return std::unexpected(first.error());
    const auto second = source.lookup();
    if (!second)
        return std::unexpected(second.error());
    // Native reads neither pointer until both calls have returned.
    if (first->empty() || second->size() < 2)
        return std::unexpected(
            error(VmfNotificationRecordErrorCode::language_reference,
                  "Selected language storage does not cover the native reads"));
    const auto high = signed_byte_word((*first)[0]);
    const auto low = signed_byte_word((*second)[1]);
    return (high << 8U) | low;
}

VmfNotificationLocaleLanguage::VmfNotificationLocaleLanguage(
    VmfMenuLocaleState& locale, const VmfMenuFinalizerInputs& inputs,
    VmfTargetLookupLimits limits) noexcept
    : locale_(locale), inputs_(inputs), limits_(limits) {}
std::expected<std::span<const char>, VmfNotificationRecordError>
VmfNotificationLocaleLanguage::lookup() noexcept {
    if (faulted_)
        return std::unexpected(error(
            VmfNotificationRecordErrorCode::language_dependency,
            "Language service is faulted; an incomplete call is not retried"));
    auto context = VmfMenuFinalizerContext::refresh_language(locale_, limits_);
    const auto result = context.run_language(locale_, inputs_);
    if (!result) {
        faulted_ = true;
        return std::unexpected(VmfNotificationRecordError{
            VmfNotificationRecordErrorCode::language_dependency,
            "Language cache service failed", result.error()});
    }
    if (result->status != VmfMenuFinalizerStatus::language_ready) {
        faulted_ = true;
        return std::unexpected(error(
            VmfNotificationRecordErrorCode::language_dependency,
            "Language service requires complete owned dependency bindings"));
    }
    return locale_.cached_storage();
}

void initialize_vmf_notification_record(
    VmfNotificationRecord& record) noexcept {
    record.field_00 = 0;
    record.field_04 = 0;
    record.field_08 = 0;
    record.title_0a[0] = '\0';
    record.body_9a.fill('\0');
    record.descriptor_8a0.fill(0);
    record.field_948 = 0;
    record.field_94c = 0;
    record.field_990 = 0;
    record.descriptor_940 = 0xffffffffU;
    record.text_96c.fill('\0');
    record.text_95c.fill('\0');
    record.field_950 = 0;
    record.field_954 = 0;
    record.field_958 = 0;
    record.flag_98c = 0;
    record.flag_98d = 0;
    record.language_89c = 0;
}

std::expected<void, VmfNotificationRecordError>
populate_vmf_notification_record(
    VmfNotificationRecord& record, VmfNotificationRecordArgs args,
    VmfNotificationLanguageSource* language_source) noexcept {
    initialize_vmf_notification_record(record);
    if (args.language == 0xffffffffU) {
        if (!language_source)
            return std::unexpected(
                error(VmfNotificationRecordErrorCode::missing_language_source,
                      "Automatic language requires its concrete source"));
        const auto language =
            resolve_vmf_notification_language(*language_source);
        if (!language)
            return std::unexpected(language.error());
        record.language_89c = *language;
    } else
        record.language_89c = args.language;
    record.field_00 = args.field_00;
    record.field_04 = args.field_04;
    record.field_08 = static_cast<std::uint16_t>(args.field_08);
    if (!args.title)
        return std::unexpected(
            error(VmfNotificationRecordErrorCode::null_string,
                  "Required title is NULL"));
    const auto title = capped_copy(record.title_0a, *args.title);
    if (!title)
        return title;
    record.title_0a.back() = '\0';
    if (args.body) {
        const auto body = capped_copy(record.body_9a, *args.body);
        if (!body)
            return body;
        record.body_9a[0x809 - 0x9a] = '\0';
    }
    if (args.descriptor) {
        if (args.descriptor->size() < record.descriptor_8a0.size())
            return std::unexpected(
                error(VmfNotificationRecordErrorCode::descriptor_range,
                      "Descriptor source does not cover forty DWORDs"));
        // REP MOVSD copies forward, including valid overlapping words.
        for (std::size_t i = 0; i < record.descriptor_8a0.size(); ++i)
            record.descriptor_8a0[i] = (*args.descriptor)[i];
        record.descriptor_940 = args.descriptor_940;
    }
    record.field_948 = args.field_948;
    record.field_94c = args.field_94c;
    const auto first_text = small_copy(record.text_96c, args.text_96c);
    if (!first_text)
        return first_text;
    const auto second_text = small_copy(record.text_95c, args.text_95c);
    if (!second_text)
        return second_text;
    record.field_950 = args.field_950;
    record.field_954 = args.field_954;
    record.field_958 = args.field_958;
    record.flag_98c = static_cast<std::uint8_t>(args.flag_98c);
    record.flag_98d = static_cast<std::uint8_t>(args.flag_98d);
    return {};
}

std::expected<void, VmfNotificationRecordError> copy_vmf_notification_record(
    VmfNotificationRecord& destination, const VmfNotificationRecord& source,
    VmfNotificationLanguageSource* language_source) noexcept {
    VmfNotificationRecordArgs args;
    args.flag_98d = source.flag_98d;
    args.flag_98c = source.flag_98c;
    args.field_958 = source.field_958;
    args.field_954 = source.field_954;
    args.field_950 = source.field_950;
    args.text_95c =
        std::string_view{source.text_95c.data(), source.text_95c.size()};
    args.text_96c =
        std::string_view{source.text_96c.data(), source.text_96c.size()};
    args.field_94c = source.field_94c;
    args.field_948 = source.field_948;
    args.descriptor_940 = source.descriptor_940;
    args.descriptor = std::span<const std::uint32_t>{source.descriptor_8a0};
    args.title =
        std::string_view{source.title_0a.data(), source.title_0a.size()};
    args.field_08 = source.field_08;
    args.body = std::string_view{source.body_9a.data(), source.body_9a.size()};
    args.field_04 = source.field_04;
    args.field_00 = source.field_00;
    args.language = source.language_89c;
    return populate_vmf_notification_record(destination, args, language_source);
}

} // namespace tdur::ui

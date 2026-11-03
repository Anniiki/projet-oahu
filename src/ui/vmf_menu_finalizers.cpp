#include "tdur/ui/vmf_menu_finalizers.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <utility>

namespace tdur::ui {
namespace {
VmfMenuFinalizerError error(VmfMenuFinalizerErrorCode code,
                            std::string_view detail) noexcept {
    return {code, detail, {}};
}
std::string_view prefix(std::string_view value) noexcept {
    return value.substr(0, value.find('\0'));
}
std::expected<void, VmfMenuFinalizerError>
copy_code(std::array<char, 64>& output, std::string_view value) noexcept {
    value = prefix(value);
    if (value.size() >= output.size())
        return std::unexpected(error(VmfMenuFinalizerErrorCode::locale_limit,
                                     "Locale exceeds modern owned storage"));
    // Preserve bytes beyond the first NUL, like the original copy loop.
    if (!value.empty())
        std::memmove(output.data(), value.data(), value.size());
    output[value.size()] = '\0';
    return {};
}
std::string_view code_view(const std::array<char, 64>& value) noexcept {
    return {value.data(),
            static_cast<std::size_t>(
                std::find(value.begin(), value.end(), '\0') - value.begin())};
}
std::uint32_t little_word(std::span<const std::byte> bytes,
                          std::size_t at) noexcept {
    std::uint32_t result{};
    for (unsigned i = 0; i < 4; ++i)
        result |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8 * i);
    return result;
}
std::expected<std::uint64_t, VmfMenuFinalizerError>
catalog_id(std::string_view text, VmfMenuDataLimits limits) noexcept {
    if (text.empty() || text.size() > limits.max_reference_bytes ||
        std::any_of(text.begin(), text.end(),
                    [](char c) { return c < '0' || c > '9'; }))
        return std::unexpected(
            error(VmfMenuFinalizerErrorCode::invalid_reference,
                  "Language catalog ID is outside the decimal uint64 domain"));
    std::uint64_t value{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::unexpected(
            error(VmfMenuFinalizerErrorCode::invalid_reference,
                  "Language catalog ID exceeds uint64"));
    return value;
}
} // namespace

std::uint32_t vmf_menu_language_index(std::string_view code) noexcept {
    code = prefix(code);
    if (code == "fr")
        return 1;
    if (code == "it")
        return 2;
    if (code == "ge")
        return 3;
    if (code == "sp")
        return 4;
    if (code == "ja")
        return 5;
    if (code == "ko")
        return 6;
    if (code == "ch")
        return 7;
    if (code == "ru")
        return 8;
    if (code == "po")
        return 9;
    return 0; // us, gb and unknown codes.
}
std::uint32_t vmf_menu_unit_mode(std::string_view code) noexcept {
    return code.size() >= 2 && code[0] == 'u' && code[1] == 's' ? 1U : 0U;
}

std::expected<VmfMenuLanguageTable, VmfMenuFinalizerError>
VmfMenuLanguageTable::from_bindings(
    std::array<VmfMenuLanguageBinding, 5> bindings,
    VmfMenuDataLimits limits) noexcept {
    std::size_t bytes{};
    for (const auto& binding : bindings) {
        if (!binding.text)
            continue;
        const auto extent = binding.text->extent_bytes();
        if (!extent)
            return std::unexpected(
                error(VmfMenuFinalizerErrorCode::invalid_reference,
                      "Language text has no owned backing"));
        if (extent > limits.max_linked_bytes - bytes)
            return std::unexpected(
                error(VmfMenuFinalizerErrorCode::resource_limit,
                      "Language text backing quota exceeded"));
        bytes += extent;
    }
    VmfMenuLanguageTable table;
    table.bindings_ = std::move(bindings);
    return table;
}

std::expected<VmfMenuLanguageTable, VmfMenuFinalizerError>
VmfMenuLanguageTable::from_catalog(std::span<const std::byte> records,
                                   const assets::DatabaseStrings& catalog,
                                   VmfMenuDataLimits limits) noexcept {
    if (records.size() != 80)
        return std::unexpected(error(
            VmfMenuFinalizerErrorCode::invalid_catalog,
            "Language source table requires exactly five16-byte records"));
    if (catalog.entries().size() > limits.max_catalog_entries)
        return std::unexpected(error(VmfMenuFinalizerErrorCode::resource_limit,
                                     "Language catalog entry quota exceeded"));
    std::array<std::uint64_t, 5> ids{};
    std::array<VmfMenuLanguageBinding, 5> bindings;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ids[i] = little_word(records, i * 16) |
                 (static_cast<std::uint64_t>(little_word(records, i * 16 + 4))
                  << 32);
        bindings[i].language = little_word(records, i * 16 + 8);
    }
    for (const auto& entry : catalog.entries()) {
        const auto id = catalog_id(entry.reference_token, limits);
        if (!id)
            return std::unexpected(id.error());
        std::optional<VmfTextReference> first_text;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (bindings[i].text || ids[i] != *id)
                continue;
            if (!first_text) {
                const auto text =
                    VmfLinkedText::copy(entry.value, limits.max_linked_bytes);
                if (!text)
                    return std::unexpected(error(
                        text.error().code ==
                                VmfStackErrorCode::allocation_failure
                            ? VmfMenuFinalizerErrorCode::allocation_failure
                            : VmfMenuFinalizerErrorCode::resource_limit,
                        "Language catalog text rejected"));
                first_text = VmfValue::linked(*text).text_reference();
            }
            bindings[i].text = first_text;
        }
    }
    return from_bindings(std::move(bindings), limits);
}
const VmfMenuLanguageBinding&
VmfMenuLanguageTable::select(std::uint32_t language) const noexcept {
    for (const auto& binding : bindings_)
        if (binding.language == language)
            return binding;
    return bindings_[0];
}

std::expected<VmfMenuLocaleState, VmfMenuFinalizerError>
VmfMenuLocaleState::create(std::string_view source, std::string_view cached,
                           std::uint32_t unit_mode) noexcept {
    VmfMenuLocaleState result;
    const auto first = result.set_source(source);
    if (!first)
        return std::unexpected(first.error());
    const auto second = result.set_cached(cached);
    if (!second)
        return std::unexpected(second.error());
    result.unit_mode_ = unit_mode;
    return result;
}
std::string_view VmfMenuLocaleState::source() const noexcept {
    return code_view(source_);
}
std::string_view VmfMenuLocaleState::cached() const noexcept {
    return code_view(cached_);
}
std::span<const char, 64> VmfMenuLocaleState::cached_storage() const noexcept {
    return cached_;
}
std::expected<void, VmfMenuFinalizerError>
VmfMenuLocaleState::set_source(std::string_view value) noexcept {
    return copy_code(source_, value);
}
std::expected<void, VmfMenuFinalizerError>
VmfMenuLocaleState::set_cached(std::string_view value) noexcept {
    return copy_code(cached_, value);
}
std::uint32_t VmfMenuLocaleState::unit_mode() const noexcept {
    return unit_mode_;
}
void VmfMenuLocaleState::set_unit_mode(std::uint32_t value) noexcept {
    unit_mode_ = value;
}

std::expected<bool, VmfMenuFinalizerError>
remove_vmf_menu_callback(VmfMenuEventQueue& queue,
                         std::uintptr_t callback) noexcept {
    if (queue.count > queue.entries.size())
        return std::unexpected(
            error(VmfMenuFinalizerErrorCode::resource_limit,
                  "Callback count exceeds modern owned event storage"));
    for (std::size_t i = 0; i < queue.count; ++i) {
        if (queue.entries[i].callback != callback)
            continue;
        for (std::size_t j = i + 1; j < queue.count; ++j)
            queue.entries[j - 1] = queue.entries[j];
        auto& vacated = queue.entries[queue.count - 1];
        vacated.payload[0] = std::byte{};
        vacated.callback = 0;
        --queue.count;
        return true;
    }
    return false;
}
VmfMenuFinalizerContext::VmfMenuFinalizerContext(
    VmfMenuBindingContext&& rows, VmfDisplayRuntime& display,
    VmfMenuLocaleState& locale, VmfMenuLanguageTable languages,
    VmfTargetLookupLimits target_limits) noexcept
    : rows_(std::move(rows)), display_(&display), root_(display.root()),
      locale_(&locale), languages_(std::move(languages)),
      target_limits_(target_limits) {}

VmfMenuFinalizerContext::VmfMenuFinalizerContext(
    VmfMenuLocaleState& locale, VmfTargetLookupLimits limits) noexcept
    : locale_(&locale), target_limits_(limits), language_only_(true) {
    state_.phase = VmfMenuFinalizerPhase::language_cache;
}
VmfMenuFinalizerContext VmfMenuFinalizerContext::refresh_language(
    VmfMenuLocaleState& locale, VmfTargetLookupLimits limits) noexcept {
    return VmfMenuFinalizerContext{locale, limits};
}

std::expected<VmfMenuFinalizerContext, VmfMenuFinalizerError>
VmfMenuFinalizerContext::after_rows(VmfMenuBindingContext&& rows,
                                    VmfDisplayRuntime& display,
                                    VmfMenuLocaleState& locale,
                                    VmfMenuLanguageTable languages) noexcept {
    if (!rows.ready_for_finalization(display))
        return std::unexpected(error(
            VmfMenuFinalizerErrorCode::rows_incomplete,
            "Menu rows are unfinished, faulted, moved or bound elsewhere"));
    const auto live = display.inspect_instance(display.root());
    if (!live)
        return std::unexpected(
            error(VmfMenuFinalizerErrorCode::invalid_context,
                  "Finalizer requires the live bound display root"));
    return VmfMenuFinalizerContext{std::move(rows), display, locale,
                                   std::move(languages), rows.target_limits_};
}

VmfMenuFinalizerContext::VmfMenuFinalizerContext(
    VmfMenuFinalizerContext&& other) noexcept
    : rows_(std::move(other.rows_)),
      display_(std::exchange(other.display_, nullptr)), root_(other.root_),
      locale_(std::exchange(other.locale_, nullptr)),
      languages_(std::move(other.languages_)),
      target_limits_(other.target_limits_), state_(other.state_),
      pending_mode_(other.pending_mode_),
      pending_metric_(other.pending_metric_),
      game_(std::exchange(other.game_, nullptr)),
      events_(std::exchange(other.events_, nullptr)),
      callbacks_(other.callbacks_), language_only_(other.language_only_) {
    other.state_.faulted = true;
}
VmfMenuFinalizerContext&
VmfMenuFinalizerContext::operator=(VmfMenuFinalizerContext&& other) noexcept {
    if (this != &other) {
        rows_ = std::move(other.rows_);
        display_ = std::exchange(other.display_, nullptr);
        root_ = other.root_;
        locale_ = std::exchange(other.locale_, nullptr);
        languages_ = std::move(other.languages_);
        target_limits_ = other.target_limits_;
        state_ = other.state_;
        pending_mode_ = other.pending_mode_;
        pending_metric_ = other.pending_metric_;
        game_ = std::exchange(other.game_, nullptr);
        events_ = std::exchange(other.events_, nullptr);
        callbacks_ = other.callbacks_;
        language_only_ = other.language_only_;
        other.state_.faulted = true;
    }
    return *this;
}
VmfMenuFinalizerSnapshot VmfMenuFinalizerContext::snapshot() const noexcept {
    return state_;
}

std::expected<VmfMenuFinalizerReport, VmfMenuFinalizerError>
VmfMenuFinalizerContext::run(VmfDisplayRuntime& display,
                             VmfMenuLocaleState& locale,
                             const VmfMenuFinalizerInputs& inputs,
                             std::size_t step_budget) noexcept {
    return run_impl(&display, locale, inputs, step_budget);
}
std::expected<VmfMenuFinalizerReport, VmfMenuFinalizerError>
VmfMenuFinalizerContext::run_language(VmfMenuLocaleState& locale,
                                      const VmfMenuFinalizerInputs& inputs,
                                      std::size_t step_budget) noexcept {
    return run_impl(nullptr, locale, inputs, step_budget);
}
std::expected<VmfMenuFinalizerReport, VmfMenuFinalizerError>
VmfMenuFinalizerContext::run_impl(VmfDisplayRuntime* display,
                                  VmfMenuLocaleState& locale,
                                  const VmfMenuFinalizerInputs& inputs,
                                  std::size_t step_budget) noexcept {
    const auto fail = [&](VmfMenuFinalizerError reason) {
        state_.faulted = true;
        return std::unexpected(reason);
    };
    if (state_.faulted || locale_ != &locale ||
        (language_only_
             ? display != nullptr
             : (!display || display_ != display || display->root() != root_ ||
                !display->inspect_instance(root_))))
        return fail(error(VmfMenuFinalizerErrorCode::invalid_context,
                          "Finalizer context is faulted or mismatched"));
    if (game_ &&
        (!inputs.game || *inputs.game != game_ || inputs.events != events_ ||
         !inputs.callback_keys || *inputs.callback_keys != callbacks_))
        return fail(error(VmfMenuFinalizerErrorCode::invalid_context,
                          "Active unit pass changed game/event identities"));
    std::size_t executed{};
    const auto report = [&](VmfMenuFinalizerStatus status) {
        return VmfMenuFinalizerReport{status, state_, executed};
    };
    const auto advance = [&] {
        ++executed;
        ++state_.steps;
    };
    const auto host_failure = [&](const VmfHostVariableError& reason) {
        return fail(VmfMenuFinalizerError{
            VmfMenuFinalizerErrorCode::host_variables,
            "Menu finalization host write rejected", reason});
    };
    while (state_.phase != VmfMenuFinalizerPhase::finished ||
           state_.unit_step != VmfMenuUnitStep::inactive) {
        if (executed == step_budget)
            return report(VmfMenuFinalizerStatus::step_limit);
        if (state_.unit_step != VmfMenuUnitStep::inactive) {
            switch (state_.unit_step) {
            case VmfMenuUnitStep::store_mode:
                locale.set_unit_mode(pending_mode_);
                ++state_.unit_passes;
                state_.unit_step = VmfMenuUnitStep::game_binding;
                break;
            case VmfMenuUnitStep::game_binding:
                if (!inputs.game)
                    return report(VmfMenuFinalizerStatus::needs_game_state);
                if (*inputs.game == nullptr) {
                    state_.unit_step = VmfMenuUnitStep::metric_first;
                    break;
                }
                if (!inputs.events || !inputs.callback_keys)
                    return report(VmfMenuFinalizerStatus::needs_event_services);
                game_ = *inputs.game;
                events_ = inputs.events;
                callbacks_ = *inputs.callback_keys;
                state_.unit_step = VmfMenuUnitStep::remove_first;
                break;
            case VmfMenuUnitStep::remove_first:
            case VmfMenuUnitStep::remove_second:
            case VmfMenuUnitStep::remove_third: {
                const auto callback =
                    state_.unit_step == VmfMenuUnitStep::remove_third
                        ? callbacks_.callback_6e77c0
                        : callbacks_.callback_6e7910;
                const auto removed =
                    remove_vmf_menu_callback(*events_, callback);
                if (!removed)
                    return fail(removed.error());
                ++state_.callback_remove_calls;
                state_.unit_step =
                    state_.unit_step == VmfMenuUnitStep::remove_first
                        ? VmfMenuUnitStep::clear_3263
                    : state_.unit_step == VmfMenuUnitStep::remove_second
                        ? VmfMenuUnitStep::remove_third
                        : VmfMenuUnitStep::clear_3262;
                break;
            }
            case VmfMenuUnitStep::clear_3263:
                game_->flag_3263 = 0;
                state_.unit_step = VmfMenuUnitStep::remove_second;
                break;
            case VmfMenuUnitStep::clear_3262:
                game_->flag_3262 = 0;
                state_.unit_step = VmfMenuUnitStep::parameters;
                break;
            case VmfMenuUnitStep::parameters:
                game_->parameters_current = locale.unit_mode() == 0
                                                ? game_->parameters_zero
                                                : game_->parameters_nonzero;
                state_.unit_step = VmfMenuUnitStep::speed_label;
                break;
            case VmfMenuUnitStep::speed_label:
                if (game_->speed_display) {
                    const auto& label =
                        game_->speed_labels[locale.unit_mode() == 1 ? 0 : 1];
                    if (!label)
                        return report(
                            VmfMenuFinalizerStatus::needs_speed_label);
                    if (!label->extent_bytes())
                        return fail(
                            error(VmfMenuFinalizerErrorCode::invalid_reference,
                                  "Speed unit text has no owned backing"));
                    const auto written = write_vmf_host_text(
                        *game_->speed_display, "/:SpeedUnit", label->text(),
                        target_limits_);
                    if (!written)
                        return host_failure(written.error());
                    ++state_.speed_writes;
                }
                game_ = nullptr;
                events_ = nullptr;
                state_.unit_step = VmfMenuUnitStep::metric_first;
                break;
            case VmfMenuUnitStep::metric_first: {
                if (!inputs.movies)
                    return report(VmfMenuFinalizerStatus::needs_movie_registry);
                if (inputs.movies->size() > 100'000)
                    return fail(error(
                        VmfMenuFinalizerErrorCode::resource_limit,
                        "Active movie registry exceeds modern scan limit"));
                const auto* first = find_vmf_active_menu(*inputs.movies, 1, 0);
                if (!first || first->kind != 1 || !first->display) {
                    state_.unit_step = VmfMenuUnitStep::inactive;
                    break;
                }
                pending_metric_ = locale.unit_mode() == 1 ? 0 : 1;
                state_.unit_step = VmfMenuUnitStep::metric_second;
                break;
            }
            case VmfMenuUnitStep::metric_second: {
                if (!inputs.movies)
                    return report(VmfMenuFinalizerStatus::needs_movie_registry);
                if (inputs.movies->size() > 100'000)
                    return fail(error(
                        VmfMenuFinalizerErrorCode::resource_limit,
                        "Active movie registry exceeds modern scan limit"));
                const auto* second = find_vmf_active_menu(*inputs.movies, 1, 0);
                if (!second || second->kind != 1 || !second->display)
                    return fail(
                        error(VmfMenuFinalizerErrorCode::missing_metric_display,
                              "Second active movie lookup would pass NULL to "
                              "setter"));
                const auto written =
                    write_vmf_host_integer(*second->display, "/:state_Metric",
                                           pending_metric_, target_limits_);
                if (!written)
                    return host_failure(written.error());
                ++state_.metric_writes;
                state_.unit_step = VmfMenuUnitStep::inactive;
                break;
            }
            case VmfMenuUnitStep::inactive:
                break;
            }
            advance();
            continue;
        }
        switch (state_.phase) {
        case VmfMenuFinalizerPhase::binding_cache:
        case VmfMenuFinalizerPhase::language_cache: {
            const bool binding_phase =
                state_.phase == VmfMenuFinalizerPhase::binding_cache;
            if (locale.cached().empty()) {
                const auto copied = locale.set_cached(locale.source());
                if (!copied)
                    return fail(copied.error());
                pending_mode_ = vmf_menu_unit_mode(locale.cached());
                state_.unit_step = VmfMenuUnitStep::store_mode;
            }
            state_.phase = binding_phase ? VmfMenuFinalizerPhase::language_cache
                           : language_only_
                               ? VmfMenuFinalizerPhase::finished
                               : VmfMenuFinalizerPhase::language_write;
            break;
        }
        case VmfMenuFinalizerPhase::language_write: {
            const auto& binding =
                languages_.select(vmf_menu_language_index(locale.cached()));
            if (!binding.text)
                return fail(
                    error(VmfMenuFinalizerErrorCode::reference_not_found,
                          "Selected language ID has no catalog text"));
            if (!binding.text->extent_bytes())
                return fail(error(VmfMenuFinalizerErrorCode::invalid_reference,
                                  "Language text has no owned backing"));
            const auto written = write_vmf_host_linked_text(
                *display, "/:WhatLanguage", *binding.text, target_limits_);
            if (!written)
                return host_failure(written.error());
            ++state_.language_writes;
            state_.phase = VmfMenuFinalizerPhase::finished;
            break;
        }
        case VmfMenuFinalizerPhase::finished:
            break;
        }
        advance();
    }
    return report(language_only_ ? VmfMenuFinalizerStatus::language_ready
                                 : VmfMenuFinalizerStatus::bindings_finished);
}

} // namespace tdur::ui

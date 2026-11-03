#pragma once

#include "tdur/ui/vmf_notification_text.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

enum class VmfNotificationLookupError { unterminated_key, missing_token };

// 716C40: first case-sensitive strncmp match, using the stored key's length.
// Empty keys are skipped. Keys created by the real binder include C2 A7;
// imported keys retain their literal prefix semantics. A view end implies NUL.
// NULL token is only needed when a nonempty key is compared; an all-empty
// table returns no match without reading it. An encountered unterminated
// 18-byte key is a modern refusal before comparison, not a native result.
// The typed slot index replaces the original pointer to its64-byte value.
// Values are never inspected or copied, and may be nonterminated. Preserve
// the table/lifetimes and serialize accesses until the caller uses that slot.
// No allocation or mutations. A match stops before inspecting later slots.
[[nodiscard]] std::expected<std::optional<std::size_t>,
                            VmfNotificationLookupError>
find_vmf_notification_text(const VmfNotificationTextParameters& parameters,
                           std::optional<std::string_view> token) noexcept;

} // namespace tdur::ui

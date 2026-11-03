#pragma once

#include "tdur/input/native_input_state.hpp"

#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace tdur::input {

// Names queried by 0x980550, in native order. These names do not establish
// device enumeration, SDL dispatch, calibration or menu navigation behavior.
inline constexpr std::array<std::string_view, 7> native_selection_names{
    "LEFT", "UP", "WHEEL_UP", "WHEEL_DOWN", "BUTTON_0", "BUTTON_2", "BUTTON_1"};

// Modern owned records corresponding to the native +0x44 record array.
// Separate references preserve already-selected records when that array is
// replaced by a callback. The records retain their established 0x54-byte data.
struct NativeInputDescription {
    std::vector<std::shared_ptr<NativeInputRecord>> records;
};

// Two views over one owner's registry. An absent ID is a native null-slot
// sentinel, not ID zero. Both views must describe the same valid prefix.
struct NativeInputDescriptionSlots {
    std::span<const std::optional<std::uint32_t>> ids;
    std::span<const std::shared_ptr<NativeInputDescription>> descriptions;
};

// 0x434190 remains unresolved. Only its signed index result is established.
// The callback may update the description; records are read after it returns.
class NativeInputNameLookup {
  public:
    virtual ~NativeInputNameLookup() = default;
    virtual std::int32_t command_index(NativeInputDescription& description,
                                       std::string_view name) = 0;
};

struct NativeInputSelection {
    std::shared_ptr<NativeInputDescription> description;
    std::array<std::shared_ptr<NativeInputRecord>, 7> commands;
};

enum class NativeInputSelectionStatus {
    completed,
    invalid_source,      // Modern refusal: inconsistent registry views.
    invalid_record_index // Modern refusal: no owned record at a nonnegative
                         // index.
};

// 0x980550: find ID 1, ordinal 0 through the existing 0x433DF0 reconstruction;
// store the description, then resolve the seven names. Missing descriptions
// clear every output; negative indices clear only that command. Modern
// refusals preserve prior effects and leave later outputs unchanged.
// completed is a modern end-of-function status, not a native success result.
NativeInputSelectionStatus
select_native_input_commands(NativeInputSelection& output,
                             NativeInputDescriptionSlots source,
                             NativeInputNameLookup& lookup);

} // namespace tdur::input

#include "tdur/input/native_input_selection.hpp"

namespace tdur::input {

NativeInputSelectionStatus
select_native_input_commands(NativeInputSelection& output,
                             NativeInputDescriptionSlots source,
                             NativeInputNameLookup& lookup) {
    if (source.ids.size() != source.descriptions.size())
        return NativeInputSelectionStatus::invalid_source;
    const auto slot = find_native_input_slot(source.ids, 1, 0);
    // Keep the native ESI identity throughout the callbacks, even when they
    // change the output's description or the owning registry.
    const auto description = slot ? source.descriptions[*slot] : nullptr;
    if (slot && !description)
        return NativeInputSelectionStatus::invalid_source;
    output.description = description;
    if (!description) {
        output.commands.fill(nullptr);
        return NativeInputSelectionStatus::completed;
    }
    for (std::size_t index = 0; index < native_selection_names.size();
         ++index) {
        const auto row =
            lookup.command_index(*description, native_selection_names[index]);
        if (row < 0) {
            output.commands[index].reset();
            continue;
        }
        const auto position = static_cast<std::size_t>(row);
        if (position >= description->records.size() ||
            !description->records[position])
            return NativeInputSelectionStatus::invalid_record_index;
        output.commands[index] = description->records[position];
    }
    return NativeInputSelectionStatus::completed;
}

} // namespace tdur::input

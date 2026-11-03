#include "tdur/ui/vmf_frame_player.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>

namespace {
// VMF operands are bytes, not necessarily UTF-8. JSON \u00XX escapes retain
// each byte value without interpreting, normalizing or executing the command.
void json_bytes(std::ostream& output, std::span<const std::byte> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    output << '"';
    for (const auto byte : bytes) {
        const auto value = std::to_integer<unsigned>(byte);
        if (value >= 32 && value < 127 && value != '"' && value != '\\')
            output << static_cast<char>(value);
        else
            output << "\\u00" << digits[value >> 4] << digits[value & 15U];
    }
    output << '"';
}
} // namespace

// Read-only diagnostic of the reconstructed player. No commercial executable
// is loaded, no host command is acknowledged, and no interface is rendered.
int wmain(int argc, wchar_t** argv) {
    using namespace tdur;
    bool variables = false;
    bool text_data = false;
    bool text_bindings = false;
    bool properties = false;
    bool source_locations = false;
    bool host_operands = false;
    bool flags_valid = true;
    for (int i = 3; i < argc; ++i) {
        const std::wstring_view flag(argv[i]);
        if (flag == L"--variables" && !variables)
            variables = true;
        else if (flag == L"--text-data" && !text_data)
            text_data = true;
        else if (flag == L"--text-bindings" && !text_bindings)
            text_bindings = true;
        else if (flag == L"--properties" && !properties)
            properties = true;
        else if (flag == L"--source-locations" && !source_locations)
            source_locations = true;
        else if (flag == L"--host-operands" && !host_operands)
            host_operands = true;
        else
            flags_valid = false;
    }
    if ((argc < 3 || argc > 9) ||
        (std::wstring_view(argv[2]) != L"pc" &&
         std::wstring_view(argv[2]) != L"xbox") ||
        !flags_valid) {
        std::cerr << "usage: tdur_inspect_vmf_playback <movie.vmf> pc|xbox "
                     "[--variables] [--text-data] [--text-bindings] "
                     "[--properties] [--source-locations] [--host-operands]\n";
        return 1;
    }
    variables = variables || text_bindings;
    text_data = text_data || text_bindings;
    try {
        const std::filesystem::path path(argv[1]);
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto length = input.tellg();
        if (!input || length <= 0 || length > 64 * 1024 * 1024) {
            std::cerr << "VMF input missing or outside 1..64MiB\n";
            return 1;
        }
        std::vector<std::byte> data(static_cast<std::size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        if (!input) {
            std::cerr << "VMF input read failed\n";
            return 1;
        }
        const auto profile = std::wstring_view(argv[2]) == L"pc"
                                 ? assets::VmfProfile::pc_2_8
                                 : assets::VmfProfile::xbox_2_5;
        const auto movie = assets::VmfMovie::parse(data, profile);
        if (!movie) {
            std::cerr << movie.error().detail << '\n';
            return 1;
        }
        std::optional<assets::VmfTextLimits> text_limits;
        if (text_data)
            text_limits.emplace();
        const auto program = ui::VmfProgram::build(
            *movie, {}, 512U * 1024U * 1024U, text_limits);
        if (!program) {
            std::cerr << program.error().detail << '\n';
            return 1;
        }
        ui::VmfFramePlayerLimits limits;
        if (variables)
            limits.variables.emplace();
        if (text_bindings)
            limits.text_bindings.emplace();
        limits.properties = properties;
        auto player = ui::VmfFramePlayer::create(*program, limits);
        if (!player) {
            std::cerr << player.error().detail << '\n';
            if (const auto& nested = player.error().display_error)
                std::cerr << "display_error=" << static_cast<int>(nested->code)
                          << ": " << nested->detail << '\n';
            return 1;
        }
        auto& p = **player;
        ui::VmfFrameCounters totals;
        const char* status = "slice_limit";
        std::optional<ui::VmfFramePlayerError> failure;
        ui::VmfFrameReport last;
        std::size_t completed = 0;
        bool done = false;
        for (std::size_t slice = 0; slice < 100 && !done; ++slice) {
            if (p.state() == ui::VmfFramePlayerState::idle) {
                const auto started = p.begin(p.display().root(), true);
                if (!started) {
                    failure = started.error();
                    break;
                }
            }
            ui::VmfFrameBudget budget;
            budget.records = 10'000;
            budget.nodes = 10'000;
            budget.scripts = 1'000;
            budget.actions.instructions = 10'000;
            budget.actions.skipped_actions = 10'000;
            const auto report = p.resume(budget);
            const auto counts =
                report ? report->counters : report.error().counters;
            totals.records += counts.records;
            totals.nodes += counts.nodes;
            totals.scripts += counts.scripts;
            totals.instructions += counts.instructions;
            totals.skipped_actions += counts.skipped_actions;
            totals.text_bytes += counts.text_bytes;
            if (!report) {
                failure = report.error();
                break;
            }
            last = *report;
            switch (last.stop) {
            case ui::VmfFrameStop::completed:
                if (++completed == 16) {
                    status = "completed_16_invocations";
                    done = true;
                }
                break;
            case ui::VmfFrameStop::needs_host:
                status = "needs_host";
                done = true;
                break;
            case ui::VmfFrameStop::needs_record_host:
                status = "needs_record_host";
                done = true;
                break;
            default:
                break;
            }
        }
        if (failure)
            status = "error";
        std::optional<std::array<std::span<const std::byte>, 2>> operands;
        if (host_operands && last.stop == ui::VmfFrameStop::needs_host &&
            last.script) {
            const auto cursor = last.script->cursor;
            const auto& script =
                (*program)->movie().scripts()[cursor.script_index];
            const auto& action =
                (*program)
                    ->movie()
                    .actions()[script.first_action + cursor.next_action];
            if (action.opcode == 0x83) {
                operands.emplace();
                for (std::size_t index = 0; index < 2; ++index) {
                    if (!action.strings[index]) {
                        std::cerr << "Validated GetURL operand is missing\n";
                        return 2;
                    }
                    const auto bytes =
                        (*program)->movie().bytes(*action.strings[index]);
                    if (!bytes) {
                        std::cerr << bytes.error().detail << '\n';
                        return 2;
                    }
                    (*operands)[index] = *bytes;
                }
            }
        }
        std::cout << "{\"status\":" << std::quoted(status)
                  << ",\"variable_backend\":" << (variables ? "true" : "false")
                  << ",\"variables\":"
                  << (p.display().variable_dictionary()
                          ? p.display().variable_dictionary()->size()
                          : 0)
                  << ",\"invocations_completed\":" << completed
                  << ",\"records\":" << totals.records
                  << ",\"nodes\":" << totals.nodes
                  << ",\"scripts\":" << totals.scripts
                  << ",\"instructions\":" << totals.instructions
                  << ",\"skipped_actions\":" << totals.skipped_actions
                  << ",\"text_bytes\":" << totals.text_bytes
                  << ",\"stack_values\":" << p.stack().size();
        if (const auto *decoded = (*program)->text_data()) {
            std::size_t fonts = 0, companions = 0;
            for (const auto &field : decoded->fields()) {
                fonts += field.font_record_index.has_value();
                companions += field.companion_record_index.has_value();
            }
            std::cout << ",\"text_fields\":" << decoded->fields().size()
                      << ",\"text_font_links\":" << fonts
                      << ",\"text_companion_links\":" << companions;
        }
        if (text_bindings) {
            std::size_t initialized = 0;
            for (const auto &field : (*program)->text_data()->fields()) {
                const auto state =
                    p.display().inspect_text_resource(field.character_id);
                initialized += state && state->binding_executed;
            }
            std::cout
                << ",\"text_bindings\":true,\"text_resources_initialized\":"
                << initialized;
        }
        if (properties)
            std::cout << ",\"property_backend\":true";
        if (operands) {
            std::cout << ",\"host_operands\":[";
            json_bytes(std::cout, (*operands)[0]);
            std::cout << ',';
            json_bytes(std::cout, (*operands)[1]);
            std::cout << ']';
        }
        if (last.script) {
            const auto cursor = last.script->cursor;
            const auto& script =
                (*program)->movie().scripts()[cursor.script_index];
            const auto &action =
                (*program)
                    ->movie()
                    .actions()[script.first_action + cursor.next_action];
            std::cout << ",\"script_index\":" << cursor.script_index
                      << ",\"action_index\":" << cursor.next_action
                      << ",\"opcode\":" << action.opcode;
            if (source_locations)
                std::cout << ",\"source_offset\":" << action.source_offset
                          << ",\"script_source_offset\":"
                          << script.encoded_region.offset
                          << ",\"script_source_size\":"
                          << script.encoded_region.size
                          << ",\"record_source_offset\":"
                          << (*program)
                                 ->movie()
                                 .records()[script.owner_record_index]
                                 .source_offset;
        }
        if (failure) {
            std::cout << ",\"error_code\":" << static_cast<int>(failure->code)
                      << ",\"detail\":" << std::quoted(failure->detail);
            if (failure->display_error)
                std::cout << ",\"display_detail\":"
                          << std::quoted(failure->display_error->detail);
            if (failure->execution_error)
                std::cout << ",\"execution_detail\":"
                          << std::quoted(failure->execution_error->detail);
        }
        std::cout << "}\n";
        return failure ? 2 : 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

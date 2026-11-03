#include "tdur/game/config_aux_986b60.hpp"

namespace tdur::game::config {

Result986b60 run_args_lookup_986b60(NativeIdentity key,
                                    std::optional<std::string_view> label,
                                    std::span<char> buffer, std::uint32_t n,
                                    Inputs986b60 inputs,
                                    ArgsTablePorts& ports) {
    Result986b60 result;
    const OpaqueWord table = inputs.ebx_10e7350;
    // 986B64..986B66 : TEST EDI,EDI ; JLE (signé) -> AL = 0, aucune lecture.
    const auto count = static_cast<std::int32_t>(inputs.edi_10e734c);
    for (std::int32_t i = 0; i < count; ++i) { // 986B8F..986B92 (signé)
        // 986B70 : EAX = [EBX + ESI*4], relu à chaque tour.
        const std::optional<OpaqueWord> entry = ports.read_entry(table, i);
        if (!entry) {
            result.outcome = Outcome986b60::refused_entry_unreadable;
            result.index = i;
            return result;
        }
        // 986B73 : CMP byte [EAX], '-' ; JNZ 986B8F.
        const std::optional<std::span<const char>> text =
            ports.resolve_text(*entry);
        if (!text || text->empty()) {
            result.outcome = Outcome986b60::refused_text_unreadable;
            result.index = i;
            return result;
        }
        if ((*text)[0] != '-') {
            continue;
        }
        // 986B78..986B86 : cmp(clé, élément + 1) ; JNZ si EAX != 0.
        ++result.compares;
        if (ports.compare_9ec25c(key, label, *entry + 1) != 0) {
            continue;
        }
        // 986B88..986B8D : ESI < EDI - 1 (signé), sinon tour suivant.
        if (!(i < count - 1)) {
            continue;
        }
        // 986B9D : EAX = [EBX + ESI*4 + 4], lu au site.
        const std::optional<OpaqueWord> value = ports.read_entry(table, i + 1);
        if (!value) {
            result.outcome = Outcome986b60::refused_entry_unreadable;
            result.index = i + 1;
            return result;
        }
        ports.copy_9ec36c(buffer, *value, n); // 986BA8
        result.copied = true;
        result.index = i;
        result.al = 1; // 986BB2
        return result;
    }
    result.al = 0; // 986B95
    return result;
}

} // namespace tdur::game::config

#include "tdur/game/config_file_loader.hpp"

namespace tdur::game::config {

void decode_integrated_in_place(std::span<std::byte> bytes) noexcept {
    std::byte previous_decoded{0}; // 6B1A3B : XOR DL,DL
    for (std::byte& value : bytes) {
        const std::byte decoded = value ^ previous_decoded; // 6B1A41..43
        value = decoded;                                    // 6B1A45
        previous_decoded = decoded;                         // 6B1A4A
    }
}

void reencode_like_98ec91_in_place(std::span<std::byte> bytes) noexcept {
    std::byte previous_clear{0}; // 98EC94 : XOR DL,DL
    for (std::byte& value : bytes) {
        const std::byte clear = value; // 98ECA0 : MOV CL,[EAX]
        value = clear ^ previous_clear;
        previous_clear = clear; // 98ECAB : MOV DL,CL
    }
}

LoaderResult run_config_file_loader_6b1a00(std::span<const char> path,
                                           LoadSlots& slots,
                                           std::uint8_t& storage_flag,
                                           BlockStore& store,
                                           ConfigLoaderPorts& ports) {
    // 6B1A05 : sélection du bloc.
    ports.select_block_6b18d0(path, slots);

    // 6B1A0A..6B1A17 : TEST du pointeur, SETZ, écriture du drapeau.
    const bool block_absent = !slots.block.has_value();
    storage_flag = block_absent ? std::uint8_t{1} : std::uint8_t{0};

    LoaderResult result;
    result.storage_flag_written = storage_flag;

    // 6B1A19 : JZ 6B1A35 si AL == 0 (bloc présent).
    if (block_absent) {
        // 6B1A1B..6B1A2C puis retour direct 6B1A31..6B1A32.
        const ExternalLoadRequest request{.path = path};
        if (ports.load_external_9a5d20(request, slots) ==
            ExternalCallOutcome::modern_refusal) {
            result.outcome = LoaderOutcome::refused_external_call;
            return result;
        }
        result.outcome = LoaderOutcome::external_requested;
        return result;
    }

    // 6B1A35..6B1A39 : début et fin. Refus moderne avant tout accès.
    const std::optional<std::span<std::byte>> owned = store.view(*slots.block);
    if (!owned) {
        result.outcome = LoaderOutcome::refused_unknown_block;
        return result;
    }
    if (slots.size > owned->size()) {
        result.outcome = LoaderOutcome::refused_size_exceeds_storage;
        return result;
    }

    // 6B1A3F : JNC (début >= fin) ; taille 0 => aucun accès.
    decode_integrated_in_place(owned->first(slots.size));
    result.outcome = LoaderOutcome::integrated_decoded;
    result.decoded_bytes = slots.size;
    return result;
}

} // namespace tdur::game::config

#include "tdur/game/config_external_loader.hpp"

#include <algorithm>

namespace tdur::game::config {

namespace {

// Sortie 9A5DEE..9A5E03 : nettoyage 6097B0 puis XOR AL,AL ; RET 0x1C.
ExternalLoadResult exit_zero(ExternalLoadResult result, LocalFileObject& local,
                             ExternalLoaderPorts& ports) {
    ports.cleanup_6097b0(local);
    result.outcome = ExternalOutcome::native_return;
    result.exit = NativeExit::zero_9a5dfa;
    result.al = 0;
    return result;
}

ExternalLoadResult refuse(ExternalLoadResult result, ExternalOutcome why) {
    result.outcome = why;
    result.exit = NativeExit::none;
    return result;
}

} // namespace

ExternalLoadResult run_external_loader_9a5d20(const ExternalLoaderArgs& args,
                                              BlockStore& store,
                                              ExternalLoaderPorts& ports) {
    ExternalLoadResult result;

    // 9A5D2F : EBP = arg2 ; 9A5D3E : MOV [EBP],ESI (ESI = 0), premier effet.
    if (args.block_slot == nullptr) {
        return refuse(result, ExternalOutcome::refused_block_slot_absent);
    }
    std::optional<BlockHandle>& block_slot = *args.block_slot;
    block_slot.reset();

    // 9A5D3A..9A5D41 : constructeur de l'objet local [ESP+0x18].
    LocalFileObject local;
    ports.construct_609780(local);

    // 9A5D46..9A5D51 : taille optionnelle mise à 0 APRÈS le constructeur.
    if (args.size_slot != nullptr) {
        *args.size_slot = 0;
    }

    // 9A5D53, 9A5D57 : temporaires du lookup à 0.
    LookupTemps temps;
    std::uint32_t size = 0; // EDI

    // 9A5D5B..9A5D6A : MOV AL,[ESI+0xA71C] ; TEST AL,AL ; JZ 9A5E0D.
    const std::optional<std::uint8_t> flag =
        ports.read_service_byte_a71c(args.service);
    if (!flag) {
        return refuse(result, ExternalOutcome::refused_service_unreadable);
    }

    bool read_locally = true;
    if (*flag == 0) {
        result.route = ExternalRoute::local_direct;
    } else {
        // 9A5D70..9A5D8B : CALL 9A4BD0 ; TEST AL,AL ; JZ 9A5D9E.
        const std::uint8_t found =
            ports.lookup_9a4bd0(args.service, args.path, temps);
        if (found != 0) {
            // 9A5D8D..9A5D9C : EDI = [entry+0x0C] ; JNZ 9A5E3A sinon 9A5E0D.
            if (temps.entry.value == 0) {
                return refuse(result,
                              ExternalOutcome::refused_lookup_entry_null);
            }
            const std::optional<std::uint32_t> entry_size =
                ports.read_entry_dword_0c(temps.entry);
            if (!entry_size) {
                return refuse(result,
                              ExternalOutcome::refused_lookup_entry_unreadable);
            }
            size = *entry_size;
            result.size = size;
            if (size != 0) {
                read_locally = false;
                result.route = ExternalRoute::lookup_entry;
            } else {
                result.route = ExternalRoute::lookup_entry_zero_local;
            }
        } else {
            // 9A5D9E..9A5DB3 : DL = [[0x1140370]+0xE0] ; DL != 0 -> 11404C4.
            const std::optional<std::uint8_t> selector =
                ports.read_selector_1140370_e0();
            if (!selector) {
                return refuse(result,
                              ExternalOutcome::refused_selector_unreadable);
            }
            const NativeIdentity prefix =
                *selector != 0 ? kPrefix11404c4 : kPrefix1140444;
            const std::optional<std::span<const char>> bytes =
                ports.prefix_string(prefix);
            if (!bytes) {
                return refuse(result,
                              ExternalOutcome::refused_prefix_unavailable);
            }
            // 9A5DB8..9A5DC7 : longueur jusqu'au NUL, NUL exclu.
            const auto nul = std::find(bytes->begin(), bytes->end(), '\0');
            if (nul == bytes->end()) {
                return refuse(result,
                              ExternalOutcome::refused_prefix_unterminated);
            }
            const auto length =
                static_cast<std::uint32_t>(nul - bytes->begin());
            // 9A5DC9..9A5DEC : même chaîne relue par le même test de DL.
            const std::uint32_t compared =
                ports.compare_9ec258(prefix, *bytes, args.path, length);
            if (compared == 0) {
                result.route = ExternalRoute::prefix_refused;
                return exit_zero(result, local, ports);
            }
            result.route = ExternalRoute::prefix_accepted_local;
        }
    }

    if (read_locally) {
        // 9A5E0D..9A5E20 : CALL 609800(chemin) ; TEST AL,AL ; JZ 9A5DEE.
        if (ports.open_609800(local, args.path) == 0) {
            return exit_zero(result, local, ports);
        }
        // 9A5E22..9A5E38 : taille, fermeture, PUIS test de taille nulle.
        size = ports.size_609710(local);
        result.size = size;
        ports.close_609760(local);
        if (size == 0) {
            return exit_zero(result, local, ports);
        }
    }

    // 9A5E3A..9A5E3E : allocateur nul -> 0xFA9CB0.
    const NativeIdentity allocator =
        args.allocator.value != 0 ? args.allocator : kDefaultAllocator9a5d20;
    // 9A5E43..9A5E68 : LEA EAX,[EDI+0x801] (modulo 2^32) ; résultat stocké
    // dans [EBP] AVANT le test nul (MOV entre TEST et JZ).
    const std::uint32_t bytes = size + kAllocationSlack;
    block_slot = ports.allocate_slot20(allocator, bytes, kAllocationAlignment,
                                       args.arg6, args.arg7);
    if (!block_slot) {
        return exit_zero(result, local, ports);
    }

    // 9A5E6A..9A5E75 : taille de sortie écrite AVANT 9A5730.
    if (args.size_slot != nullptr) {
        *args.size_slot = size;
    }

    // 9A5E77..9A5E8D : appel par registres et pile.
    const Callback9a5730 call{.eax_entry = temps.entry,
                              .esi_second = temps.second,
                              .service = args.service,
                              .path = args.path,
                              .block = *block_slot,
                              .size = size};
    const auto callback = ports.callback_9a5730(
        call, ExternalOutputs{block_slot, args.size_slot});
    if (callback.control != ExternalCallOutcome::native_returned)
        return refuse(result, ExternalOutcome::refused_callback);
    const std::uint8_t bl = callback.al;
    result.al = bl;

    // 9A5E96 : bloc relu APRÈS le callback.
    const std::optional<BlockHandle> reread = block_slot;
    if (bl == 0) {
        // 9A5E9B..9A5EB3 : libération si non nul, [EBP] = 0, nettoyage.
        // La taille de sortie n'est pas touchée.
        if (reread) {
            ports.release_674e0c(*reread);
        }
        block_slot.reset();
        ports.cleanup_6097b0(local);
        result.outcome = ExternalOutcome::native_return;
        result.exit = NativeExit::callback_failed_9a5ebb;
        return result;
    }

    // 9A5ECB : MOV byte [EDI+EAX],0 ; EDI = taille locale, pas l'emplacement.
    if (!reread) {
        return refuse(result, ExternalOutcome::refused_terminator_null_block);
    }
    const std::optional<std::span<std::byte>> owned = store.view(*reread);
    if (!owned) {
        return refuse(result,
                      ExternalOutcome::refused_terminator_unknown_block);
    }
    if (size >= owned->size()) {
        return refuse(result,
                      ExternalOutcome::refused_terminator_out_of_bounds);
    }
    (*owned)[size] = std::byte{0};

    // 9A5ECF..9A5EE0 : nettoyage puis AL = BL brut.
    ports.cleanup_6097b0(local);
    result.outcome = ExternalOutcome::native_return;
    result.exit = NativeExit::callback_success_9a5ed7;
    return result;
}

void ExternalLoaderBinding::select_block_6b18d0(std::span<const char> path,
                                                LoadSlots& slots) {
    selection_.select_block_6b18d0(path, slots);
}

ExternalCallOutcome
ExternalLoaderBinding::load_external_9a5d20(const ExternalLoadRequest& request,
                                            LoadSlots& slots) {
    // 6B1A1B..6B1A27 : EDX -> &slots.block, EBX -> &slots.size.
    const ExternalLoaderArgs args{.service = request.service,
                                  .block_slot = &slots.block,
                                  .path = request.path,
                                  .size_slot = &slots.size,
                                  .allocator = NativeIdentity{request.arg4},
                                  .arg6 = request.arg5.value,
                                  .arg7 = request.arg6};
    last_ = run_external_loader_9a5d20(args, store_, ports_);
    return last_->outcome == ExternalOutcome::native_return
               ? ExternalCallOutcome::native_returned
               : ExternalCallOutcome::modern_refusal;
}

} // namespace tdur::game::config

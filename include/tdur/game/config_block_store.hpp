#pragma once

// Stockage possédé des blocs de configuration, étendu ensuite.
// Un BlockHandle est un indice dans le magasin, jamais une adresse native ni
// une adresse hôte. Remplacer un handle dans un emplacement ne libère rien.
// Extension : release() est la seule libération anticipée ; elle est
// EXPLICITE (chemin d'échec 9A5E9B..9A5EA0 de 9A5D20). Les indices ne sont
// jamais réutilisés : un handle libéré reste inconnu pour view().

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace tdur::game::config {

struct BlockHandle {
    std::size_t index = 0;
    friend bool operator==(BlockHandle, BlockHandle) = default;
};

class BlockStore {
  public:
    BlockHandle add(std::vector<std::byte> bytes) {
        blocks_.push_back(Entry{std::move(bytes), true});
        return BlockHandle{blocks_.size() - 1};
    }

    // Vue modifiable bornée au stockage possédé ; nullopt si le handle est
    // inconnu ou libéré. Un bloc vivant de 0 octet donne une vue vide mais
    // présente. Aucun octet n'est lu ici.
    std::optional<std::span<std::byte>> view(BlockHandle handle) {
        if (!is_live(handle)) {
            return std::nullopt;
        }
        return std::span<std::byte>(blocks_[handle.index].bytes);
    }

    [[nodiscard]] bool is_live(BlockHandle handle) const noexcept {
        return handle.index < blocks_.size() && blocks_[handle.index].live;
    }

    // Libération explicite. false (aucun effet) si le handle est inconnu ou
    // déjà libéré : un double appel est refusé, jamais silencieux.
    bool release(BlockHandle handle) {
        if (!is_live(handle)) {
            return false;
        }
        Entry& entry = blocks_[handle.index];
        std::vector<std::byte>().swap(entry.bytes);
        entry.live = false;
        return true;
    }

    // Nombre de handles émis (vivants ou libérés), comme initialement.
    std::size_t count() const noexcept { return blocks_.size(); }

  private:
    struct Entry {
        std::vector<std::byte> bytes;
        bool live = false;
    };
    std::vector<Entry> blocks_;
};

} // namespace tdur::game::config

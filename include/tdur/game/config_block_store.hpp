#pragma once

// Owned storage of the configuration blocks, extended later.
// A BlockHandle is an index into the store, never a native address or a
// host address. Replacing a handle in a slot releases nothing.
// Extension: release() is the only early release; it is EXPLICIT (failure
// path 9A5E9B..9A5EA0 of 9A5D20). Indices are never reused: a released
// handle stays unknown to view().

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

    // Mutable view bounded to the owned storage; nullopt if the handle is
    // unknown or released. A live block of 0 bytes gives an empty but
    // present view. No byte is read here.
    std::optional<std::span<std::byte>> view(BlockHandle handle) {
        if (!is_live(handle)) {
            return std::nullopt;
        }
        return std::span<std::byte>(blocks_[handle.index].bytes);
    }

    [[nodiscard]] bool is_live(BlockHandle handle) const noexcept {
        return handle.index < blocks_.size() && blocks_[handle.index].live;
    }

    // Explicit release. false (no effect) if the handle is unknown or
    // already released: a double call is refused, never silent.
    bool release(BlockHandle handle) {
        if (!is_live(handle)) {
            return false;
        }
        Entry& entry = blocks_[handle.index];
        std::vector<std::byte>().swap(entry.bytes);
        entry.live = false;
        return true;
    }

    // Number of handles issued (live or released), as initially.
    std::size_t count() const noexcept { return blocks_.size(); }

  private:
    struct Entry {
        std::vector<std::byte> bytes;
        bool live = false;
    };
    std::vector<Entry> blocks_;
};

} // namespace tdur::game::config

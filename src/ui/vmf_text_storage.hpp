#pragma once

#include <cstddef>
#include <vector>

namespace tdur::ui::detail {

// Private owned backing shared by VMF values/stacks and notification caches.
// Mutable users serialize access and preserve termination before publication.
struct VmfTextStorage {
    explicit VmfTextStorage(std::size_t size) : bytes(size, '\0') {}
    std::vector<char> bytes;
};

} // namespace tdur::ui::detail

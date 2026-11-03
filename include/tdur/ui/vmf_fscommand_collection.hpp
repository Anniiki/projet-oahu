#pragma once

#include "tdur/ui/vmf_fscommand_global.hpp"

namespace tdur::ui {

// Concrete99A9A0/99A780 port for one fixed owned movie span. The adapter
// BORROWS that span; it owns neither movies nor displays. Missing binding is
// inputs.collection_114b0c0 == nullopt, distinct from a bound empty span.
// Metadata/flags stay live; count/item never cache a filtered list or cursor.
// Keep this service, the physical span and every selected movie alive and
// stationary until all contexts release them. Serialize access. Membership
// values may change between calls; invalidating/resizing storage may not.
// Does not implement the external866040/866080 services or a native loader.
class VmfFsMenuCollection final : public VmfFsGlobalCollection {
  public:
    explicit VmfFsMenuCollection(std::span<VmfMenuMovie> movies) noexcept;
    VmfFsMenuCollection(const VmfFsMenuCollection&) = delete;
    VmfFsMenuCollection& operator=(const VmfFsMenuCollection&) = delete;
    VmfFsMenuCollection(VmfFsMenuCollection&&) = delete;
    VmfFsMenuCollection& operator=(VmfFsMenuCollection&&) = delete;
    std::expected<std::int32_t, VmfFsCommandError>
    count(std::uint32_t selector) noexcept override;
    std::expected<VmfFsCollectionItem*, VmfFsCommandError>
    item(std::uint32_t selector, std::int32_t index) noexcept override;

  private:
    std::span<VmfMenuMovie> movies_;
};

} // namespace tdur::ui

#pragma once

#include "tdur/ui/vmf_menu_data.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

struct VmfTutorialRow {
    VmfTextReference key;        // Text_ID_, native +0.
    VmfTextReference text_index; // TextIndex_, +8; retained, not interpreted.
    VmfTextReference text;       // Libelle_, +10 hex.
    VmfTextReference file;       // File_, +18 hex.
    std::int32_t time_cent;      // +20 hex; no timing unit conversion.
};

// TDU_Tutorials, topic11, native stride40. Four source u/u/h/u ID64 fields
// use the existing first-definition catalog semantics; i is signed32.
// Different IDs with equal text retain DIFFERENT backing identities.
// Aliases of one ID share backing, including leading-zero decimal aliases.
// Missing references, bad schemas/numbers and quotas are modern refusals.
class VmfTutorialDatabase {
  public:
    [[nodiscard]] static std::expected<VmfTutorialDatabase, VmfMenuDataError>
    from_table(const assets::DatabaseTable& table,
               const assets::DatabaseStrings& catalog,
               VmfMenuDataLimits limits = {}) noexcept;
    [[nodiscard]] std::span<const VmfTutorialRow> rows() const noexcept;
    [[nodiscard]] std::expected<std::optional<VmfTextReference>,
                                VmfMenuDataError>
    text_by_id(std::uint64_t id) const noexcept;

    // 9827B0: first matching row, raw case-sensitive C-string comparison.
    [[nodiscard]] std::expected<std::optional<std::size_t>, VmfMenuDataError>
    find_help(std::string_view key) const noexcept;
    // 7FBEF0: same scan, but skip EVERY match whose text POINTER equals
    // the catalog's sentinel pointer, then continue scanning duplicate keys.
    // The caller supplies the ID observed in its native service. Missing
    // sentinel is a known NULL lookup, not text equality with "none".
    [[nodiscard]] std::expected<std::optional<std::size_t>, VmfMenuDataError>
    find_tutorial(std::string_view key,
                  std::uint64_t sentinel_id) const noexcept;

  private:
    struct Storage;
    explicit VmfTutorialDatabase(std::shared_ptr<const Storage> storage,
                                 VmfMenuDataLimits limits) noexcept;
    [[nodiscard]] std::expected<std::optional<std::size_t>, VmfMenuDataError>
    find(std::string_view key, const VmfTextReference* excluded) const noexcept;
    std::shared_ptr<const Storage> storage_;
    VmfMenuDataLimits limits_;
};

} // namespace tdur::ui

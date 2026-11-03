#pragma once

// Pure core of the read-only D-mode probe. Contract: the global G
// (VA 0x10E7238) holds an object D whose vptr is 0xF0B138; D+0x100 is the
// mode (DWORD), D+0x104 the previous mode (DWORD), D+0x108 a guard BYTE.
// That G is the `this` of the dispatcher stays an ABI hypothesis, and even an
// observation here does not prove what the code at PC 0x97CE80 reads.
// The catalogue is fixed: no caller can choose an address.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tdur::diagnostics::mode_probe {

inline constexpr std::uint32_t preferred_base = 0x00400000;
inline constexpr std::uint32_t global_va = 0x010E7238;
inline constexpr std::uint32_t expected_vptr_va = 0x00F0B138;
inline constexpr std::uint32_t mode_offset = 0x100;
inline constexpr std::uint32_t old_mode_offset = 0x104;
inline constexpr std::uint32_t guard_offset = 0x108;
// Mode DWORD, previous-mode DWORD and the guard byte: 9 contiguous bytes.
inline constexpr std::size_t field_length = guard_offset + 1 - mode_offset;
inline constexpr std::size_t max_reads_per_sample = 6;
inline constexpr std::size_t max_bytes_per_sample = 34;
inline constexpr std::size_t max_logged_changes = 64;
inline constexpr std::uint32_t min_interval_ms = 25;
inline constexpr std::uint32_t default_interval_ms = 50;
inline constexpr std::uint32_t max_interval_ms = 1000;
inline constexpr std::uint32_t max_duration_s = 120;

enum class Space { image, object };

// Fills `out` completely or returns false; a partial read is a failure.
class Reader {
  public:
    virtual ~Reader() = default;
    virtual bool read(Space space, std::uint32_t address,
                      std::span<std::byte> out) = 0;
};

struct Layout {
    std::uint32_t global = 0;
    std::uint32_t vptr = 0;
};

// Relocates the two catalogue constants if the image is not at its preferred
// base. The global must lie inside the mapped image.
inline std::expected<Layout, std::string> make_layout(std::uint32_t base,
                                                      std::uint32_t image_size,
                                                      std::uint32_t preferred) {
    if (preferred != preferred_base)
        return std::unexpected("preferred base must be 0x400000");
    if (base == 0 || image_size < 4 ||
        image_size > std::numeric_limits<std::uint32_t>::max() - base)
        return std::unexpected("invalid image range");
    constexpr std::uint32_t global_rva = global_va - preferred_base;
    constexpr std::uint32_t vptr_rva = expected_vptr_va - preferred_base;
    if (global_rva > image_size - 4 || vptr_rva > image_size - 4)
        return std::unexpected("image too small for the catalogue");
    return Layout{base + global_rva, base + vptr_rva};
}

enum class Status { available, unavailable, unstable };
enum class Reason {
    none,
    null_pointer,
    misaligned,
    overflow,
    unexpected_type,
    memory
};
enum class Guard { zero, one, other };

// Deliberately holds no pointer: only classified, numeric results.
struct Sample {
    Status status = Status::unavailable;
    Reason reason = Reason::none;
    std::int32_t mode = 0;
    std::int32_t old_mode = 0;
    Guard guard = Guard::zero;
    friend bool operator==(const Sample&, const Sample&) = default;
};

namespace detail {

inline std::uint32_t le32(std::span<const std::byte> bytes) {
    return std::to_integer<std::uint32_t>(bytes[0]) |
           std::to_integer<std::uint32_t>(bytes[1]) << 8 |
           std::to_integer<std::uint32_t>(bytes[2]) << 16 |
           std::to_integer<std::uint32_t>(bytes[3]) << 24;
}

inline Guard classify(std::byte value) {
    const auto number = std::to_integer<unsigned>(value);
    return number == 0 ? Guard::zero : number == 1 ? Guard::one : Guard::other;
}

// Refuses any read beyond the per-sample budget, whatever the caller does.
class Budget {
  public:
    explicit Budget(Reader& reader) : reader_(reader) {}
    bool read(Space space, std::uint32_t address, std::span<std::byte> out) {
        if (reads_ >= max_reads_per_sample ||
            out.size() > max_bytes_per_sample - bytes_)
            return false;
        ++reads_;
        bytes_ += out.size();
        return reader_.read(space, address, out);
    }

  private:
    Reader& reader_;
    std::size_t reads_ = 0;
    std::size_t bytes_ = 0;
};

inline Sample refused(Reason reason) {
    return Sample{Status::unavailable, reason};
}

} // namespace detail

// Reads G, the vptr, then the 9 field bytes; then reads all three again and
// rejects any difference. This is stability by double read, not an atomic
// snapshot: a change and its reversal between the two passes go unseen.
inline Sample take_sample(Reader& reader, const Layout& layout) {
    using detail::refused;
    detail::Budget budget(reader);
    std::array<std::byte, 4> global{}, vptr{}, global_again{}, vptr_again{};
    std::array<std::byte, field_length> fields{}, fields_again{};
    if (!budget.read(Space::image, layout.global, global))
        return refused(Reason::memory);
    const auto object = detail::le32(global);
    if (object == 0)
        return refused(Reason::null_pointer);
    if (object % 4 != 0)
        return refused(Reason::misaligned);
    if (object > std::numeric_limits<std::uint32_t>::max() - guard_offset)
        return refused(Reason::overflow);
    if (!budget.read(Space::object, object, vptr))
        return refused(Reason::memory);
    if (detail::le32(vptr) != layout.vptr)
        return refused(Reason::unexpected_type);
    if (!budget.read(Space::object, object + mode_offset, fields) ||
        !budget.read(Space::image, layout.global, global_again))
        return refused(Reason::memory);
    if (global != global_again)
        return Sample{Status::unstable, Reason::none};
    if (!budget.read(Space::object, object, vptr_again))
        return refused(Reason::memory);
    // Never read fields again after observing a changed object class.
    if (vptr != vptr_again)
        return Sample{Status::unstable, Reason::none};
    if (!budget.read(Space::object, object + mode_offset, fields_again))
        return refused(Reason::memory);
    if (fields != fields_again)
        return Sample{Status::unstable, Reason::none};
    const std::span<const std::byte> view(fields);
    constexpr auto old_at = old_mode_offset - mode_offset;
    constexpr auto guard_at = guard_offset - mode_offset;
    return Sample{
        Status::available, Reason::none,
        static_cast<std::int32_t>(detail::le32(view.first(4))),
        static_cast<std::int32_t>(detail::le32(view.subspan(old_at, 4))),
        detail::classify(fields[guard_at])};
}

struct Counters {
    std::uint64_t samples = 0;
    std::uint64_t available = 0;
    std::uint64_t unstable = 0;
    std::uint64_t null_pointer = 0;
    std::uint64_t misaligned = 0;
    std::uint64_t overflow = 0;
    std::uint64_t unexpected_type = 0;
    std::uint64_t memory = 0;
    std::uint64_t changes = 0;
    std::uint64_t logged = 0;
    std::uint64_t dropped = 0;
    // Consecutive available samples with the same mode but another previous
    // mode. Sampling can miss SetMode calls (A to B to A), so this counts
    // observations; it is not proof that the contract was broken.
    std::uint64_t previous_changed_mode_same = 0;
    friend bool operator==(const Counters&, const Counters&) = default;
};

struct Change {
    std::uint64_t elapsed_ms = 0;
    std::uint64_t index = 0;
    Sample sample;
};

// Coalesces identical consecutive samples; keeps at most 64 changes.
class Journal {
  public:
    void record(std::uint64_t elapsed_ms, const Sample& sample) {
        const auto index = counters_.samples++;
        switch (sample.status) {
        case Status::available:
            ++counters_.available;
            break;
        case Status::unstable:
            ++counters_.unstable;
            break;
        case Status::unavailable:
            count_reason(sample.reason);
            break;
        }
        if (last_ && *last_ == sample)
            return;
        if (last_ && last_->status == Status::available &&
            sample.status == Status::available && last_->mode == sample.mode &&
            last_->old_mode != sample.old_mode)
            ++counters_.previous_changed_mode_same;
        last_ = sample;
        ++counters_.changes;
        if (changes_.size() < max_logged_changes) {
            changes_.push_back({elapsed_ms, index, sample});
            ++counters_.logged;
        } else {
            ++counters_.dropped;
        }
    }
    const std::vector<Change>& changes() const { return changes_; }
    const Counters& counters() const { return counters_; }

  private:
    void count_reason(Reason reason) {
        switch (reason) {
        case Reason::null_pointer:
            ++counters_.null_pointer;
            break;
        case Reason::misaligned:
            ++counters_.misaligned;
            break;
        case Reason::overflow:
            ++counters_.overflow;
            break;
        case Reason::unexpected_type:
            ++counters_.unexpected_type;
            break;
        case Reason::memory:
        case Reason::none:
            ++counters_.memory;
            break;
        }
    }
    std::optional<Sample> last_;
    std::vector<Change> changes_;
    Counters counters_;
};

inline std::string_view name(Status status) {
    switch (status) {
    case Status::available:
        return "available";
    case Status::unstable:
        return "unstable";
    case Status::unavailable:
        break;
    }
    return "unavailable";
}

inline std::string_view name(Reason reason) {
    switch (reason) {
    case Reason::null_pointer:
        return "null_pointer";
    case Reason::misaligned:
        return "misaligned";
    case Reason::overflow:
        return "overflow";
    case Reason::unexpected_type:
        return "unexpected_type";
    case Reason::memory:
        return "memory";
    case Reason::none:
        break;
    }
    return "none";
}

inline std::string_view name(Guard guard) {
    switch (guard) {
    case Guard::zero:
        return "zero";
    case Guard::one:
        return "one";
    case Guard::other:
        break;
    }
    return "other";
}

// One JSON object per change; numbers and classes only, never a pointer.
inline std::string render_change(const Change& change) {
    std::string text = "{\"t_ms\":" + std::to_string(change.elapsed_ms) +
                       ",\"sample\":" + std::to_string(change.index) +
                       ",\"status\":\"" +
                       std::string(name(change.sample.status)) + "\"";
    if (change.sample.status == Status::unavailable)
        text +=
            ",\"reason\":\"" + std::string(name(change.sample.reason)) + "\"";
    if (change.sample.status == Status::available)
        text += ",\"mode\":" + std::to_string(change.sample.mode) +
                ",\"old_mode\":" + std::to_string(change.sample.old_mode) +
                ",\"guard\":\"" + std::string(name(change.sample.guard)) + "\"";
    return text + "}";
}

inline std::string render_counters(const Counters& c) {
    const auto field = [](std::string_view key, std::uint64_t value) {
        return "\"" + std::string(key) + "\":" + std::to_string(value);
    };
    return "{" + field("samples", c.samples) + "," +
           field("available", c.available) + "," +
           field("unstable", c.unstable) + "," +
           field("null_pointer", c.null_pointer) + "," +
           field("misaligned", c.misaligned) + "," +
           field("overflow", c.overflow) + "," +
           field("unexpected_type", c.unexpected_type) + "," +
           field("memory", c.memory) + "," + field("changes", c.changes) + "," +
           field("logged", c.logged) + "," + field("dropped", c.dropped) + "," +
           field("previous_changed_mode_same", c.previous_changed_mode_same) +
           "}";
}

struct Options {
    std::uint32_t pid = 0;
    std::uint64_t creation = 0;
    std::wstring executable;
    std::uint32_t preferred = 0;
    std::wstring output;
    std::uint32_t duration_s = 0;
    std::uint32_t interval_ms = default_interval_ms;
};

namespace detail {

inline std::optional<std::uint64_t> parse_number(std::wstring_view text,
                                                 bool allow_hex) {
    unsigned radix = 10;
    if (allow_hex && text.size() > 2 && text[0] == L'0' &&
        (text[1] == L'x' || text[1] == L'X')) {
        radix = 16;
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 20)
        return std::nullopt;
    std::uint64_t value = 0;
    for (const auto ch : text) {
        unsigned digit = 0;
        if (ch >= L'0' && ch <= L'9')
            digit = static_cast<unsigned>(ch - L'0');
        else if (radix == 16 && ch >= L'a' && ch <= L'f')
            digit = static_cast<unsigned>(ch - L'a') + 10;
        else if (radix == 16 && ch >= L'A' && ch <= L'F')
            digit = static_cast<unsigned>(ch - L'A') + 10;
        else
            return std::nullopt;
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / radix)
            return std::nullopt;
        value = value * radix + digit;
    }
    return value;
}

} // namespace detail

// Every option is required once, except --interval-ms. No address option.
inline std::expected<Options, std::string>
parse_options(std::span<const std::wstring_view> args) {
    constexpr std::array<std::wstring_view, 7> keys{
        L"--pid", L"--creation-filetime", L"--exe",        L"--preferred-base",
        L"--out", L"--duration-s",        L"--interval-ms"};
    std::array<std::optional<std::wstring_view>, keys.size()> seen{};
    if (args.size() % 2 != 0)
        return std::unexpected("options come in name/value pairs");
    for (std::size_t i = 0; i < args.size(); i += 2) {
        std::size_t k = 0;
        while (k < keys.size() && keys[k] != args[i])
            ++k;
        if (k == keys.size())
            return std::unexpected("unknown option");
        if (seen[k])
            return std::unexpected("duplicate option");
        seen[k] = args[i + 1];
    }
    for (std::size_t k = 0; k + 1 < keys.size(); ++k)
        if (!seen[k] || seen[k]->empty())
            return std::unexpected("missing required option");
    const auto pid = detail::parse_number(*seen[0], false);
    const auto creation = detail::parse_number(*seen[1], false);
    const auto preferred = detail::parse_number(*seen[3], true);
    const auto duration = detail::parse_number(*seen[5], false);
    const auto interval =
        seen[6] ? detail::parse_number(*seen[6], false)
                : std::optional<std::uint64_t>(default_interval_ms);
    if (!pid || *pid == 0 || *pid > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected("invalid pid");
    if (!creation || *creation == 0)
        return std::unexpected("invalid creation FILETIME");
    if (!preferred || *preferred != preferred_base)
        return std::unexpected("preferred base must be 0x400000");
    if (!duration || *duration == 0 || *duration > max_duration_s)
        return std::unexpected("duration must be 1 to 120 seconds");
    if (!interval || *interval < min_interval_ms || *interval > max_interval_ms)
        return std::unexpected("interval must be 25 to 1000 ms");
    Options options;
    options.pid = static_cast<std::uint32_t>(*pid);
    options.creation = *creation;
    options.executable = std::wstring(*seen[2]);
    options.preferred = static_cast<std::uint32_t>(*preferred);
    options.output = std::wstring(*seen[4]);
    options.duration_s = static_cast<std::uint32_t>(*duration);
    options.interval_ms = static_cast<std::uint32_t>(*interval);
    return options;
}

} // namespace tdur::diagnostics::mode_probe

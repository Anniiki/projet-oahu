#pragma once

// Sibling drivers 0x98DD20 (RET 4) and 0x98DE80 (RET 8),
// and their wiring to the DriverPorts of 98E960. Listings 29 and 30.
// The loader result contract applies: any outcome other than a native
// return of 6B1A00 (LoaderResult::native_returned) stops the sequence.
// Reuses construct_9896c0 and kRegistrations9896c0, LoadSlots,
// run_config_file_loader_6b1a00, run_config_section_parser_438450,
// BlockStore, 986B60 and 988FC0. A single ParserObject per frame.

#include "tdur/game/config_aux_986b60.hpp"
#include "tdur/game/config_aux_988fc0.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

inline constexpr std::size_t kBuffer98dd20 = 0x100; // 98DD31..98DD49
inline constexpr std::uint32_t kBound98dd20 = 0xFF; // 98DD57, 98DD7B
inline constexpr std::size_t kBuffer98de80 = 0x80;  // 98DEAB..98DECB
inline constexpr std::uint32_t kBound98de80 = 0x7F; // 98DEBD, 98DEDE
inline constexpr std::size_t kFormedName = 0x20;    // ESP+0x1C..0x3C
inline constexpr std::uint32_t kEntryStride3c = 0x3C;
inline constexpr std::uint32_t kElementStride138 = 0x138;
inline constexpr std::uint32_t kRecordStride30 = 0x30;

// Stack frame owned by the caller: never reset to zero by the driver
// outside the buffer (native). The loader outputs keep their earlier
// value if 6B1A00 does not write them.
struct Frame98dd20 {
    std::array<char, kBuffer98dd20> buffer{}; // ESP+0x100
    LoadSlots slots;                          // +0x10 block, +0x14 size
    std::uint8_t storage_flag = 0;            // +0x0C
};

struct Frame98de80 {
    std::array<char, kBuffer98de80> buffer{}; // ESP+0x3C
    std::array<char, kFormedName> formed{};   // ESP+0x1C, uninitialized
    LoadSlots slots;                          // +0x18 block, +0x14 size
    std::uint8_t storage_flag = 0;            // +0x10
};

// Absolute globals read by 98DE80, reread at their sites.
struct AuxGlobals {
    std::uint32_t guard_10e7544 = 0; // read signed (98DE80)
    std::uint32_t count_fa9db8 = 0;  // signed counter
    std::uint32_t limit_fa9da0 = 0;  // signed bound
    OpaqueWord base_fa9dc0 = 0;      // host base of the 0x3C entries
    std::uint32_t count_11403c4 = 0; // 0x1140360 + 0x64, absolute global
    OpaqueWord base_11403d0 = 0;     // 0x1140360 + 0x70, absolute global
};

// Native entry of 0x3C bytes (table 0xFA9D30); host link at +0x18.
struct Entry3c {
    std::uint32_t count_10 = 0;
    std::uint32_t field_14 = 0;
    OpaqueWord data_18 = 0;
    std::array<char, kEntryStride3c - 0x1C> name_1c{};
};

class AuxDriverPorts {
  public:
    virtual ~AuxDriverPorts() = default;
    // Native string identity to bounded storage; nullopt = unknown.
    virtual std::optional<std::string_view> resolve_label(NativeIdentity) = 0;
    virtual void call_896160() = 0; // 98DEFC, inputs not established
    // 98DF15: EAX = 0, EDX = 0xF035DF, EDI = 0xFA9D30; return = entry.
    virtual OpaqueWord call_405300(std::uint32_t eax, NativeIdentity edx,
                                   NativeIdentity edi) = 0;
    virtual Entry3c* resolve_entry(OpaqueWord entry) = 0;
    virtual std::optional<std::span<std::byte>> resolve_data(OpaqueWord) = 0;
    // 98DFF5: [0x9EC378](local, 0xF0A394, n), cdecl; format not assumed.
    virtual void format_9ec378(std::span<char, kFormedName> local,
                               NativeIdentity format, std::uint32_t n) = 0;
    virtual std::optional<std::uint8_t> read_record_byte(NativeIdentity) = 0;
    virtual std::optional<std::uint32_t> read_record_dword(NativeIdentity) = 0;
    // 98E048: EAX = element, EDX = record (preserved by the callee).
    virtual OpaqueWord call_985610(OpaqueWord element,
                                   NativeIdentity record) = 0;
    // 98E054: [element + offset] = r; false = invalid access.
    virtual bool write_element(OpaqueWord element, std::uint32_t offset,
                               OpaqueWord value) = 0;
};

struct AuxEnvironment {
    DriverContext& globals; // [0x10E7350], [0x10E734C], [0x10E7814]
    AuxGlobals& aux;
    BlockStore& store;
    SectionParserState& state;
    Frame98dd20& frame_dd20;
    Frame98de80& frame_de80;
    config_registry::Constructor9896c0Ports& constructor;
    ConfigLoaderPorts& loader;
    SectionParserPorts& sections;
    ArgsTablePorts& args;
    ParserTeardownPorts& teardown;
    AuxDriverPorts& aux_ports;
};

enum class AuxOutcome : std::uint8_t {
    native_return,
    guard_exit, // 98DE8D: guard > 0, no effect
    refused_parser_construction,
    refused_args_lookup,    // 986B60
    refused_label_unknown,  // string absent from the resolved catalog
    refused_loader,         // 6B1A00
    refused_section_parser, // 438450
    refused_table_entry,    // 98DF20: 0x3C entry unresolved
    refused_table_data,     // 98DF30: data outside storage
    refused_table_name,     // 98DF65: name outside the owned entry
    refused_record,         // 98E030/98E051/98E057
    refused_element_write,  // 98E054
    refused_teardown,       // 988FC0
};

struct AuxResult {
    AuxOutcome outcome = AuxOutcome::native_return;
    NativeIdentity refused_at;
    bool present = false; // captured pointer non-null
    std::optional<BlockHandle> block;
    std::uint32_t size = 0; // size captured once
    std::uint32_t section_calls = 0;
    std::uint32_t table_entries = 0;
    std::uint32_t calls_985610 = 0;
    std::optional<Result986b60> lookup;
    std::optional<Result988fc0> teardown;
};

AuxResult run_aux_driver_98dd20(startup::StartupServiceBase& service,
                                AuxEnvironment env, DriverPorts& driver);
AuxResult run_aux_driver_98de80(startup::StartupServiceBase& service,
                                std::uint32_t n, AuxEnvironment env,
                                DriverPorts& driver);

// Wiring: replaces override_986b60, call_98dd20, call_98de80 and
// destroy_988fc0; delegates the other ports to `inner`. Each modern refusal
// is reported as HelperCallOutcome::modern_refusal; AL = 0 and the guard
// exit of 98DE80 are native returns. Latest results are observable.
class AuxDriverBinding final : public DriverPorts {
  public:
    AuxDriverBinding(DriverPorts& inner, AuxEnvironment env)
        : inner_(inner), env_(env) {}
    void copy_9ec36c(std::span<char> dest, NativeIdentity source,
                     std::optional<std::string_view> text,
                     std::uint32_t count) override {
        inner_.copy_9ec36c(dest, source, text, count);
    }
    HelperCallOutcome override_986b60(NativeIdentity key,
                                      std::string_view label,
                                      std::span<char> path, std::uint32_t bound,
                                      Inputs986b60 inputs) override;
    HelperCallOutcome
    call_98dd20(startup::StartupServiceBase& service) override;
    HelperCallOutcome call_98de80(startup::StartupServiceBase& service,
                                  std::uint32_t arg) override;
    void call_9a4750(NativeIdentity s) override { inner_.call_9a4750(s); }
    void call_9a6040(NativeIdentity s, std::uint32_t a) override {
        inner_.call_9a6040(s, a);
    }
    void release_6b19c0(BlockHandle b, std::uint32_t size,
                        std::uint8_t& flag) override {
        inner_.release_6b19c0(b, size, flag);
    }
    void free_674e0c(BlockHandle b) override { inner_.free_674e0c(b); }
    void call_87b430(NativeIdentity l, Block11e2f3a& b,
                     std::uint32_t v) override {
        inner_.call_87b430(l, b, v);
    }
    HelperCallOutcome destroy_988fc0(ParserObject& parser) override;

    std::optional<Result986b60> last_lookup;
    std::optional<AuxResult> last_98dd20;
    std::optional<AuxResult> last_98de80;
    std::optional<Result988fc0> last_teardown;

  private:
    DriverPorts& inner_;
    AuxEnvironment env_;
};

} // namespace tdur::game::config

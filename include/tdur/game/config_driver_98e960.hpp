#pragma once

// Driver 0x98E960 (configuration loading at startup).
// Composes the accepted modules: construct_9896c0 and its table,
// BlockStore, LoadSlots and run_config_file_loader_6b1a00,
// run_config_section_parser_438450, reencode_like_98ec91_in_place,
// StartupServiceBase and GameModeRuntime. Callees that are not
// reconstructed remain ports; no native address is called or followed.
// The native code sets no AL: the result below is modern.

#include "tdur/game/config_block_store.hpp"
#include "tdur/game/config_file_loader.hpp"
#include "tdur/game/config_registry_9896c0.hpp"
#include "tdur/game/config_section_parser.hpp"
#include "tdur/game/startup_services.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

// Definition of the opaque type of 438450: parser context constructed
// by 9896C0 and destroyed by 988FC0. 438450 does not know its definition.
struct ParserObject {
    config_registry::ParserObject9896c0 registry;
};

inline constexpr std::size_t kPathBufferSize = 0x100;      // 98E98A..98E9B5
inline constexpr std::uint32_t kPathCopyBound = 0xFF;      // PUSH 0xFF
inline constexpr std::size_t kPhysicsRecordStride = 0x3B0; // 944
inline constexpr std::size_t kPhysicsCopyBytes = 0xEB * 4; // 940
inline constexpr std::size_t kServiceBlock29cSize = 0x400; // 98EDCF
inline constexpr std::uint32_t kFinalCopyBound = 0x7F;     // 98EDC6
inline constexpr std::uint16_t kWord11e2fba = 0x2328;      // 9000

// [0x10E7814]: target of the section callbacks, written by the driver.
// unset = earlier value not established, never written by 98E960.
enum class PhysicsTarget : std::uint8_t { unset, car_168d230, bike_168ce80 };

// Native records 0x168CE80 (Bike, bytes 0..943) and 0x168D230
// (Car, bytes 944..1887), contiguous as in the native code.
struct PhysicsRecords {
    std::array<std::byte, 2 * kPhysicsRecordStride> bytes{};
    [[nodiscard]] std::span<std::byte, kPhysicsRecordStride> bike() noexcept {
        return std::span(bytes).first<kPhysicsRecordStride>();
    }
    [[nodiscard]] std::span<std::byte, kPhysicsRecordStride> car() noexcept {
        return std::span(bytes).last<kPhysicsRecordStride>();
    }
};

// Block 0x11E2F3A: 127 bytes copied, NUL at +0x7F, word at +0x80.
struct Block11e2f3a {
    std::array<char, 0x80> text{};
    std::uint16_t word_80 = 0;
};

// Owned data replacing the stack and the globals of 98E960. The ports
// may modify them; each read by the driver happens at the site.
struct DriverContext {
    LoadSlots slots;               // S+0x14 (block) and S+0x0C (size)
    std::uint8_t storage_flag = 0; // S+0x10
    std::array<char, kPathBufferSize> path{}; // S+0x100, initialized once
    PhysicsTarget physics_target = PhysicsTarget::unset; // [0x10E7814]
    PhysicsRecords physics;
    startup::OpaqueWord global_10e7350 = 0; // host table, reread at each call
    std::uint32_t global_10e734c = 0;       // EDI read before each 986B60
    std::array<std::byte, kServiceBlock29cSize> service_29c{}; // service+29C
    Block11e2f3a block_11e2f3a;
};

// Inputs of 986B60 read at the site: EBX is a table of host pointers,
// EDI keeps its 32 native bits (count interpreted as signed by the callee).
struct Inputs986b60 {
    startup::OpaqueWord ebx_10e7350 = 0;
    std::uint32_t edi_10e734c = 0;
};

// Extension: native return reached, or modern refusal at the invalid
// access site (no continuation simulated). Never a functional success.
enum class HelperCallOutcome : std::uint8_t { native_returned, modern_refusal };

// Integration points. The four reconstructed helpers are supplied by
// AuxDriverBinding; the other bodies remain ports.
class DriverPorts {
  public:
    virtual ~DriverPorts() = default;
    // CALL [0x9EC36C](dest, source, count), cdecl. CRT identity not deduced.
    // dest is bounded to count bytes; verified_text is the verified label
    // of the source, absent for 0x143F5B6.
    virtual void copy_9ec36c(std::span<char> dest, NativeIdentity source,
                             std::optional<std::string_view> verified_text,
                             std::uint32_t count) = 0;
    // Extension: the four reconstructed helpers return a modern control
    // result, distinct from any native AL (ignored by 98E960).
    // 986B60(key, buffer, 0xFF); buffer bounded to 255 bytes.
    virtual HelperCallOutcome override_986b60(NativeIdentity key,
                                              std::string_view label,
                                              std::span<char> path,
                                              std::uint32_t bound,
                                              Inputs986b60 inputs) = 0;
    virtual HelperCallOutcome
    call_98dd20(startup::StartupServiceBase& service) = 0;
    virtual HelperCallOutcome call_98de80(startup::StartupServiceBase& service,
                                          std::uint32_t arg) = 0;
    virtual void call_9a4750(NativeIdentity service_10e80c0) = 0;
    virtual void call_9a6040(NativeIdentity service_10e80c0,
                             std::uint32_t arg) = 0;
    // 6B19C0: EAX = block, stack = size, ECX = &flag S+0x10.
    virtual void release_6b19c0(BlockHandle block, std::uint32_t size,
                                std::uint8_t& storage_flag) = 0;
    // 674E0C(block), cdecl.
    virtual void free_674e0c(BlockHandle block) = 0;
    // 87B430(0xF1B9A4, 0x11E2F3A, 0x2328), cdecl.
    virtual void call_87b430(NativeIdentity label_f1b9a4, Block11e2f3a& block,
                             std::uint32_t value) = 0;
    // 988FC0: EAX = parser.
    virtual HelperCallOutcome destroy_988fc0(ParserObject& parser) = 0;
};

struct DriverPortSet {
    config_registry::Constructor9896c0Ports& constructor;
    ConfigLoaderPorts& loader; // direct or ExternalLoaderBinding
    SectionParserPorts& sections;
    DriverPorts& driver;
};

enum class ConfigFile : std::uint8_t { gamepc, physics, adddevice, replay };

struct FileTrace {
    bool loader_called = false;
    bool present = false;             // TEST of the block pointer at the site
    std::optional<BlockHandle> block; // captured value
    std::uint32_t size = 0;           // size captured at the site
};

enum class PhysicsRelease : std::uint8_t {
    not_reached,
    freed_674e0c, // flag reread nonzero (98EC84)
    reencoded,    // flag reread zero: 98EC91..98ECAD
};

enum class DriverOutcome : std::uint8_t {
    completed, // modern end after 988FC0; never a native success
    refused_parser_construction,   // construct_9896c0 refused
    refused_loader,                // modern refusal from 6B1A00
    refused_section_parser,        // modern refusal from 438450
    refused_physics_block_unknown, // reencoding on unknown/released block
    refused_physics_size_exceeds_storage,
    refused_override_986b60, // extension: refusal propagated from the helper
    refused_98dd20,
    refused_98de80,
    refused_destroy_988fc0,
};

struct DriverResult {
    DriverOutcome outcome = DriverOutcome::completed;
    std::optional<ConfigFile> refused_file;
    std::string_view refused_section;
    config_registry::ConstructResult construct;
    std::optional<LoaderResult> refused_loader;
    std::optional<SectionParseResult> refused_section_result;
    std::array<FileTrace, 4> files{};
    std::uint32_t section_calls = 0; // calls to 438450 performed
    PhysicsRelease physics_release = PhysicsRelease::not_reached;
};

// Runs 98E960. Precondition: `service` is the stack argument (0x1140360
// at the known site) and `owner` the EAX of the caller 9B4E40. A modern
// refusal stops at the access point: earlier effects are kept, no later
// native effect (988FC0 included) is simulated.
DriverResult
run_configuration_driver_98e960(startup::StartupServiceBase& service,
                                GameModeRuntime& owner, DriverContext& context,
                                BlockStore& store, SectionParserState& state,
                                DriverPortSet ports);

} // namespace tdur::game::config

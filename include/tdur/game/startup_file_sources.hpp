#pragma once

#include "tdur/game/config_external_loader.hpp"
#include "tdur/game/startup_config_drivers.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace tdur::game::config {

enum class FileLeafOutcome {
    copied,
    source_unterminated,
    source_unrepresentable,
    destination_too_small
};
struct FileLeafResult {
    FileLeafOutcome outcome = FileLeafOutcome::source_unterminated;
    std::size_t characters = 0; // Excludes the NUL.
};
// Primary 9AFE90. A forward byte copy, including its overlap effects;
// no case folding, drive normalization or extension removal.
FileLeafResult copy_file_leaf_9afe90(std::span<const char> source,
                                     std::span<char> destination);

struct IntegratedFileContext {
    std::uintptr_t port_state = 0;
};
struct IntegratedSourceState {
    NativeIdentity identity{0xFA4068};
    std::uint8_t available_2ac = 0;
    std::uint8_t use_context_48 = 0;
    IntegratedFileContext context_24;
};
struct IntegratedFileObject {
    std::uintptr_t port_state = 0;
};
struct IntegratedSourceFrame {
    std::array<char, 0x200> leaf;
    IntegratedFileObject file;
};

// These five helpers are not ordinary functions in the official static
// image. Their behavior is required from the host, never synthesized.
class IntegratedFilePorts {
  public:
    virtual ~IntegratedFilePorts() = default;
    virtual HelperCallOutcome construct_622035(IntegratedFileObject& file) = 0;
    virtual std::optional<std::uint32_t>
    lookup_622019(IntegratedFileContext* context, std::span<const char> leaf,
                  IntegratedFileObject& file) = 0;
    virtual std::optional<std::uint32_t>
    size_621be0(IntegratedFileObject& file) = 0;
    virtual IntegratedConfigReturn data_621bd8(IntegratedFileObject& file) = 0;
    virtual HelperCallOutcome destroy_622044(IntegratedFileObject& file) = 0;
};

enum class IntegratedSourceOutcome {
    native_return,
    refused_receiver,
    refused_constructor,
    refused_leaf,
    refused_lookup,
    refused_size,
    refused_data,
    refused_destructor
};
struct IntegratedSourceResult {
    IntegratedSourceOutcome outcome = IntegratedSourceOutcome::native_return;
    FileLeafResult leaf;
    std::optional<BlockHandle>
        block; // Captured EAX, even if destroy mutates it.
};
// Entire primary 9AFF20. size_slot is optional; the gate-false route never
// inspects the input path or constructs the local file object.
IntegratedSourceResult run_integrated_source_9aff20(
    IntegratedSourceState& source, std::span<const char> path,
    std::uint32_t* size_slot, IntegratedSourceFrame& frame,
    IntegratedFilePorts& ports);

struct SynchronousLoaderArgs {
    NativeIdentity service;
    std::optional<BlockHandle>* block_slot = nullptr; // Incoming ECX.
    std::span<const char> path;
    std::uint32_t* size_slot = nullptr; // Third stack argument, optional.
    std::uint32_t capacity = 0; // Fourth stack argument; signed compare.
    NativeIdentity allocator;   // Incoming EAX; null selects FA9CB0.
    std::uint32_t arg5 = 0;
    std::uint32_t arg6 = 0;
};
enum class SynchronousSourceOutcome {
    native_return,
    refused_service,
    refused_lookup_entry,
    refused_entry_size,
    refused_selector,
    refused_prefix,
    refused_prefix_termination,
    refused_prefix_length,
    refused_block_slot,
    refused_callback
};
enum class SynchronousSourceExit { none, zero, callback };
struct SynchronousSourceResult {
    SynchronousSourceOutcome outcome = SynchronousSourceOutcome::native_return;
    SynchronousSourceExit exit = SynchronousSourceExit::none;
    ExternalRoute route = ExternalRoute::not_reached;
    std::uint8_t al = 0; // Valid only on a native return.
    std::uint32_t size = 0;
};
// Entire primary 9A5A70, RET 18h. The buffer-supplied route is distinct from
// 9A5D20: no unconditional slot clear, release on false, or trailing NUL.
SynchronousSourceResult
run_synchronous_source_9a5a70(const SynchronousLoaderArgs& args,
                              ExternalLoaderPorts& ports);

// Composes these actual bodies with the three startup configuration drivers.
// Formatting, prefix selection and freeing remain with the required host.
class StartupFileSourcesBinding final : public StartupConfigPorts {
  public:
    StartupFileSourcesBinding(StartupConfigPorts& inner,
                              IntegratedSourceState& integrated,
                              IntegratedSourceFrame& frame,
                              IntegratedFilePorts& integrated_ports,
                              ExternalLoaderPorts& external_ports)
        : inner_(inner), integrated_(integrated), frame_(frame),
          integrated_ports_(integrated_ports), external_ports_(external_ports) {
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        return inner_.read_selector_1140370_e0();
    }
    HelperCallOutcome format_9ec378(std::span<char> destination,
                                    const StartupPathFormat& call) override {
        return inner_.format_9ec378(destination, call);
    }
    IntegratedConfigReturn lookup_9aff20(NativeIdentity receiver,
                                         std::span<const char> path,
                                         std::uint32_t& size) override;
    std::optional<std::uint8_t>
    load_9a5a70(const SynchronousConfigCall& call) override;
    HelperCallOutcome free_674e0c(BlockHandle block) override {
        return inner_.free_674e0c(block);
    }
    std::optional<IntegratedSourceResult> last_integrated;
    std::optional<SynchronousSourceResult> last_external;

  private:
    StartupConfigPorts& inner_;
    IntegratedSourceState& integrated_;
    IntegratedSourceFrame& frame_;
    IntegratedFilePorts& integrated_ports_;
    ExternalLoaderPorts& external_ports_;
};

} // namespace tdur::game::config

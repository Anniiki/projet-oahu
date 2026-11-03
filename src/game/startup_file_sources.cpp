#include "tdur/game/startup_file_sources.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace tdur::game::config {

FileLeafResult copy_file_leaf_9afe90(std::span<const char> source,
                                     std::span<char> destination) {
    const auto nul = std::find(source.begin(), source.end(), '\0');
    if (nul == source.end())
        return {FileLeafOutcome::source_unterminated};
    const auto length = static_cast<std::size_t>(nul - source.begin());
    if (length > static_cast<std::size_t>(INT32_MAX))
        return {FileLeafOutcome::source_unrepresentable};
    std::size_t begin = length;
    while (begin && source[begin - 1] != '\\' && source[begin - 1] != '/')
        --begin;
    const auto count = length - begin;
    if (destination.size() <= count)
        return {FileLeafOutcome::destination_too_small};
    // 9AFED0..9AFED9 counts against the previously measured length. Using
    // memmove or copying via a temporary would change overlapping inputs.
    for (std::size_t i = 0; i < count; ++i)
        destination[i] = source[begin + i];
    destination[count] = '\0';
    return {FileLeafOutcome::copied, count};
}

IntegratedSourceResult run_integrated_source_9aff20(
    IntegratedSourceState& source, std::span<const char> path,
    std::uint32_t* size_slot, IntegratedSourceFrame& frame,
    IntegratedFilePorts& ports) {
    IntegratedSourceResult result;
    auto refuse = [&](IntegratedSourceOutcome reason) {
        result.outcome = reason;
        return result;
    };
    if (source.available_2ac == 0) {
        if (size_slot)
            *size_slot = 0;
        return result;
    }
    if (ports.construct_622035(frame.file) !=
        HelperCallOutcome::native_returned)
        return refuse(IntegratedSourceOutcome::refused_constructor);
    result.leaf = copy_file_leaf_9afe90(path, frame.leaf);
    if (result.leaf.outcome != FileLeafOutcome::copied)
        return refuse(IntegratedSourceOutcome::refused_leaf);
    // 9AFF4D reads +48 only after construction and basename extraction.
    auto* context = source.use_context_48 ? &source.context_24 : nullptr;
    const auto found = ports.lookup_622019(
        context,
        std::span<const char>(frame.leaf).first(result.leaf.characters + 1),
        frame.file);
    if (!found)
        return refuse(IntegratedSourceOutcome::refused_lookup);
    if (*found != 0) {
        if (ports.destroy_622044(frame.file) !=
            HelperCallOutcome::native_returned)
            return refuse(IntegratedSourceOutcome::refused_destructor);
        if (size_slot)
            *size_slot = 0; // Failure writes zero after destruction.
        return result;
    }
    if (size_slot) {
        const auto size = ports.size_621be0(frame.file);
        if (!size)
            return refuse(IntegratedSourceOutcome::refused_size);
        *size_slot = *size;
    }
    const auto data = ports.data_621bd8(frame.file);
    if (data.control != HelperCallOutcome::native_returned)
        return refuse(IntegratedSourceOutcome::refused_data);
    result.block = data.block;
    if (ports.destroy_622044(frame.file) != HelperCallOutcome::native_returned)
        return refuse(IntegratedSourceOutcome::refused_destructor);
    return result;
}

SynchronousSourceResult
run_synchronous_source_9a5a70(const SynchronousLoaderArgs& args,
                              ExternalLoaderPorts& ports) {
    SynchronousSourceResult result;
    LocalFileObject local;
    ports.construct_609780(local);
    if (args.size_slot)
        *args.size_slot = 0;
    LookupTemps temps;
    auto refuse = [&](SynchronousSourceOutcome reason) {
        result.outcome = reason;
        return result;
    };
    auto zero = [&] {
        ports.cleanup_6097b0(local);
        result.exit = SynchronousSourceExit::zero;
        return result;
    };
    const auto flag = ports.read_service_byte_a71c(args.service);
    if (!flag)
        return refuse(SynchronousSourceOutcome::refused_service);
    bool read_local = true;
    if (*flag == 0) {
        result.route = ExternalRoute::local_direct;
    } else if (ports.lookup_9a4bd0(args.service, args.path, temps) != 0) {
        if (!temps.entry.value)
            return refuse(SynchronousSourceOutcome::refused_lookup_entry);
        const auto size = ports.read_entry_dword_0c(temps.entry);
        if (!size)
            return refuse(SynchronousSourceOutcome::refused_entry_size);
        result.size = *size;
        read_local = *size == 0;
        result.route = read_local ? ExternalRoute::lookup_entry_zero_local
                                  : ExternalRoute::lookup_entry;
    } else {
        const auto selector = ports.read_selector_1140370_e0();
        if (!selector)
            return refuse(SynchronousSourceOutcome::refused_selector);
        const auto prefix = *selector ? kPrefix11404c4 : kPrefix1140444;
        const auto bytes = ports.prefix_string(prefix);
        if (!bytes)
            return refuse(SynchronousSourceOutcome::refused_prefix);
        const auto nul = std::find(bytes->begin(), bytes->end(), '\0');
        if (nul == bytes->end())
            return refuse(SynchronousSourceOutcome::refused_prefix_termination);
        const auto length = static_cast<std::size_t>(nul - bytes->begin());
        if (length > UINT32_MAX)
            return refuse(SynchronousSourceOutcome::refused_prefix_length);
        if (ports.compare_9ec258(prefix, *bytes, args.path,
                                 static_cast<std::uint32_t>(length)) == 0) {
            result.route = ExternalRoute::prefix_refused;
            return zero();
        }
        result.route = ExternalRoute::prefix_accepted_local;
    }
    if (read_local) {
        if (ports.open_609800(local, args.path) == 0)
            return zero();
        result.size = ports.size_609710(local);
        ports.close_609760(local);
        if (result.size == 0)
            return zero();
    }
    const auto allocator =
        args.allocator.value ? args.allocator : kDefaultAllocator9a5d20;
    const std::uint32_t required = result.size + kAllocationSlack;
    if (args.capacity == 0) {
        // 9A5B26 is the first access to the slot, immediately before allocate.
        if (!args.block_slot)
            return refuse(SynchronousSourceOutcome::refused_block_slot);
        args.block_slot->reset();
        *args.block_slot = ports.allocate_slot20(
            allocator, required, kAllocationAlignment, args.arg5, args.arg6);
        if (!*args.block_slot)
            return zero();
    } else if (std::bit_cast<std::int32_t>(args.capacity) <=
               std::bit_cast<std::int32_t>(required)) {
        // Strict signed JLE, not an unsigned >= capacity check.
        return zero();
    }
    if (args.size_slot)
        *args.size_slot = result.size;
    // The supplied-buffer route can read a null value from a valid slot.
    // It does not dereference the block here; preserve that callback input.
    if (!args.block_slot)
        return refuse(SynchronousSourceOutcome::refused_block_slot);
    const auto callback =
        ports.callback_9a5730({temps.entry, temps.second, args.service,
                               args.path, *args.block_slot, result.size},
                              {*args.block_slot, args.size_slot});
    if (callback.control != ExternalCallOutcome::native_returned)
        return refuse(SynchronousSourceOutcome::refused_callback);
    result.al = callback.al;
    ports.cleanup_6097b0(local);
    result.exit = SynchronousSourceExit::callback;
    return result;
}

IntegratedConfigReturn StartupFileSourcesBinding::lookup_9aff20(
    NativeIdentity receiver, std::span<const char> path, std::uint32_t& size) {
    if (receiver != integrated_.identity) {
        last_integrated = IntegratedSourceResult{
            .outcome = IntegratedSourceOutcome::refused_receiver};
        return {};
    }
    last_integrated = run_integrated_source_9aff20(integrated_, path, &size,
                                                   frame_, integrated_ports_);
    return {last_integrated->outcome == IntegratedSourceOutcome::native_return
                ? HelperCallOutcome::native_returned
                : HelperCallOutcome::modern_refusal,
            last_integrated->block};
}

std::optional<std::uint8_t>
StartupFileSourcesBinding::load_9a5a70(const SynchronousConfigCall& call) {
    last_external = run_synchronous_source_9a5a70(
        {call.service, &call.block, call.path, &call.size,
         call.trailing_arguments[0], NativeIdentity{call.eax_at_call},
         call.trailing_arguments[1], call.trailing_arguments[2]},
        external_ports_);
    if (last_external->outcome != SynchronousSourceOutcome::native_return)
        return std::nullopt;
    return last_external->al;
}

} // namespace tdur::game::config

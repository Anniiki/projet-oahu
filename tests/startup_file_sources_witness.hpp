#pragma once

#include "tdur/game/startup_file_sources.hpp"

#include <functional>
#include <string>
#include <vector>

namespace source_test {
using namespace tdur::game::config;

struct IntegratedPorts final : IntegratedFilePorts {
    std::vector<std::string> events;
    std::function<void(std::string_view)> hook;
    HelperCallOutcome construct_result = HelperCallOutcome::native_returned;
    HelperCallOutcome destroy_result = HelperCallOutcome::native_returned;
    std::optional<std::uint32_t> found{0}, size{5};
    IntegratedConfigReturn data{HelperCallOutcome::native_returned, {}};
    IntegratedFileObject* local = nullptr;
    IntegratedFileContext* context_seen = nullptr;
    std::string leaf_seen;
    bool same_local = true;
    void event(std::string_view name, IntegratedFileObject& file) {
        same_local = same_local && local == &file;
        events.emplace_back(name);
        if (hook)
            hook(name);
    }
    HelperCallOutcome construct_622035(IntegratedFileObject& file) override {
        local = &file;
        event("construct", file);
        return construct_result;
    }
    std::optional<std::uint32_t>
    lookup_622019(IntegratedFileContext* context, std::span<const char> leaf,
                  IntegratedFileObject& file) override {
        context_seen = context;
        leaf_seen.assign(leaf.data(), leaf.size() - 1);
        event("lookup", file);
        return found;
    }
    std::optional<std::uint32_t>
    size_621be0(IntegratedFileObject& file) override {
        event("size", file);
        return size;
    }
    IntegratedConfigReturn data_621bd8(IntegratedFileObject& file) override {
        event("data", file);
        return data;
    }
    HelperCallOutcome destroy_622044(IntegratedFileObject& file) override {
        event("destroy", file);
        return destroy_result;
    }
};

struct ExternalPorts final : ExternalLoaderPorts {
    std::vector<std::string> events;
    std::function<void(std::string_view)> hook;
    std::optional<std::uint8_t> flag{std::uint8_t{0}},
        selector{std::uint8_t{0}};
    std::uint8_t found = 1, open = 1;
    LookupTemps lookup_values{{0x1234}, {0x5678}}, lookup_incoming;
    std::optional<std::uint32_t> entry_size{5};
    std::uint32_t local_size = 5, compared = 1;
    std::string prefix = "ROOT/";
    bool prefix_present = true, prefix_terminated = true;
    std::optional<BlockHandle> allocation;
    FileReadCallbackResult callback_result{ExternalCallOutcome::native_returned,
                                           1};
    Callback9a5730 callback_seen{};
    NativeIdentity service_seen{}, allocator_seen{}, prefix_seen{};
    std::uint32_t bytes_seen = 0, alignment_seen = 0, arg5_seen = 0,
                  arg6_seen = 0, compare_length = 0, callback_size_seen = 0;
    LocalFileObject* local = nullptr;
    bool same_local = true, replace_block = false;
    std::optional<BlockHandle> replacement;
    std::optional<std::uint32_t> replacement_size;
    int releases = 0;
    void event(std::string_view name) {
        events.emplace_back(name);
        if (hook)
            hook(name);
    }
    void observe_local(LocalFileObject& value) {
        same_local = same_local && local == &value;
    }
    void construct_609780(LocalFileObject& value) override {
        local = &value;
        event("construct");
    }
    void cleanup_6097b0(LocalFileObject& value) override {
        observe_local(value);
        event("cleanup");
    }
    std::optional<std::uint8_t>
    read_service_byte_a71c(NativeIdentity service) override {
        service_seen = service;
        event("flag");
        return flag;
    }
    std::uint8_t lookup_9a4bd0(NativeIdentity service, std::span<const char>,
                               LookupTemps& temps) override {
        service_seen = service;
        lookup_incoming = temps;
        temps = lookup_values;
        event("lookup");
        return found;
    }
    std::optional<std::uint32_t> read_entry_dword_0c(LookupWord) override {
        event("entry-size");
        return entry_size;
    }
    std::optional<std::uint8_t> read_selector_1140370_e0() override {
        event("selector");
        return selector;
    }
    std::optional<std::span<const char>>
    prefix_string(NativeIdentity which) override {
        prefix_seen = which;
        event("prefix");
        if (!prefix_present)
            return std::nullopt;
        return std::span(prefix.c_str(),
                         prefix.size() + (prefix_terminated ? 1 : 0));
    }
    std::uint32_t compare_9ec258(NativeIdentity which, std::span<const char>,
                                 std::span<const char>,
                                 std::uint32_t length) override {
        prefix_seen = which;
        compare_length = length;
        event("compare");
        return compared;
    }
    std::uint8_t open_609800(LocalFileObject& value,
                             std::span<const char>) override {
        observe_local(value);
        event("open");
        return open;
    }
    std::uint32_t size_609710(LocalFileObject& value) override {
        observe_local(value);
        event("size");
        return local_size;
    }
    void close_609760(LocalFileObject& value) override {
        observe_local(value);
        event("close");
    }
    std::optional<BlockHandle> allocate_slot20(NativeIdentity allocator,
                                               std::uint32_t bytes,
                                               std::uint32_t alignment,
                                               std::uint32_t arg5,
                                               std::uint32_t arg6) override {
        allocator_seen = allocator;
        bytes_seen = bytes;
        alignment_seen = alignment;
        arg5_seen = arg5;
        arg6_seen = arg6;
        event("allocate");
        return allocation;
    }
    FileReadCallbackResult callback_9a5730(const Callback9a5730& call,
                                           ExternalOutputs out) override {
        callback_seen = call;
        callback_size_seen = out.size ? *out.size : 0;
        event("callback");
        if (replace_block)
            out.block = replacement;
        if (out.size && replacement_size)
            *out.size = *replacement_size;
        return callback_result;
    }
    void release_674e0c(BlockHandle) override {
        ++releases;
        event("release");
    }
};
} // namespace source_test

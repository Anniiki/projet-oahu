#pragma once

// Helper 0x986B60 (lookup of a "-key value" option).
// Body read: 986B60..986BB5 (listing 27). cdecl, stack = (key,
// buffer, n); EBX = table, EDI = count, read once (registers).
// The calls [0x9EC25C] and [0x9EC36C] remain ports: neither strcmp, nor
// locale, nor strncpy is assumed.

#include "tdur/game/config_driver_98e960.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::game::config {

using startup::OpaqueWord;

class ArgsTablePorts {
  public:
    virtual ~ArgsTablePorts() = default;
    // 986B70 and 986B9D: host element [EBX + i], read at the site (host
    // width). nullopt = unreadable element (modern refusal at the site).
    virtual std::optional<OpaqueWord> read_entry(OpaqueWord table,
                                                 std::int32_t index) = 0;
    // 986B73: bounded storage of the designated string. nullopt = unresolved;
    // empty view = no readable byte (modern refusal, byte 0 is read).
    virtual std::optional<std::span<const char>>
    resolve_text(OpaqueWord entry) = 0;
    // 986B7F: CALL [0x9EC25C](key, element + 1), cdecl. Only EAX == 0 is
    // tested (986B84). label = verified text of the key, otherwise nullopt.
    virtual std::uint32_t compare_9ec25c(NativeIdentity key,
                                         std::optional<std::string_view> label,
                                         OpaqueWord entry_plus_1) = 0;
    // 986BA8: CALL [0x9EC36C](buffer, value, n), cdecl; bounded buffer.
    virtual void copy_9ec36c(std::span<char> dest, OpaqueWord source,
                             std::uint32_t count) = 0;
};

enum class Outcome986b60 : std::uint8_t {
    native_return,            // 986B98 (AL = 0) or 986BB5 (AL = 1)
    refused_entry_unreadable, // 986B70 or 986B9D
    refused_text_unreadable,  // 986B73
};

struct Result986b60 {
    Outcome986b60 outcome = Outcome986b60::native_return;
    std::uint8_t al = 0;
    std::int32_t index = -1; // copied index, or index of the refusal
    std::uint32_t compares = 0;
    bool copied = false;
};

// Runs 986B60. `buffer` is the bounded view passed to the copy port.
Result986b60 run_args_lookup_986b60(NativeIdentity key,
                                    std::optional<std::string_view> label,
                                    std::span<char> buffer, std::uint32_t n,
                                    Inputs986b60 inputs, ArgsTablePorts& ports);

} // namespace tdur::game::config

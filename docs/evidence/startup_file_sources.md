# Startup file sources: 9AFF20 and 9A5A70

The C++ module `tdur/game/startup_file_sources.hpp` reconstructs the complete
two file-source callers used by the effects, radio and leaderboards startup
drivers. It also implements their filename helper, `9AFE90`. It calls required
host ports for unresolved helpers; it does not call original machine addresses.

## Primary evidence

Reference: official PC MC 1.66 A executable, 16,797,696 bytes, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Rea/Ghidra 12.2 supplied procedure bodies, complete instruction listings and
bounded bytes. The session was exported and closed before implementation.
The byte envelopes were compared mechanically with the same official PE image;
all matched, and the executable's full hash remained unchanged.

| Function | Instructions | Defined bytes | Envelope | Native returns |
| --- | ---: | ---: | ---: | --- |
| `9AFE90` | 36 | 77 | 80 | `RET` at `9AFEDF` |
| `9AFF20` | 46 | 159 | 159 | `RET 4` at `9AFF9D`, `9AFFBC` |
| `9A5A70` | 127 | 397 | 400 | `RET 18h` at `9A5B5C`, `9A5BFD` |

The three-byte alignment gaps in `9AFE90` and `9A5A70` are outside their
defined instruction sets. Envelope hashes, instruction ranges, commands and
the export are retained with the local validation record. No commercial
executable, extracted code or analysis database is distributed by this module.

## Filename helper

`copy_file_leaf_9afe90` first measures the NUL-terminated input, then searches
backwards for the final slash or backslash. It copies the remaining characters
forwards, followed by a NUL. A trailing separator produces an empty name.
Case, high-bit bytes, drive notation and extensions are unchanged.

The copy uses its previously measured length. Overlap can therefore propagate
an overwritten source byte. For example, copying `dir/abcd` to a destination
one byte after its `a` yields `aaaa`, followed by a NUL. `memmove` or an
intermediate string would produce a different result.

The modern interface requires a NUL within the supplied input span and enough
destination storage. Lengths outside the signed 32-bit search domain are
refused. These refusals preserve the destination.

## Integrated source

`run_integrated_source_9aff20` reads the owned source's `+2AC` byte first.
When zero, it returns a null block, writes zero only to a present size output,
and performs no path read or helper call. Otherwise it constructs a local file
object, extracts the name into its 512-byte frame, and only then reads `+48`.
That live byte selects the embedded `+24` context or null for `622019`.

The entire EAX returned by `622019` is tested. Nonzero means destruction occurs
before the optional size is set to zero. On zero, `621BE0` runs only when the
size output exists; `621BD8` supplies the block, which is captured across the
following destruction. A null data result is a native null return, including
when the reported size is nonzero.

`622035`, `622019`, `621BE0`, `621BD8` and `622044` remain mandatory typed host
ports. Their requested addresses did not resolve to usable ordinary bodies in
this static image. A containing procedure returned for a different address
was not used as evidence for an integrated helper. No helper behavior or
successful default was invented.

## Synchronous source

`run_synchronous_source_9a5a70` shares the established low-level port types with
`9A5D20`, while preserving its distinct caller body:

- The local object is constructed before the optional size output is cleared.
  The block output is preserved on early failure paths.
- A nonzero service `+A71C` enables `9A4BD0`. A nonzero lookup AL uses the
  entry's `+0C` size; a zero size falls back to the local-file route. On lookup
  failure, the live selector chooses `1140444` or `11404C4`; the prefix length
  excludes its NUL. A zero full EAX from `9EC258` takes the native false exit.
- Local open failure returns false. A successful open is sized and closed
  before its zero size is tested. Lookup temporaries survive fallback routes.
- A zero capacity clears the output slot immediately before allocation. The
  allocation is `size + 801h` modulo 32 bits, aligned to `10h`; an incoming
  null allocator selects `FA9CB0`. The result is stored before the null test.
- A nonzero capacity skips allocation and must be **strictly greater**, using
  signed 32-bit comparison, than the same wrapped `size + 801h`. Equality and
  unsigned comparisons give different results.
- The optional size output is written before the block slot is read for
  `9A5730`. A null value in a valid supplied-buffer slot is forwarded as null.
  A missing slot is refused only at a reached read or write.
- The callback's raw AL is captured across cleanup. Both false and nonzero
  returns preserve its output effects. There is no release, slot reset or
  trailing NUL write after the callback, unlike `9A5D20`.

Service reads, archive lookup, prefix comparison, local-file operations,
allocation and the `9A5730` body remain required ports. An explicit modern
callback refusal stops before native cleanup, with earlier effects retained.

## Composition and validation

`StartupFileSourcesBinding` supplies both actual source bodies to
`StartupConfigPorts`. It maps the synchronous call's EAX and its three trailing
arguments explicitly. Formatting, prefix selection and freeing remain host
dependencies. The three startup drivers are no longer replaced by high-level
file-success witnesses in the full 51-service composition test.

Six x64 Release CTests passed, totaling **11,175 checks and zero failures**:
`startup_file_sources`, `startup_config_drivers`, `config_external_loader`,
`config_driver_98e960`, `startup_completion` and `startup_lifecycle_binding`.
The tests cover both source priorities, all three drivers, absent files,
fallback, the signed capacity boundary and wraparound, overlapping copies,
optional outputs, raw AL, live context, callback mutations and modern refusals.
MSVC compiled with `/W4 /WX`, one compiler task, BelowNormal priority and the
game guard. Changed C++ files passed clang-format 19.1.5.

These are owned-data tests. No commercial configuration was parsed, no original
function was executed and no game or save was opened. Actual local/archive I/O,
the concrete host services and a playable world remain integration work.

Related: [startup configuration callers](startup_config_drivers.md) and
[derived startup completion](startup_completion.md).

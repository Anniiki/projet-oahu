# Text relay and deferred queue: 718120, 9B4490

The catalogue relay passes the selected text to the real VMF dispatch
`717FF0`. The queue producer keeps deferred requests with its own recursive
lock. These services prepare the callers `89E330` and `89E060`; they do not
yet constitute playable commercial menus.

## Official evidence

Reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The answers from the exported and closed Rea session `vmf-child-services-v1`
are compared with the two files on disk:

| Complete body | Size | SHA-256 |
| --- | ---: | --- |
| `718120..718192` | 115 bytes | `b51bd7c9e9a91050602d68c987eb6360e4aa4094c68cf37eed97813cd33ce564` |
| `9B4490..9B44DB` | 76 bytes | `c8f21bcaa86a7e84683d9aaf81b1ebf568c855cd948541e27c52fdecea9b6484` |

The Evidence identifiers and fingerprints of the six answers stay outside
Git, in `vmf-catalog-display-deferred-runtime-v1/native-proof.json`. This
batch performs no new game launch, native call or memory capture.
The body of the bridge `416450` remains uncorroborated on the primary: the
eleven typed slots are a modern boundary, without emulating an uninitialised
x86 stack.

## One lookup, one resumable publication

`VmfNotificationCatalogDisplayContext` captures the global registry before
reading the owner's selector. It calls `9AE600` exactly once with the 64-bit
ID, then makes a single invocation of the real `717FF0`. The descriptor is
`selector * 0x4C + 0x48` modulo **2^32**, not `selector % 32`. The final zero
DWORD of the call from `89E060` is the high half of the ID.

The lookup precedes the display guards: an overlong name does not hide an
invalid table. A missing ID passes a null format and finishes without reading
the name or requesting its unused services. A 65-byte name finishes before
the request for ownership of the text. Once this guard has been passed, a
wait does not replay it, even if the name changes.

The loader must provide a `VmfNotificationCatalogPoolBinding` for the chosen
descriptor. An unknown slot waits; absence, duplicate or incompatible owner
are explicit errors. The selected slot stays captured during the wait. The
physical addresses and extents must match.
An empty logical view with `data()==NULL` designates a **known empty text**,
different from a missing format: it requires an owned empty reference.

`VmfLinkedText::copy_storage` copies, at load time, a bounded pool terminated
by a real NUL, including the internal NULs and the following bytes.
`VmfTextReference::suffix` shares this storage without allocation or scan; an
uninitialised reference, or one pointing one byte past the end, is refused.
The setter keeps the entire physical suffix after the calling context is
destroyed. Do not replace this path with a copy of the C-string prefix alone.

The formatter is executed only once: an argument that itself contains a
marker stays literal. Waits and errors keep the effects already produced by
the cache and the setters. Keep views, owners, arguments and services stable
and serialise their accesses. The reentry guard of the context is not a
mutex. Its dependencies may allocate.

## A 64-slot queue, 63 normal entries

`enqueue_vmf_notification_deferred` locks the manager's own recursive mutex,
then evaluates `int32(count + 1 modulo 2^32) < 64`. For normal counts 0 to
62, it writes the pointer into the current slot, increments the count, then
sets byte 04 of the object to 1; it unlocks and returns AL1. A full queue
unlocks and returns AL0 without modifying slot, count or object. A null
object that goes unused on this branch is not inspected.

Some corrupted counts pass the guard but designate an invalid index: the C++
reports a range error, distinct from AL0. A null object with room available
is refused before the stores, outside the well-formed native domain; no native
crash prefix is claimed. There is neither deduplication nor callback.

The object is a stable token of the future parent owner, not a 128-byte
identity record. Keep it alive and distinct from the manager's storage.
Access the fields and the flag under the lock of the associated manager. This
lock is distinct from the parent's lock and from the global queue `895AF0`.
The producer does not run the worker and does not cancel the parent store 208
when AL0 is ignored.

## Targeted verification

**48 targeted suites pass on each of the x64 and Win32 architectures**, under
MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`. The display performs
528 checks, including the 316 of its initial delivery; the queue performs
217 checks. The 55 tracked sources and six sensitive files stay unchanged
during validation; the PE architectures and the compilation options are
also verified.

The display test reuses real VMF programs, dictionaries, formatters, cache
and setters with synthetic PC 2.8/Xbox 2.5 data. It covers the guards, the
high ID, the resumptions, the complete pool, the empty text, the host errors
and the single formatting pass. The queue test covers capacity, DWORD
overflows, a duplicated object, recursive reentry and real independence of
the locks.

```powershell
cmake --build build --config Release --target vmf_notification_display_test vmf_notification_deferred_queue_test
ctest --test-dir build -C Release -R '^(vmf_notification_display|vmf_notification_deferred_queue)$' --output-on-failure
```

The test getters remain synthetic. Complete hosts, orchestration, commercial
menus, final native rendering and two-machine co-op remain required.

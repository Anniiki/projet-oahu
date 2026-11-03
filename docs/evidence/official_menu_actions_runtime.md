# Official observation of the global menu commands

## Result and scope

The exclusive `ui-vmf-menu-actions` profile reads nine fixed code ranges,
1,204 bytes, without reading any scalar or following any pointer. The five ranges covering
four established bodies, 980 bytes, match the bytes on disk:

| Body | VA ranges / bytes |
| --- | --- |
| Global handler | `99B7A0/301`, `99B8D0/495` |
| Active-item counter per group | `99A9A0/58` |
| Tail of `frca` | `866040/63` |
| Tail of `frok` | `866080/63` |

The three-byte hole in the handler is excluded. Four chosen windows,
224 bytes, differ from the bytes on disk: `635A80/32`, `634DA0/32`, `634D80/32`,
`622B20/128`. Their code becomes readable after a normal startup. These windows
remain prefixes; reading them does not prove complete bodies.

Official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The commercial bytes stay in the private evidence, outside Git.

## Isolated trial and identity

Isolated copy slot 0, batch
`<isolated-copies>/slot0/20261101T221456-30952`.
PID 12372, FILETIME `134380449365042481`, base `00400000`.
The PID, the exact creation time and the executable match between launcher and
probe; the read handle keeps the identity of the observed process.
The FILETIME is compared as a 64-bit Python integer and published as a string,
because a JavaScript Number cannot represent it exactly.

The instrumented trial ends at the planned timeout after 100.323 seconds. The
slot is released; the 6,697 reference files and 17 saves remain identical.
The Python/C++ relative names are compared after normalizing the separators
only, with no collision and no change to the hashes. Two windows are
enumerated; no UI action or visual inspection is performed.
The existing D3D9On12 bridge serves only this reference, distinct from the
future native D3D12 engine. Protection and activation remain unchanged.

## Verification and limits

The C++ probe uses only QUERY_LIMITED_INFORMATION/VM_READ. The reader checks
path, module, bounds, base allocation, committed executable pages and the
absence of guard pages. It does not test MEM_IMAGE separately. The reads do
not form an atomic snapshot and do not demonstrate that the routines execute.

The catalog enforces its sizes, groups, categories and non-overlap at
compile time. MSVC C++latest/W4/WX builds and self-tests pass in
x64 and Win32; the Python launcher accepts the new profile. The self-tests
target our own process, without launching an additional game.

Private evidence: `reprise-2026-11-01/vmf-global-tail-services-v1/`, notably
`runtime-comparison.json`, `runtime-evidence-manifest.json` and
`runtime-verified/capture/`. The tracking manifest references 24 artifacts;
the previous static phase keeps its own manifest of 39 references.
The four derived subjects keep their window SHA and the provenance chain
to the official process. Since Ghidra refused the raw format,
Rea finally analyzed synthetic PE32 containers, never executed,
with an artificial base preserving the VAs. The
[review of the helpers](vmf_menu_action_helpers.md) checks their exact
limits; neither their SHA nor their headers are those of the commercial PE.

The [handler reconstruction](vmf_fscommand_global.md), the
[shared registry](vmf_menu_registry.md) and the
[contract of the two tails](vmf_fscommand_menu_actions.md) have their own limits.
This trial alone establishes neither the complete helpers, nor playable
reconstructed commercial menus, nor authentication, final rendering or co-op.

# Named notification resolution

`VmfNotificationNamedResolver` reconstructs the control of `7178C0` and
implements the service boundary of the `717BF0` engine. Its formatter
`VmfNotificationMoneyContext` carries `719AD0`. These are owned C++
components; the complete host objects remain to be reconstructed before
playable menus are available. The [wrapper port](vmf_notification_wrapper.md)
now provides the recursive control `717280` and `7171B0`.

## Reference and evidence

Primary reference: official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The Rea disassembly comes from the secondary
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The complete ranges were compared again against the primary on disk:

| Routine | Bytes | SHA-256 of the concatenated ranges |
| --- | ---: | --- |
| `7178C0` | 785 | `12b11305ebb1b803fed068540077e4ad6ff6e2ef1b16a3059e8c69d9a010819d` |
| `719AD0` | 364 | `4427a3d46f384da4a0e1388e7d9f134c2e001536c7244304f6dbae3103b0e795` |

For `719AD0`, the two three-byte padding intervals are excluded.
The private evidence files `vmf-named-backend-runtime-v1/native-proof.json`
and `constants-proof.json` keep the answers, identities and bounds. The second
also rechecks the earlier bodies `716C40`/`898890` and 296 bytes of data.
This does not amount to a new invocation of these routines in the game.

## Branches of `7178C0`

`begin` captures the raw body with its whole suffix: 512-byte copy,
padding after the first NUL, then a NUL forced at 511. The closing search
stays in the parent, after the service. Comparisons are case-sensitive
`strncmp` prefixes, with no requirement that the whole name be equal.

| Prefix | Length | Reconstructed behavior |
| --- | ---: | --- |
| `PLNA` | 4 | Real getter `+1C`, field `+58`, copy padded to 512 bytes. |
| `NEWS` | 4 | Real getter `+30`, captured owner, existing counter `895920` and language effects `895880`. |
| `ACHV_PTS` | 8 | Getter `+4C`, sum `89BD90`, signed representation of the DWORD bits. |
| `LEVEL` | 5 | Getter `+4C`, level `89C210`, six native IDs, opaque selector 0. |
| `COMPL_PCT` | 9 | Two distinct `+4C` calls, sum, x87 computation already ported with an explicit control word. |
| `DRV_MODE` | 8 | Signed mode 1 to 3, three native IDs, selector 0. |
| `RACE_LUX` | 8 | ID `03AB6967`, selector 7. |
| `MY_CLUB` | 7 | Getter `+1C`, inline field `+8C`, exact fallback `ERR:NO_CLUB`. |
| `REW_MAX` | 7 | Reward DWORD interpreted as signed, then real `719AD0` context. |
| Default | not applicable | Real lookup `716C40`, first match, then 64-byte value. |

The first `+4C` result of `COMPL_PCT` is ignored, even when null. The host
global is reread for the second call: a getter may modify it. Its internal
intent is not established. Returned owners and computations already started
survive waits; no getter is replayed. A NULL profile remains a modern
dereference error, distinct from a valid empty club.

An inactive first NEWS record yields zero without requesting an unused
language source. Otherwise, the real counter keeps the two language calls per
visited record, even for universally accepted codes. Callbacks may modify
the following records, under serialized access.

Selectors are not arbitrarily associated with a bank or a language.
An unknown service waits; an established but missing descriptor is an error.
An ID absent from a valid table yields the native fallback. An imported
64-byte value without NUL is refused; only `PLNA` applies a native truncation.

## Money and language effects

`719AD0` starts by writing the destination NUL. Only signed amounts greater
than or equal to 1000 are grouped; negatives stay without a separator.
Remainders are written as three digits. The `us` cache must match on three
bytes, NUL included: `usx` uses a space, despite its unit mode 1.

The empty-cache path reuses `refresh_language`/`run_language` and its
complete `898890` pass: mode, callback suppressions, flags, parameters,
speed label and menu metric when the objects exist. Effects preceding an
absent dependency are kept. If the copied source remains empty, the pass is
required again for the next group.

The global money service is consulted only after the number is written.
A confirmed NULL pointer keeps the number; an unknown binding waits.
A valid owner receives the ID `039D61B2`, a 32-byte buffer of which only
the first byte is explicitly zeroed, and the live numeric argument.
The [recursive formatter](vmf_notification_wrapper.md) implements this boundary
with a distinct adapter per frame. An absent format leaves the buffer
empty; an execution error remains an error. The completed result is
consumed before control returns to the caller.

## Modern contracts and validation

Keep objects, views, inputs and services unmoved, with serialized accesses.
Each recursive invocation must have its own backend. The formatter
explicitly configures its frame pool; no native two-level limit is
inferred. No x86 varargs DWORD is reinterpreted as an x64 pointer.

Budgets count phases; a call to the NEWS counter remains an operation
bounded to 64 records. Capacity and source refusals are modern, preserve
earlier writes and make the context terminal. A reentry is refused without
modifying the active invocation. These engines do not allocate directly on
the heap; their dependencies may allocate.

Build the `vmf_notification_money_test` and `vmf_notification_named_test`
targets, then run the CTest tests of the same name in Release.
Initial validation of this batch: 43 suites in each of the x64 and Win32
architectures,
including 232 money checks and 303 resolver checks. Two preserved scenarios
failed before correction: club fallback and cache copy before suspension.
The 22 tracked sources and six sensitive files are unchanged between the
start and the end of the validation. Getter and money-boundary fixtures are
synthetic in the isolated tests; sums, counters, catalogs, unit effects and
replacement engines are the repository's real implementations. The private
report distinguishes the x64/Win32 checks, the static evidence and the
absence of any game trial. No final rendering, commercial network or
two-machine co-op is validated by these tests.

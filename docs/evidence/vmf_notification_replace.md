# Notification text replacement

`VmfNotificationReplaceContext` reconstructs the `717BF0` engine in modern C++.
It reuses the real comparison/copy helpers and the owned references of the
VMF reader. The complete body, **1 018 bytes in two ranges**, excludes six
alignment bytes and matches the official PC MC 1.66 A primary, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The ten bytes of the marker/fallback constants also match on disk.

## Order of operations

The engine keeps the `1/2/3/other 1` steps, without UTF-8 validation. A
literal copy advances the logical counter by the full width, even when
truncated. Copies operate on live bytes, front to back; the advance then
rereads the first byte, possibly modified by an overlap. The final NUL is
placed at `min(logical_written, capacity-1)`.

`§0§` and `§1§` select argument zero, `§9§` the eighth; the tenth argument is
not reachable through this syntax. A known NULL argument yields `(ERR:2)`. An
unknown binding remains a distinct wait. Copies stop at the first source NUL.
`§10§`, `§1abc§` and `§+1§` are named parameters.

The returned flag means "delimiter encountered". The capacity guard precedes
any new encounter. The search for the closing marker stays **after** the
replacement, even when the destination is full. It stops at NUL; the common
advance can then resume in a physically linked continuation. A read beyond
the backing is refused while earlier stores are preserved.

## Named resolution and lifetime

The required backend receives the body with **the whole suffix**, closing
marker included. Its start is called once; resumptions continue its
execution. A finished result must contain an established NUL within at most
512 bytes; it is copied before handing control back. The fixtures are
explicitly synthetic. The [separate port of the
resolver](vmf_notification_named.md) now provides the `7178C0` check and the
`719AD0` formatter. The complete host implementations remain required; the
[recursive formatter](vmf_notification_wrapper.md) now provides the
`717280`/`7171B0` wiring.

Retained formats keep their storage, not a copy of the contents. Borrowed
sources and the destination must remain stable, with serialised access. The
context is non-movable and refuses reentrancy. An error preserves the writes
already made, without adding a success terminator.

## Verification

Initial validation of this engine: **41 suites in x64 and Win32**,
MSVC Release C++latest,
`/W4 /WX /permissive- /utf-8`, including 75 checks of the engine. These cover
aliases, resumptions, real shared numeric backings and zero ordinary
allocation by the engine. Ten sources and six sensitive files are preserved.

```powershell
cmake --build build --config Release --target vmf_notification_replace_test
ctest --test-dir build -C Release -R '^vmf_notification_replace$' --output-on-failure
```

An external oracle is not adopted globally: six counter-examples are kept. A
later review reports no concrete defect, within stated limits.
Private evidence: `<private-evidence>/reprise-2026-10-31/vmf-notification-replace-runtime-v1/`.
No targeted native invocation, new playable menu or co-op trial is claimed.

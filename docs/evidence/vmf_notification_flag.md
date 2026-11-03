# Help flag and menu write: 8986B0

`VmfNotificationFlagContext` carries the helper called by the parent 89E060
when the level changes and the previous level was zero. It reuses
`find_vmf_active_menu` and then the real `write_vmf_host_integer`. The hookup
to the complete singleton and parent remains required.

## Effects and guards

The routine first writes the raw low byte of its argument into the owner's
FA field. The arguments 2 and 255 therefore stay 2 and 255 in that field.
It then looks for the active menu of group 1, ordinal 0. If no menu is
found, if its type differs from 1 or if its display is null, it finishes
without requiring the variable expression and without calling the writer.

For a valid display, only the writer's argument is normalized to 0 or 1.
The service 440BA0 produces **decimal text**, then uses the real VMF
resolver and dictionary; it does not store a boolean or a float.
The fixed expression at F0A8F8 is an owned binding supplied by the caller.
Its sequence of 14 bytes followed by a NUL is corroborated in both images.
The names embed no commercial resource in this module.

A completed host call does not guarantee an insertion: a missing path or a
full table can be native no-ops. The report therefore exposes
`host_call_completed`. The native EAX return does not establish a boolean
result; the C++ reports remain modern progress information.

## Resumptions and lifetime

The phases `store_flag`, `select_menu`, `write_text` and `finished` preserve
the effects already performed. An unknown registry and a known empty registry
are distinct. A wait for a binding does not rewrite FA. After selection, the
context retains the display: a later change of the registry does not change
its target. Budgets, including zero, execute no phase beyond their limit.

Moves transfer the phase and invalidate the source. A reference error or a
host service error keeps the byte already written, faults the context and
refuses an automatic replay. The owner and the selected display must stay
alive and unmoved; all accesses remain serialized. The owner structure
represents a logical field, without imposing the x86 ABI or reconstructing
the singleton initialization.

## Validation on 01/11/2026

The **74 bytes** of the complete body match the official PC MC 1.66 A
primary executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Rea, PE and DUMPBIN are cross-checked. No new game trial for this batch.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **30 targeted suites
pass on x64 and Win32**. The helper scenario counts 286 checks, including
the verifications of the existing VMF PC 2.8 and Xbox 2.5 fixtures. These
fixtures do not prove the controller coverage of the commercial game.
The tests verify real text, cleanup of the right display, unchanged drivers,
guards, budgets, resumptions, moves, errors and no-ops without false insertion.

```powershell
cmake --build build --config Release --target vmf_get_variable_test
ctest --test-dir build -C Release -R '^vmf_(notification_flag|host_write|host_integer|menu_finalizers)$' --output-on-failure
```

Six frozen sources, six preserved sensitive files, the format check of the
modified lines and the logs remain outside Git in the private evidence
`reprise-2026-10-31/vmf-notification-flag-runtime-v1/`.
Playable commercial menus, online profile, final native rendering, real co-op
and the smoothness of the complete client remain to be verified separately.

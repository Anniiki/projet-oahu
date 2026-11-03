# Notification queue: 895880, 895920, 895A00, 895AF0

The C++ runtime provides the selection and insertion of notifications into
an owned queue of 64 records. It reuses the
[constructor and the language service](vmf_notification_record.md).
Binding this queue to the real game data and receivers is still required.

## Language and searches

`accept_vmf_notification_language` carries 895880. Each evaluation performs
**two real invocations** of the getter, even for a decision that does not
depend on the current language. `resolve_vmf_notification_language`, shared
with 8956C0, then reads the two signed bytes in the live views.

| Notification code | Acceptance |
| --- | --- |
| fr, ge, it, sp, us | Always, after the two calls |
| ch, ja, ko | Only if the computed DWORD equals that code |
| Other codes, including FFFFFFFF | Refused, after the two calls |

`count_vmf_notification_prefix` carries 895920: it counts a contiguous prefix,
stopping at the first `field_04 == 0` or at the first refused language.
It is not a sum of the occupied slots. The active field and the language of
the current slot are read before the getter; its effects remain visible
for the following slots.

`select_vmf_notification_slot` carries 895A00. The ordinal counts inactive
slots **or** accepted active slots; a refused active slot is skipped.
The ordinal remains a DWORD with decrement modulo 2^32. An exhausted search
returns index 0, as in the native code.

## Ordered insertion

`enqueue_vmf_notification` carries 895AF0 with the following steps:

1. Acquire the shared lock, then compute the first prefix.
2. If that prefix is below 64, really recompute it and search for the
   ordinal returned by this **second** call. A variation up to 64 does not
   change the branch already chosen.
3. Otherwise, shift the next 63 records forward, preserving all their
   fields, including the opaque bytes and 990. The assignments explicitly
   follow increasing order, without an overlapping `std::copy`.
4. Insert through the real copy constructor 895800. A source that references
   a slot of the queue observes its content **after** the shift. The destination
   is reconstructed, with its self-copy effects if it is also the source.
5. Release the lock, set the change flag to 1, then visit the five
   receivers. Each pointer is reloaded in turn and receives `notify(0)`.

A receiver can therefore replace a later receiver. An established NULL
pointer skips its call; the bindings are explicitly required at the
construction of the owner. The interface executes real C++ effects.

## Synchronization and errors

`VmfNotificationQueueDomain` holds a real `std::recursive_mutex`, shared by
the owners that share the domain. Its flag is atomic because the native
write occurs after unlocking. Record accesses use this lock; the receiver
registry remains serialized by the caller.
Storages, services and receivers must stay alive and must not be moved.

This modern adapter does not carry the profiling fields and the 606EB0 timer
of the 606B60/606B90 wrappers. It does not claim to reproduce their binary
object. Earlier Rea evidence of the wrappers shows their calls to Windows
critical sections and their additional statistics writes.

A modern language or copy error keeps the effects already performed and
releases the lock. It precedes the flag and the receivers. An error from a
receiver keeps the insertion, the flag and the earlier calls, then stops the
following calls. No error authorizes an automatic replay.
The exposed reports and counters are modern diagnostics.

## Validation on 01/11/2026

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **25 suites pass
on x64 and Win32**, including 164 checks of this batch and the 44 checks of
the constructor. The cases cover the language table, the modifiable searches,
two different prefixes, the second prefix at 64, the shifts and aliases,
the partial errors and the concrete language and unit engine.
Another thread verifies the release of the lock before the callbacks and after
the errors; recursive acquisition on the same thread is also exercised.

```powershell
cmake --build build --config Release --target vmf_notification_queue_test vmf_notification_record_test
ctest --test-dir build -C Release -R '^vmf_notification_(queue|record)$' --output-on-failure
```

The four complete bodies, 498 bytes, match the official primary
MC1.66A on disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
PE, Rea and DUMPBIN are cross-checked. Six frozen sources and six preserved
sensitive files are recorded outside Git in
`<private-evidence>/reprise-2026-10-31/vmf-notification-queue-runtime-v2/`.

An independent review reported no concrete defect on nine frozen sources, 87 923
bytes; a second reviewer verified their hashes and five deliverables. Its
oracles are retained under complete preconditions: the mutation of the fourth
getter takes place during the **first** count, before the second; the copy
sources must be valid until the targeted error phase. The second reviewer
executed the tests.

No new game trial or forced native call for this batch. The callers,
the hosts, the commercial menus, the online profile and co-op between two
clients remain to be wired. These checks do not measure the smoothness of the client.

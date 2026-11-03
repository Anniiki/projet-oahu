# Menu host commands

## Wiring status

The C++ reader stops before commands whose host service is still absent.
The --source-locations diagnostic exposes the physical positions of the action,
the script and the record, without acknowledging the command. The 78 existing
summaries remain identical; 78 additional diagnostics and eight CLI cases pass
on x64 and Win32. No displayed or multiplayer menu is validated by these trials.

On the 19 private PC movies, the fifteen host stops are confirmed
byte-for-byte in both architectures: thirteen FSCommand:sbdd, one FSCommand:zoed
and one GotoFrame2. This observation does not prove the general order of the
scripts between the former Python inventory and the C++ decoder.

## Reference and evidence

The primary reference remains the official PC MC 1.66 A executable, SHA256
3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30.
The new routines below are established statically in the local secondary PE,
SHA256
f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed.
Their corroboration in the official execution remains to be specified per routine.
The 144-byte body of `445CB0` is already present in the historical capture
of trial 25, identical to the secondary but different from the primary on disk.
Its presence is not a trace of execution. The envelope is now ported
(see [vmf_fscommand.md](vmf_fscommand.md)); the complete services and receivers
remain to be wired.
The wrapper `99A6C0` and the router `99A350` are also ported,
with [their own evidence and limits](vmf_fscommand_router.md). Their provenance
remains secondary; their handlers and diagnostic services are distinct.

Private evidence: reprise-2026-10-31/rea/vmf-host-callback-v1 and v2.
The first batch verifies seven bodies (2 102 bytes) checked against PE/Rea/DUMPBIN, eight ranges,
nine bytes of excluded gaps, then eleven bytes of the PE/Rea prefix. It ends
with an explicitly preserved refused request: 9A0770 is not recognized
as a function; its disassembly had been requested before verification.

The second batch closes its 44 requests normally: eleven bodies (2 765 bytes),
twelve ranges, three bytes of excluded gaps, 89 bytes of data and additional
PE/Rea windows. Final snapshot:
02680b06d93d4c03a6dee64e89b0db7a8ac555be19c35e1345c41337e1dd5263.
The sites 718D39 and 78B964 remain outside the functions recognized by Ghidra.
These limits must not be replaced by assumed functions.

## Observed dispatch path

| Step | Routine | Established behavior |
| --- | --- | --- |
| Prefix | 445CB0 | strncmp on the ten bytes `FSCommand:`; case-sensitive; a different prefix is ignored |
| Call | 445D11 | Callback context+9C8; arguments: the command without its prefix, then userdata+9CC; x86 stack: PUSH userdata then PUSH command |
| Initialization | 441700, 9A1360 | Userdata written at 44175C; callback installed at 9A14C4 from host+A44 |
| Default | 99AFF0, 99A6C0 | Default 99A6C0 installed at 99B09A; wrapper to 99A350 with global handler 10E7284 |
| Routing | 99A350 | Without an active movie, return; global handler first, then the movie handler at +AC0 if AL is zero |
| Global handler | 99B590, 99B7A0 | Vtable F07C78 of the sub-object at +F7C, first slot 99B7A0 |

IAT 9EC374 is MSVCR71.dll::strncmp, verified by the PE imports.
The gateways perform no null-callback test. A modern implementation
must explicitly refuse an absent service before calling it.

If the initial flag 35 is set, 445CB0/445D40 call 606B90 before the
callback: this routine calls LeaveCriticalSection. After the callback,
the flag of the then-current context decides whether 606B60 is called, which calls
EnterCriticalSection. The lock is therefore released then reacquired. The two
globals 10E8078/10E807C are then restored to the saved context. The inversion of
these two operations in an earlier analysis is not retained.

## Codes and results

99B7A0 assembles the first four bytes with MOVSX, SHL and OR. On ASCII,
the constants correspond to a four-character code in big-endian order.
Bytes with the high bit set are sign-extended: preserve this effect,
without undefined signed shifts in C++. The following characters take no part
in this selection. The native code reads four bytes without a prior length
check; a modern guard must handle short inputs explicitly.

The fifteen observed global codes are avno, avye, cams, clms, frca, frok,
gein, geou, mcin, mcou, neav, tfoc, tuin, tuou and wend. sbdd and zoed
therefore pass to the movie handler, if it exists. The complete effects of
these fifteen services are not ported yet.

The AL return means that the global handler has processed the command.
A zero return from the last handler may accompany real writes;
99A350 does not use this last result. Do not turn it into a request to
replay the instruction after its effects have already been applied.

## Profile lead and next integration

The sbdd branch of 7509D0 calls 440E00 with /:HasHardcoreMode, then writes
11E28A4: true only if the integer obtained is exactly one. The true path
also calls a virtual method of service 114C15C, then 89C0E0, whose role remains
to be established. The path ends with AL = 0. The neighboring GMLogin string is
a hint; it is not enough to prove the complete link with PC_LOG_IN.

440E00 performs a path resolution, copies the text through 4436A0, restores the
two globals, releases the lock and calls MSVCR71::atoi (IAT 9EC358) if the
read succeeds; otherwise it returns zero. A floating-point conversion would
change the meaning of 1e3, for example. The overflows of the old CRT and
non-ASCII locales are not established by this batch.

The next code must wire these accesses to the owned variables and to the
services of the right menu, with identity guards and resumption tests. A service
that is not reconstructed must remain an explicit boundary. Server responses,
authentication, rendering, input and co-op require their own
implementations and trials.

## Common sbdd branch

The new private evidence rea/vmf-common-sbdd-v1 is closed normally
(47 requests). The sub-object 7188A0 first calls slot 20 of its outer
object; a zero AL continues the common processing. This handler is
not a simple adjustment thunk. Its complete link to the movie remains to be established.

The branch 718D62 calls 9AB950 with EAX = 0, the movie in EBX, then the arguments
011EE900 and 0 on the stack. The service builds the mask of the nine db_* keys
through 440E00, then walks 24-byte rows from a data structure.
The ordinary paths use the link setter 440AB0. The
[loading of TDU_Menus and its row loop](vmf_menu_data.md) are
now reconstructed, with the first definition kept for duplicated IDs, and bounded
resumption. The other calls and the registration with the right receiver remain required.
The [data finalization](vmf_menu_finalizers.md) is also ported:
language/cache, units, callbacks and texts of the three displays. The eight routines
are corroborated in the official process. The finalization context
consumes the finished rows, but does not acknowledge the whole common command.
The [host accesses](vmf_host_variables.md) now cover the three
write forms and the mask computation, without acknowledging this whole service.

After 9AB950, the byte [EBP+46] is read. If it is zero, 718ECD restores the registers,
sets AL to 1 and returns 8. If it is non-zero, 7FC920 then 9827B0 are called,
the byte is cleared and AL is also 1. The new epilogue is verified against the
PE/Rea/DUMPBIN bytes and through three decoded instructions. Do not confuse the
flag observed after the first service with its entry state.

7FC920 writes /:IsTutorial and /:IsMapForTuto as inverse texts according to
state 0/13, then /:IsMapQuit through 440BA0. Its following transitions and the
effects of 9827B0 are not ported yet. A specific handler may
already have produced effects before returning false: continuing the native
chain does not mean replaying the action. An incomplete service keeps an
explicit boundary; a handler boolean is not its completion state.

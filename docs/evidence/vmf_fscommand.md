# FSCommand command envelope

## Origin and scope

`VmfFsCommandContext` reconstructs the static body `445CB0..445D3F`
(144 bytes). The primary reference is the official PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The existing Rea export of the secondary
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`
describes this body. The historical capture of the official process in trial 25
contains the same 144 bytes, SHA256
`7888c9e0ac885ecbdaca9e08bed002c8440e594651576759de728bdc11fd875a`.
They differ from the primary on disk. Their presence in memory does not prove
that this function was executed during the trial.

Private evidence: `reprise-2026-10-31/vmf-fscommand-runtime-v1/native-proof.json`,
export `rea/vmf-host-callback-v1/session/22-procedure_assembly.json` and
`reprise-2026-10-30/vmf-core-runtime-25/runtime25-comparison.json`, entry
`vmf_fscommand`. This batch required no new access to the game or the process.

## Order of effects

The prefix `FSCommand:` is case-sensitive. Any other URL is ignored before
any access to the contexts. The command is a bounded C-string window: its
bytes stay alive and each consumer reads its prefix up to NUL.

The current context `10E8078` is loaded once. Its signed integer `+30`
controls the logging `4374F0` when it is strictly positive.
The context is reloaded only if that call actually took place.
The then-current pointer is saved, and its byte `+35` decides whether
`606B90` is called on the fixed object `114C050`. Only after that call is the
context reloaded again. The userdata `+9CC` is captured before the callback
`+9C8`, on that same owner.

After the callback has actually finished, a new load of `10E8078` provides
the flag that decides whether `606B60` is called. The two flags are
independent: no balanced leave/enter pair is invented. The two globals
`10E8078` and `10E807C` then receive the same pointer saved after logging.
The entry value of the second global is not restored separately.

## Modern contracts and limits

Completion is distinct from the handler's AL result, which this envelope
ignores. A callback that has really finished returns either NULL after its
synchronous effects, or an exclusive frame to be resumed until it ends. Waits
keep the captures and replay no prefix. A NULL userdata is valid; a known
required service that is NULL is an error, an unknown piece of data waits.

Budgets are local; nested progress debits one step. The frame is detached
before it is handed back and freed once. Reentries into the same context are
refused. Errors and abandonment are terminal, keep the effects already
executed and fabricate neither a restoration of the globals nor an enter call.
This abandonment policy is modern, with no native equivalent claimed.
The caller must not continue an abandoned command as if it had finished.

The objects, windows and dependencies stay stable and serialised; preserve
the affinity of the services. No x86 address is called. The logger bodies,
`606B90/606B60` (time, counters and owner included), `99B7A0`
and film-specific receivers remain separate work, with mandatory ports.
Declaring these ports does not recompile their bodies.
The default router is now provided with an owned
adapter; the bodies of its handlers and of the diagnostic remain separate
services. This module consumes no VMF action, does not pop GetURL and does not acknowledge
`sbdd`. The commercial player still requires its real hosts.

## Validation

`vmf_fscommand_test` uses synthetic fixtures and the real owned dictionary:
the callback and its resumptions perform controlled writes.
The scenarios distinguish the four load sites, the captures between waits,
the independent flags, the live strings, the budgets, the faults and the
hand-back at completion, cancellation or destruction. Logger and gate are
call-order fixtures; they do not validate their missing native bodies.
The Release suites remain active under NDEBUG. No commercial menu,
authentication or co-op trial is validated by these tests.

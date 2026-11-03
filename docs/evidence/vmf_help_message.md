# Tutorial and help presentation

## Scope and origin

VmfHelpMessageContext reconstructs the common prefix and the type2 path of
9A03E0. It runs the registered C++ callbacks, resolves the target, publishes
a frame request and writes the variables. The type0/1/other paths, the
manager constructor and the registration of every callback of the game
remain to be ported. The supplied manager must represent an established
state or an explicit fixture; the zeros of the PE do not prove its state.

The primary PC MC 1.66 A reference is identified by SHA256
3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30.
Rea/Ghidra also analyzes the secondary
f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed.
The complete bodies 9A03E0/441180/441560 total 1 065 bytes,
cross-checked against the PE, Rea readings and DUMPBIN. This evidence count
is not a coverage rate of the port: only the announced branches are ported.

Both helpers differ from the primary on disk. An
[official in-memory capture](official_help_message_runtime.md)
confirms the four studied ranges. No cause for these differences is inferred.

## Order of effects

If an earlier message is active, the function callback runs before the
object callback. The selection of the object and its arguments is read
again after the function, even if the function cleared the active flag.
The reconstructed identities are keys; no native code pointer is invoked.

An invocation already selected keeps its key, reason and arguments when
its implementation is missing. Resuming and moving the context preserve
that invocation. The next object is selected only after the function has
actually run. A fault after effects keeps the prefix and forbids its
replay. Reentering the same context is refused; a nested native invocation
requires its own context. Moving a context during run is forbidden.

The id_low/topic/id_high/object_key writes precede the read of the
display. An unknown binding suspends exactly at that read; a display
confirmed NULL allows the remaining writes and then completes.
An unknown world suspends after the target is resolved; a world confirmed
absent does not prevent activation. Borrowed objects must stay alive, and
accesses are serialized.

441180 resolves /tutorial from the root and clears the result on a miss.
441560 picks the label on_group for mode 1, on_group_moto for 2, and the
label on otherwise. A missing label keeps the earlier request but still
applies play1. The modern helper returns a base1 match before clamping, or
nullopt; it does not turn absence into an invented numeric return.
The older apply_goto_label API keeps its stop2 behavior.

The three host calls follow the native order: WhatButton copies empty,
TutorialTitle copies an empty string, TutorialText keeps the live link to
the captured argument9. The latter is not read again from manager.text.
The host_writes counters count completed calls, not dictionary insertions
nor displayed pixels; callbacks counts the callbacks that completed without a
fault. The variables reuse the
[host services](vmf_host_variables.md), including their native no-ops.

## Validation and limits

The tests use synthetic PC2.8 and Xbox2.5 VMF scenes, C++ callbacks with
effects and live text backings. They cover the stops at unknown services,
the NULL presences, the order of the stores, the modes, the missing labels,
the display changes, the faults and the budgets. The observed runs do not
allocate on the ordinary heap.

The retained selection regression failed on both callbacks before the fix:
837 checks, two failures. After the fix, 840 checks pass per architecture.
This fix was tested after the native capture; it changes neither the probe
nor the original bytes compared.

    cmake --build build --config Release --target vmf_help_message_test
    ctest --test-dir build -C Release -R '^vmf_help_message$' --output-on-failure

message_finished ends this presentation, not sbdd nor FSCommand.
The tutorial services 7FBEF0/7FC7F0 and the help service 9827B0 still have
to be wired in full, then rendering and inputs. No usable commercial
screen, online profile or co-op mode is validated here.

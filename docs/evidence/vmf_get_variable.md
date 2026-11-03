# Reading VMF variables from scripts

## Sources and scope

The primary reference is the official PC MC 1.66 A executable, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The Rea assembly used comes from the secondary Paradise
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Official trial 25 had corroborated the body of the handler `445A60` (305 bytes).
The October 31 batch corroborated the bodies `4491A0` (145 bytes) and `4490E0`
(184 bytes). This attests their presence, without attesting that they are
called in game. The wrapper `449070` remains secondary evidence in this batch.

| Routine | Retained fact | Private Rea export |
| --- | --- | --- |
| `445A60` | Pops the name, calls the wrapper, empty text on failure | `vmf-variable-dictionary/session/23-procedure_assembly.json` |
| `449070` | Name present: scope lookup; null name: sprite | `vmf-host-context/session-v2/17-procedure_assembly.json` |
| `4491A0` | First colon takes priority, otherwise slash or local name | `vmf-text-services/session/12-procedure_assembly.json` |
| `4490E0` | Target lookup passed by value, optional outputs null here | `vmf-text-services/session/14-procedure_assembly.json` |
| `443620` | Pushes a typed value, false if the variable is absent | `vmf-action-interpreter/session/58-procedure_assembly.json` |

The first four paths are under
`<private-evidence>/reprise-2026-10-31/rea/`, the last one under
`reprise-2026-10-30/rea/`. The envelopes keep the EvidenceID and SHA256.
The handler loads `F035DF` before `44B2E0` on failure. A static read of both
PE files confirms a NUL at that address, with no new live observation.

## Explicit entry and order of effects

`execute_vmf_actions(display, context, stack, const VmfVariables&, limits)`
adds GetVariable `1C`: 28 opcodes supported, 8 host boundaries.
The three earlier entries keep their contracts and stop on `1C`
before the pop. SetVariable `1D` remains a boundary before consumption.

Get pops the name, keeps its owner, charges its text, then uses the
[classifier](vmf_variable_reference.md). The lookup starts from the current
target, passed by value. It receives the whole path, even when a stored name
itself contains separators. A local name performs no path walk. Scope 0 of the
root is valid.

An absent variable or target pushes an empty text. The current target and the
execution phase stay unchanged; the script continues normally.
A path without a colon produces the typed sprite identity. A trailing colon
designates the empty key, distinct from a missing name. End keeps its existing
behavior of restoring the origin.

Numbers keep their bits; owned text follows the native limit of 31;
links keep their owner and observe the changes of their arena or scratch.
No copy freezes these aliases. The caller binds the same dictionary to the
display and serializes the stack and shared storages between resumptions.
Native diagnostic logging is disabled, as in the other entries. The prepared
operations add neither allocation nor host callback.

## Modern refusals and remaining work

A budget error after the pop keeps the consumed name. Display and dictionary
errors keep their nested diagnostic; a push failure keeps the stack diagnostic,
along with the dictionary diagnostic when it originates there.
The faulty action does not count among the successful actions. The cursor and
the context become faulty; the prefix effects are not replayable.
An instruction stop remains resumable, without a double pop.

This const entry does not close the complete menu cycle. An additional entry
now connects the [dictionary write path and lifecycle](vmf_variable_lifecycle.md),
with explicit activation in the frame driver. The guards on text service 37 and
focus remain active. No rendered commercial menu, co-op mode or final native
x64/D3D12 client is validated by these component tests.

## Validation of 2026-10-31

`vmf_get_variable_test` passes in Release x64 and Win32: **1,034 checks per
architecture**, fixtures included, for the synthetic PC 2.8 and Xbox 2.5
profiles. They cover current targets distinct from the origin, empty paths and
keys, numeric bits, aliases and lifetimes, budgets/resumptions, nested errors
and the absence of prepared allocation. The 9 targeted CTest suites pass on
each architecture, including the 8 existing regression suites. An earlier raw
test harness is not an accepted oracle: its contract/API errors were noted
and the canonical tests were written separately. Receipts, exports and
corrections remain in the private evidence.

```powershell
cmake --build build --config Release --target vmf_get_variable_test
ctest --test-dir build -C Release -R '^vmf_get_variable$' --output-on-failure
```

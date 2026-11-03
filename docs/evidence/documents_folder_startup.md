# Official Documents call during startup

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
A fresh isolated copy ran for 45 seconds with the compiled observation DLL.
The owned diagnostic deadline ended the process; this was not a reproduced
hang or a new-game test. No UI input or window text was collected.

The Documents hook recorded `folder-a-redirected`, success, a current-user token,
and main-image return RVA `005A4885`. The 17 locked source-save files and all
6,697 reference files remained unchanged; the process slot was released.

## Primary code correlation

At image base `00400000`, the observed return maps to `9A4885`. Rea confirms a
six-byte indirect CALL at `9A487F`, immediately before `TEST EAX,EAX`. The hook
observation identifies the API actually reached. The static computed target
associated with the indirect slot is not independently established as its
runtime destination.

Ghidra has decoded instructions here without assigning a function at `9A4840`.
A bounded walk of provider-decoded instruction lengths and direct jump targets
reached 76 instructions and two returns, `9A48CE` and `9A4925`, with no unresolved
jump or exit from the declared window. Calls were assumed to return for this
traversal; this is not a provider assertion of a complete function body.

The observed path requests token access `200EC`, passes folder selector `8005`
(`CSIDL_PERSONAL` with create), token, flags 0 and an output buffer. A negative
HRESULT skips the path update. On success, code appends the static suffix
`\Test Drive Unlimited\` before calling `608DE0`.

Subsequent code calls `608E50`; a nonzero result returns false. The other path
selects the static name `<pc>`, clears two fields in each of 32 records, clears
another flag, constructs a mutex named `SaveQuery mutex`, and invokes `9A47F0`.
The lower-level services and full profile I/O contracts remain unresolved.

## What this resolves

The corrected hook accepts the current-user-token call in this real startup.
Earlier empty import-reference searches did not establish that the call was
absent. Conversely, accepting this startup call does not prove new-game loading,
saved-game resume, controller coverage or cooperative play. No new playable
release is inferred from this observation.

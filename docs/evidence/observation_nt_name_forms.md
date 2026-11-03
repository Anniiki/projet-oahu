# Native path compatibility in isolated diagnostics

This change concerns the project's Win32 observation DLL. It does not reconstruct
a game routine, change activation, or establish the cause of the offline
new-game hang.

## Name handling

Write requests are classified before DOS path normalization. A native
`\??\X:\...` file path can pass the existing confinement checks. Other DOS
device forms are refused rather than interpreted as files in the current
directory. Existing explicit input-device and system-service forwarding remains
in effect.

A bare NT name without a root handle returns `STATUS_OBJECT_PATH_SYNTAX_BAD`.
An absolute name supplied with a root handle returns `STATUS_INVALID_PARAMETER`.
Other unconfined name forms return `STATUS_ACCESS_DENIED`. These are diagnostic
confinement decisions, not fabricated successful device responses.

Trailing separators are removed for canonical parent and protected-root checks,
then restored for the real kernel open. Windows decides whether the requested
target is a directory. Missing-parent errors, reparse refusals and original
installation/save protections remain enforced.

## Documents decisions

The three Documents hooks log bounded reasons for accepted or refused requests,
including null/current-user tokens, invalid tokens and null output pointers.
These events contain no SID, token handle or Documents path.

Each event name retains its first 512 individual lines, then uses periodic
counter summaries. Emitted counters are exact snapshots; detailed reasons above
the limit and a complete final history are not guaranteed. This limit preserves
bounded logging during the 100,001-call flood fixture.

## Native validation

The unchanged hooks failed the new decision checks. With the adverse witness,
invented device, bare-name and absolute-with-root requests incorrectly created
private files, while trailing-separator directory requests failed with a
missing-parent error. Detached witness stderr was unavailable; these red results
were verified through the private files and observation log.

The proposed hooks compile under MSVC Win32, C++ latest, `/W4 /WX`. Four native
tests pass: Documents/path behavior, source-write mutation detection, launcher
lifecycle and late-load refusal. Only source-owned fixtures ran. No successful
new-game session or multiplayer restoration is claimed.

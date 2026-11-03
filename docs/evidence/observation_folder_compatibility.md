# Observation-layer folder compatibility

## Corrected behavior

The Win32 observation layer redirects Documents to private storage and
protects source saves and the reference installation. Two compatibility
defects were reproduced on project-owned witnesses and corrected:

- Opening a path whose parent directory does not exist now returns
  `STATUS_OBJECT_PATH_NOT_FOUND` (`0xC000003A`), mapped by Win32 to
  `ERROR_PATH_NOT_FOUND` (3). Previously the observer returned access
  denied, obscuring the condition a caller may use to create directories
  and retry. A missing final file with an existing parent retains
  `ERROR_FILE_NOT_FOUND` (2).
- Documents queries may supply a queryable token belonging to the same
  user as the current process. The observer now accepts that case for
  `SHGetFolderPathA/W` and `SHGetKnownFolderPath`, returning the same
  private Documents root. It previously rejected every non-null token.

The unresolved-path branch performs no open or create. Reparse points,
unrecognized errors and protected source writes remain rejected. Token
validation queries `TokenUser`, validates both SIDs and compares them
with `EqualSid`; it does not impersonate, elevate or change a token.
Default User (`INVALID_HANDLE_VALUE`), invalid handles and nonmatching
users are rejected. No SID or token contents are logged. A non-null
accepted token adds only the `current-user-token` reason to the existing
sampled folder event.

Microsoft documents the token parameter in
[SHGetFolderPathA](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpatha),
query requirements in
[GetTokenInformation](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation)
and SID comparison in
[EqualSid](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-equalsid).

## Discriminating validation

The missing-parent witness failed before correction and passed after it.
It checks existing-parent/missing-leaf `OPEN_EXISTING`, missing-parent
`OPEN_EXISTING`, and missing-parent `CREATE_NEW`. No test creates or
modifies a source save.

The current-process-token witness likewise failed before correction.
It checks ANSI, wide and known-folder queries, private-root results,
and rejection of Default User and an invalid handle. An actual
different-user token was not exercised; the SID mismatch rejection is
an implementation condition, not a separately executed account trial.

The strict MSVC Win32 build uses C++ latest, `/W4 /WX`. Four targeted
CTests pass after both corrections: `observation_proxy_and_documents`,
`observation_guard_mutation_detected`, `observation_launcher_lifecycle`
and `observation_reject_late_load`, totaling 17.91 seconds. Earlier
failing runs are retained privately. These tests use source-owned
programs, not the commercial game.

## Limits of the actual-game diagnosis

A user trial reached the menu, then failed when starting a new offline
profile. Its private files and text-only Windows error records were
preserved; the original installation and source saves were unchanged.
The access violation's cause remains unknown. These witness fixes
**do not establish that the actual new-game failure is resolved**.

Rea's static inspection of official PC MC 1.66 A identifies
`SHGetFolderPathA`, `OpenProcessToken` and a profile-list literal. Its
reference inventories did not establish the running game's caller or
token argument. A corresponding path in OpenTDU is a secondary lead,
not primary proof. Real offline profile creation, world loading,
control, save/resume and clean exit still need validation before
another playable-session claim.

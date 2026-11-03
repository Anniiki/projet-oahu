# Display selection and identity reading: 99A920, 8911A0

The binding provides the two services required by the child dispatch
`89E330`: selecting a VMF display and looking up identities directly in the
records used by the assignment `894D50`. The complete hosts and this dispatch
remain to be bound.

## Official evidence

Reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
A new Rea session establishes the complete body of `99A920`, 39 bytes,
`99A920..99A946`, SHA-256
`ffb03fc6eb5c558d2c5086aeeb0468c1fd69a7e92b608d2c2873c773f81a7272`.
This body and the 98 bytes of `8911A0` are compared again against the primary
on disk, 137 bytes in total. The answers, Evidence identities and hashes are
kept outside Git in `vmf-menu-selection-runtime-v1/native-proof.json`.
This static analysis executes neither the game nor the original routines.

## Real selector

`find_vmf_active_display(movies, group, ordinal)` calls the real selector
`99A780`, already ported, then checks that the chosen movie has type 1 and
returns its display field. It keeps the choice of the last matching movie
when the ordinal exceeds the number of matches.

A chosen movie of the wrong type yields NULL, even if another movie of the
group has a usable display. A NULL display field stays NULL. The helper
caches no choice and writes no variable. A caller performing two lookups
must run them separately: the second result may change if the movie
registry has changed.

The span represents an already resolved registry. A registry still unknown
must wait at the calling boundary; it does not become a successful empty
span. Keep movies and displays alive and serialize their accesses.

## Identity projection

`find_vmf_notification_record_identity` reads the `encoded_identity_70` field
of each `VmfNotificationResetRecord` at the moment it is visited. It shares
the scan engine of the existing API on contiguous payloads. It extracts no
temporary array of 640 identities and imposes neither `sizeof(record)==128`
nor x86 offsets on the x64 C++ structure.

First match on the two words, a limit of 640 records, an explicit key and an
error only on a missing access are preserved. The historical API still
accepts `{}` without ambiguity: the two functions have distinct names. The
scan resets no field to zero and modifies no identity. Keep the key and the
storage stable during each call.

## Validation

**47 targeted suites pass in each architecture, x64 and Win32**, MSVC
Release C++latest, `/W4 /WX /permissive- /utf-8`. The selector performs 38
checks on real programs and synthetic PC 2.8 and Xbox 2.5 VMF displays,
including a publication by the real setter to the second selected display.
The identity test grows from 49 to 60 checks, with the limits, duplicates
and explicit keys of the new projection. The 50 tracked sources and six
sensitive files remain unchanged during validation.

The first setter fixture expected a numeric value. The existing contract and
the native code `440BA0` establish decimal text: the fixture now checks the
owned text type and `"1"`. Production was not modified to satisfy this
expectation.

```powershell
cmake --build build --config Release --target vmf_menu_display_selection_test vmf_notification_identity_test
ctest --test-dir build -C Release -R '^(vmf_menu_display_selection|vmf_notification_identity)$' --output-on-failure
```

These checks establish neither playable commercial menus, nor complete
hosts, nor final D3D12 rendering, nor a working co-op.

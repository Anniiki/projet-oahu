# Documents API call sites

The observation DLL now records where calls to the three Documents APIs return
inside the executable. This helps connect an isolated runtime observation to
code when static references to the folder imports are unavailable.

Each individual Documents decision can include:

- `caller_image=main` and a hexadecimal `caller_rva` relative to the executable;
- `caller_image=other` or `unresolved`, with `caller_rva=none`.

The return address is captured at the hook boundary, before calling the logging
helper. Module ownership is resolved with
[GetModuleHandleExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw),
using address lookup without changing the reference count. The borrowed handle
is only compared with the main module, never dereferenced or freed. Thread last
error is restored after recording.

No external-module address or name, stack, argument buffer, account field or
save content is collected. Addresses are classified only for individual lines
that survive the existing 512-line limit. Periodic summaries omit call-site
fields because their counters may cover several callers.

## Native validation

The owned witness failed before implementation because the metadata was absent.
Afterward, the verifier maps each main-image RVA through the witness's executable
PE sections and checks the preceding bytes for an actual fixture CALL. It requires
all three APIs and several distinct return sites, rejecting a logger/helper
address substituted for the caller.

MSVC Win32 C++ latest with `/W4 /WX` builds successfully. Four native tests pass,
including the existing 100,001-call flood bound, source-save protection, guard
mutation detection and launcher checks. These results do not identify a caller
in the commercial game or prove that the offline new-game hang is resolved.

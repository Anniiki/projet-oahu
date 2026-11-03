# Observing official UI services

`tdur_probe_ui_services` records metadata needed to connect notifications
and the localized catalog. It supplies neither host-method implementations,
a playable menu nor multiplayer validation.

## Fixed scope

The collector reads host cell `114C15C`, its vptr and virtual entries
`+1C/+30/+4C`. It queries target pages without reading their code or
calling these methods. An independent chain observes `11F2CDC` and
metadata for the two opaque descriptors `0/7`. A nonpositive signed
count skips record and pool pointers. No record or text is read.

Two passes without retry request at most **96 metadata bytes**.
Attempts, partial transfers and actual RPM requests are distinguished.
Seven PE-identity DWORDs add at most **28 bytes**, making 124 in total.
Errors and absences remain explicit; two identical partial observations
never become a successful agreement.

## Identity and isolation

The adapter requires the launch's PID and exact FILETIME, the official
MC1.66A SHA verified under a lock denying writes/deletion, and the measured
PE32 profile. It checks bounds and permissions before reads, with the
adapter rechecking permissions. These operations remain nonatomic:
they prove neither lifetime, absence of ABA nor permission to invoke
a native method.

The isolated launcher's `--ui-binding-probe` option passes this identity,
records the probe checksum and retains the coop-anything lifecycle,
private saves and before/after checks.

## Checks and first observation

**40 targeted suites pass on x64 and Win32**, MSVC Release C++latest,
`/W4 /WX /permissive- /utf-8`, including 245 collector checks and 60
reader checks. Reader fixtures use only their own memory pages. Ten
sources and six sensitive files remain unchanged during validation.

```powershell
cmake --build build --config Release --target ui_service_binding_test tdur_probe_ui_services
ctest --test-dir build -C Release -R '^(ui_service_binding_bounded_core|runtime_ui_service_readonly_probe)$' --output-on-failure
```

Official trial on 01/11/2026, PID `22784`, FILETIME `134379823609035422`:
100.238 seconds, 32 metadata bytes and 28 identity bytes. **Partial**
result: null host, catalog `011F25F4`, zero counts for both descriptors
at the observed point. This establishes neither general absence of
services nor a bank/language association. The 6 697 references and
17 checked saves are preserved, slot released. The trial D3D9On12/DXGI
bridge remains temporary.

Private evidence: `<private-evidence>/reprise-2026-10-31/`, directories
`vmf-service-bindings-runtime-v1/` and `vmf-service-bindings-official-v1/`.

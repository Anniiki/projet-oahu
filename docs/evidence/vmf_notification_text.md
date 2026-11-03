# Text parameters: 716D30, 716CF0, 451C90

The C++ runtime provides the text substitutions used by the notifications.
The buffers are owned; the x86 host context and its ABI are not reproduced.
The functions call no commercial code.

## Byte conversion

`encode_vmf_notification_bytes` ports 451C90: every byte below 80 hex is
copied, the others are expanded into two UTF-8 bytes for the code point of
the same value. Thus A7 yields C2A7. A sequence that is already UTF-8 is
expanded byte by byte: this service does not normalize UTF-8.

The initial non-positive signed guard returns 0 without reading the source.
A positive value is not an output capacity. The limit counts source bytes;
-1 becomes INT32_MAX. The other negative limits and 0 produce an empty
output, but require the first source byte to be readable. An appended NUL
counts in the return value. A source limit reached exactly avoids that NUL;
the native code nevertheless reads the byte following the last payload byte.

The owned spans must cover these reads and all writes. Prevalidation refuses
insufficient bounds or storage aliasing before any write. This error
atomicity is a modern boundary outside the safe native domain, not a
guarantee of the original binary.

## Insertion and clearing

`bind_vmf_notification_text` ports 716D30. Absent optional views represent
C NULL pointers; an internal NUL terminates a supplied view. A NULL name,
a NULL value and an empty name terminate before any access to the context.

Among ten slots of 82 bytes, the first key whose first byte is 0 is chosen.
An identical key already present is not updated: a new slot is consumed.
A full table stays unchanged, even before the modern check of the name
length.

The 18-byte key receives the name, then the actually converted suffix C2A7
and its NUL. A 15-byte name fits exactly; a longer name is an explicit
modern error. The 64-byte value keeps 63 useful bytes, a final NUL and the
`strncpy` padding for short strings. The unused bytes of the key stay
unchanged. The inputs must not alias the chosen slot, including an initially
empty value inside its key. The returned statuses and index are modern
diagnostics.

`clear_vmf_notification_text` ports 716CF0: only `key[0]=0` and
`value[0]=0` in each of the ten slots. The queues stay intact.

## Evidence and validation of October 31, 2026

The bodies 716D30/318B and 716CF0/59B match the official primary on disk;
451C90/115B matches the loaded official process. The latter differs on
disk. Rea, PE and DUMPBIN cover the 492 bytes of the three bodies. The
[isolated memory verification](official_notification_helpers_runtime.md)
also confirms the suffix and the `strncpy` import.

MSVC Release, C++ latest, `/W4 /WX /permissive- /utf-8`: 21 suites pass on
x64 and Win32, including 314 checks from this batch. An independent Windows
oracle verifies the 255 non-zero bytes; the other cases cover limits, NUL,
duplicates, full table, aliasing, queues and refusals without writes. Seven
frozen sources and six preserved sensitive files are recorded in the private
evidence `reprise-2026-10-31/vmf-notification-text-runtime-v1/`. An
independent review reported no concrete defect across six sources/59889B;
a second review verified the two hash tables and five outputs. The slot
oracles are retained under explicit occupancy preconditions, without a
global verdict.

```powershell
cmake --build build --config Release --target vmf_notification_text_test
ctest --test-dir build -C Release -R '^vmf_notification_text$' --output-on-failure
```

The notification constructors, the formatter 717280 and their callers
remain to be connected. No playable menu, native D3D12 or co-op trial is
validated by these checks; the smoothness of the complete client remains to
be measured.

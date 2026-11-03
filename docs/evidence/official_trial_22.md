# Official trial 22: two distinct image extents

## Observation and preservation

Private batch `<isolated-copies>/slot0/20261030T172644-24736/`; PID
31176, creation FILETIME 134378548139721974. The job object terminates the
process at the 120-second deadline (code 1460), then the slot is released. The
6 697 reference files and the 17 digests of the source savegames remain
unchanged. The official file is verified by its SHA-256 and held under a read
lock.

The enriched probe reads the same **seven DWORDs / 28 PE bytes** as before.
The user ceiling is `7FFEFFFF`, QPC `10000000`, base `00400000`.
The `identity_refused` refusal precedes any payload: **0/66 bytes**, no
binding pass. The words received are:

| RVA | DWORD value |
| --- | --- |
| `000` | `00905A4D` |
| `03C` | `00000138` |
| `138` | `00004550` |
| `13C` | `0004014C` |
| `150` | `0A07010B` |
| `16C` | `00400000` |
| `188` | `012AD000` |

## Cause and correction of the diagnostic

Toolhelp retains a module size of **`01BB0000`**, equal to the SizeOfImage
of the official PE on disk. The header present at run time announces
**`012AD000`**. The probe compared these two sizes as a single identity;
this equality is precisely the guard that refuses. No cause for the header
change is attributed to the game, to Detours or to any other mechanism.

The secondary file has five sections and a size of `012AF000`: the four
sections and `012AD000` observed here must not be substituted for it. The
first four sections of both on-disk files end structurally at this last
aligned bound; their headers are not a capture of the runtime sections.

The fix keeps the file/PID/creation identity and requires **the exact tuple
above**, the official base and the official loader size. It reports
`image_size` (runtime) and `loader_image_size` separately. MEM_IMAGE payloads
are bounded at `016AD000` exclusive; the MEM_PRIVATE guards remain distinct.
No arbitrary size below the loader size is admitted. Modified words, modified
secondary tuples and modified bounds, as well as crossings, are tested with
independent fixtures; their official SHA admission remains disabled.

This correction prepares a new observation. It proves no binding, lifetime,
identity of the complete code, integrator execution or permission to call a
native routine. The TDU interface was not driven.

Validation of the fix: **4 700 core checks**, **97 source-owned Windows
checks** and the two binding CTests pass in Win32. The bound tests notably
place an object in an image page wider than the runtime extent; no byte
located beyond it is requested.
The independent review also had the header reads themselves bounded: an
unknown loader refuses before any read, DOS after four bytes, an unknown
e_lfanew after eight, without following its value. The measured profile
requests exactly the seven expected RVAs. The final review no longer reports
any P1/P2 defect.

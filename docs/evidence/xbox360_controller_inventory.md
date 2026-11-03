# Xbox 360 controls and interface: donor inventory

## Goal and state of the evidence

The expected result is a PC version usable end to end with an Xbox or
PlayStation controller: profile, menus, map, driving, shops, activities and
social functions. Adapting the SDL transport or offering driving bindings is
not enough. Any missing action, blocked input or operation that requires
keyboard/mouse remains unfinished work.

This document is an **inventory of sources and contracts**, not a proof of
complete reproduction. No game or console is launched. The first evidence
covers the publisher manual, the image directories and the readable FrontEnd
resources. The delivered catalog holds 2 362 candidate screen/command or
screen/label pairs; none is declared playable without keyboard/mouse on the
sole basis of these occurrences.

## Verifiable anchor of the default profile

Primary documentary source: the **Atari, Test Drive Unlimited, Xbox 360**
manual, an archived copy of the publisher booklet available on
[Games Database](https://www.gamesdatabase.org/Media/SYSTEM/Microsoft_Xbox_360/Manual/formated/Test_Drive_Unlimited_-_2006_-_Atari.pdf).
Consulted on **October 30, 2026**. This external document is not a file
extracted from the ISO and does not prove the configuration actually loaded
by the binary of this ISO.

- PDF of **8 848 429 bytes**, 22 physical pages.
- SHA-256: `3293f6c5133ead0e3af9a0af58e6554f3c5e5b5b5eddd83a28cee278d47520f3`.
- **PDF page 3, zero-based index 2, printed pages 2-3**: vehicle diagram,
  menu navigation and auxiliary menus. The glyphs were checked visually on a
  local rendering, not reconstructed by OCR.
- The booklet designates this layout as its default configuration. No setup
  number is inferred; a "Setup 02" profile found elsewhere must not be
  substituted for this source.
- PDF page 12, zero-based index 11, printed pages 20-21, is a geographic map
  of the island. It does not give the internal bindings of the interactive
  map.

### Rules usable in an independent module

The context is an **explicit input** of the future module. It must not be
inferred from the last button received. The two contracts below are
documented well enough for a pure intent profile; they make no screen
functional on their own.

| Admitted context | Default layout input | Documented intent |
| --- | --- | --- |
| `BasicDriving`, without a modal or an owning auxiliary menu | LT / RT | Brake-reverse / throttle |
| Same context | Left stick | Steering |
| Same context | Right stick / right click | Look / horn |
| Same context | LB / RB | Rear view / view change |
| Same context | A / B / X / Y | Interaction-headlight flash / downshift / handbrake / upshift |
| Same context | Back / Start | Back on the road / pause |
| `MenuNavigation`, screen that actually owns the focus | Left stick or D-pad | Directional navigation |
| Same context | A / B | Confirm / cancel-back |

A profile may simultaneously forward the level, press and release events
coming from the transport. **These events are the contract of the new
module**: the booklet does not prove the native timing rules. Thresholds,
dead zones, curves, repeat, hold delay, scroll acceleration and event
consumption remain to be established. No auto-repeat or double-press
duration may be added and attributed to the donor.

### Contextual reuse of the D-pad

The vehicle diagram documents right for the map, up for the windows, down
for the GPS zoom and left for the auxiliary menu. **This does not authorize
a global binding**: on the same page, the radio, assistance, players and
chronopack menus reuse the D-pad to act on their content. The radio even
distinguishes an activation from a double press, with no quantified duration.

The owner, the entry/exit of the auxiliary mode, its possible timeout and the
priorities over the map are still unknown. In `BasicDriving` explicitly
without auxiliary/modal, a map-opening request is documented; this does not
provide the profile of the opened map. Map, auxiliaries and modals must
receive their own contexts and must not automatically fall back to driving.
The "L" and "R" scroll glyphs in the menus are not converted here into an
undemonstrated physical assignment.

PlayStation glyphs and equivalence by physical position will constitute a
compatibility policy of the new client. They will not be presented as
bindings recovered from the PS2/PSP version, which comes from a different
code base.

## Local identities and read-only access

| Supplied image | Size | SHA-256 |
| --- | --- | --- |
| `ISO/XBOX 360/Test Drive Unlimited (Europe) (En,Fr,De,Es,It).iso` | 7 834 892 288 | `414cef98e784c50d2748c8437c8f75ed226b733c3bf4b801443168d31aad77cc` |
| `ISO/PC/Test Drive Unlimited (Europe) (En,Fr,De,Es,It) (v1.66A).iso` | 3 781 459 968 | `7e950dd23958faf7079425fafe1a04f14fa74f917ec29d7fd371b185cbf6ad48` |

The files are opened read-only; size and mtime are stable during the full
SHA computation. No save game is consulted or changed. After the inventory,
both ISOs were fully rehashed: same SHA, sizes and mtime. The 20 PC banks
present in the selected pairs were also rehashed without difference; see
`post-readonly-audit.json`.

The Xbox image presents a compatibility ISO/UDF volume and an
**XDVDFS/GDFX** game volume: base `0FD90000`, descriptor at `0FDA0000`,
`MICROSOFT*XBOX*MEDIA` signatures at the start and end of the 2 048-byte
sector. The root table is at `E8A9F800`. The bounded inventory read 616 548
bytes of metadata, 218 directory tables and 6 883 files; no absence of
behavior is inferred from the absence of a file name.

NanaZip 7.0/2610.2 identifies the PC DVD as UDF 1.02 (`TDU`). Its inventory
of the Xbox compatibility volume reports an incomplete header and data after
the archive; this is not the inventory of the game volume. The private XDVDFS
reader validates the bounds and links of its directory trees separately.

## Rea capabilities and the Xbox code boundary

The actual catalog is the one kept in
`<private-evidence>/reprise-2026-10-30/rea/shader-variant-hash/session/tools.json`;
the local guards of the Rea clone
`317bf3fe235e655b2d752d3be261cc0da7d00f2b` are also consulted.
`open_binary` announces no XDVDFS or XEX loader. The Ghidra Windows P0 path
admits x86/x64/ARM/ARM64 PE and explicitly selected raw 6502, not XEX/PPC.
The documented big-endian PowerPC ELF32 and DOL paths are distinct and do not
constitute Xbox 360 admission on this host. No Rea session was opened to
force a non-admitted format.

`default.xex`, ISO offset `E8DD4000`, is 12 566 528 bytes, SHA-256
`aaa699a9764f5efbe255ce85c98d640dc55015aa94580d438be03091efdcdb2c`.
The only public format fields examined indicate `XEX2`, 15 options, and
File Format Info at `24F0h`: size 32, encryption 1, compression 1, qualified
by the Xenia header enums as NORMAL/BASIC. **The executable body is neither
extracted, imported, decrypted, decompressed nor executed.** Its full
decompilation and its input behavior therefore remain open.

## Readable resources and PC comparison

Paths and sizes are listed before extraction in the private evidence set
`<private-evidence>/reprise-2026-10-30/xbox360-controller-inventory/`.
Only new files of this set receive the selected resources.

The `.zc` files examined have a `.ZC\0` wrapper, a big-endian total size,
then independent zlib blocks, each preceded by a two-byte big-endian length.
The decompressed banks are big-endian `BANK`, with the name, permutation and
record sections verified by their CRC32 complement. The corresponding PC
banks use little-endian `KNAB`. A resource must not be byte-swapped
wholesale: the Xbox languages examined are UTF-16LE.

The 21 top-level FrontEnd HiRes banks, 7 490 828 compressed bytes, yield
20 Xbox VMF resources; 19 corresponding PC VMFs are readable in the local
official installation. This proves neither the active resolution of the game
nor the coverage of all transitions. The PC comparison explicitly targets
`<installation>/Euro/Bnk/FrontEnd/HiRes`, **not members whose identity would
have been demonstrated in the PC DVD cabinets**. The DVD inventory and this
installed source remain distinct.

First map difference: Xbox VMF, physical member 0 at `1000h` in the
decompressed bank, 825 516 bytes, SHA
`8d11a53e7663d8b047e6a2980886c197f01199e01aa4dba5deca161e2d2608ec`;
PC VMF, member 0 at `19C0h`, 780 940 bytes, SHA
`42c80a821dd132da105c20927ca00a1838dc73fb9c6d46da5200c21ab59ce140`.
772 ASCII strings are common. `/:WhatMouseButton` appears only in the PC
corpus of this pair. **This difference establishes neither the cause of the
map bug nor a mouse replacement rule.**

## Map: first behavior matrix to close

Xbox source: `EuroZ/Bnk/FrontEnd/HiRes/map.bnk.zc`, ISO offset `3118E000`,
651 366 stored bytes, 3 649 536 after decompression. The VMF offsets below
are relative to its physical member 0, identified above.

| Function | Anchor in the donor | Preconditions / semantics still open | PC validation state without keyboard/mouse |
| --- | --- | --- | --- |
| Enter / leave the map | Base diagram; `/:IsMapQuit` at 1909 | Modal/auxiliary priority, opening/closing and focus restoration | Not run |
| Movement / pan | Label `m_MoveMap`, ID 5588630 → text 60675810; map resources present | Effective stick/axes, screen edge, center/cursor, speed, dead zones and clamp | Not run; no stick assigned here |
| Zoom | `/:WhatZoom` at 2023; `m_Zoom` at 509608; tokens `zoin` at 537218, `zoou` at 537578 | Binding, limits, zoom anchor, level/pulse/hold and cadence | Not run |
| Focus / place selection | `/:WhatSpot` at 1937, `/:InfosLoaded` at 3919; `m_Select` at 511732 | Selection by pointer or navigation, neighbor search, information trigger, cancellation of a stale request | Not run; priority to the scenario reported by Anniiki |
| GPS target / route | `m_Target` at 139180, `m_WantToTarget` at 158284; `/:IsTargetOK`, `/:IsTargetPresent`; tokens `targ`, `tgfo` | Confirmation, route creation/clearing, valid place and focus owner | Not run |
| Instant travel / locked place | `m_GoTo` at 141740; `/:IsTeleportOK` at 2075, `/:IsRoadLocked` at 2249 | Variable names prove neither the native predicates nor the allowed accesses | Not run |
| Show / choose filters | `m_Filters` at 2457, `m_FiltersHelp` at 508512; `/:filters_displayed` at 256347; `WhatFilter`, `fiin/fiou/nefi/prfi` | Switching the owner, category choice, checkboxes and state retention; command meaning to be tied to the handlers | Not run |
| Players / friends / join | `m_AllPlayers`, `m_Players`, `m_GoNear`, `m_HS_Players`; filter identifiers present | Offline/online availability, selection, loading and errors; no remote transport enabled | Not run |
| Bookmarks | `m_Bookmarks` at 2531, `m_HS_Bookmarks` at 433316 | Adding, choosing, renaming/text entry, deletion, unavailable destinations, back | Not run |
| Tutorial / challenge / place type states | `IsTutorial`, `IsChaLocked`, `IsLevelOK`, `IsSpotLocked`, `WhatSpotType` | Actual activation, previous state and adapted messages; demo strings do not prove their retail use | Not run |

Localization relations are data: `TDU_Menus.db` is physical member 1 at
`16FA0h`, 22 999 bytes, SHA
`f65352dc551dc1cf12a906a0806cb3c81d53e5652e0b4fa584c801c6bf4ad85b`.
The US language is member 1 at `ACD0h`, 151 444 bytes, SHA
`f74b5d1945400b7a9e729dd66d5dfdddb9819b4b565ed8c2d9831d911f2f3b45`.
Examples of explicit links: `m_Map` ID 6068630 → 59675810,
`m_Filters` 6087630 → 53395810, `m_Zoom` 5960630 → 61005810.
They do not tie a physical button to a handler.

A map test will have to check the successive effects: opening from the pad,
pan/zoom without losing the selection, loading of the information for the
pointed place, filter change, route/GPS, contextual confirmation or refusal,
then return to driving with the focus restored. Hold, release, fast reversal,
two simultaneous directional sources and buttons held across a transition
must be tested separately. Thresholds and durations will not be filled in
from an XInput convention.

## Coverage of all candidate screens and commands

An occurrence of a label, VMF variable or `FSCommand` proves the presence of
that data, not the binding, the path actually executed or its effect. Demo
strings kept in the banks do not prove the activation of these functions in
retail. All validations without keyboard/mouse remain **not run** at this
stage.

| Function / screen | Action to reproduce | Preconditions to close | Current 360 evidence | PC state | Test without keyboard/mouse |
| --- | --- | --- | --- | --- | --- |
| Basic driving | Default profile documented above | No UI/auxiliary owner | Printed manual 2-3 | Intent profile possible; gameplay not wired | To be done |
| Menu navigation | Direction, confirm, back | Actual focus, activatable selection | Printed manual 3 | Semantic profile possible; screen management open | To be done |
| Map | Pan/zoom, focus, select, route, filters, back | States, axis capture, locks, repeat | `map` VMF and languages readable; internal bindings unknown | Different asset; the problem reported by Anniiki is not reproduced here | To be done, priority 1 |
| Auxiliaries / phone | Radio, assistance, players, chronopack and interactions | Owner, entry/exit, interruptions | Manual and HUD VMF; native conditions open | No global success claimed | To be done |
| Profile / text entry / Guide | Create/select and resume a profile, texts, confirmations | Distinguish game from OS UI, storage and cancellation | BOOT/LOG_IN and texts to classify | PC equivalent to define; no external service assumption | To be done |

The inventory below covers the **21 selected top-level HiRes banks**,
including the demo/tooling candidates. The count columns indicate explicit
non-audio strings and localized names joined to the data, not identified
native functions. The PC state only means that a corresponding VMF is found
in the same group of local banks. An absence in this pair is not an absence
of the PC function.

| Screen resource | Functions / flows to preserve or qualify | Commands / localized names | Corresponding PC VMF | Conditions and tests still required |
| --- | --- | --- | --- | --- |
| `map` | Map and sub-modes detailed above | 29 / 95 | Present, different | Focus, data, axes, selection, filters, route, back |
| `BOOT_01` | Startup, arrival at the profile and confirmations | 1 / 0 | Not found in this pair | Identify the screens actually active; start without a keyboard key |
| `LOG_IN` | Profile choice/resume/creation, validation and errors | 5 / 14 | Present | Local profile vs OS service, storage, text entry, back and resume |
| `intro` | Initial choices, transitions and first entry | 6 / 6 | Present | Order, owner and availability conditions |
| `GENERAL` | Shared dialog boxes, confirmations, choices and cancellations | 27 / 23 | Present | Modal stack, initial/restored focus, disabled actions |
| `hud` | Driving, aids, auxiliaries and contextual actions | 85 / 231 | Present | Radio/assistance/players menu, messages and interruptions; possible phone to qualify |
| `hud_cha` | Challenge/race flow, pause, end, continue/replay/quit | 57 / 107 | Present | Race states, validation, abandonment, transitions and device loss |
| `CHALLENG` | Challenge selection and settings, opponents and session | 54 / 120 | Present | Solo/multi modes, vehicle/level conditions, lists, lobby and back |
| `drive_in` | Offered challenges, search, creation/editing, deletion and viewing | 22 / 97 | Present | Available data, confirmations, filters, text entry and errors |
| `ice` | Instant challenges / creation and candidate steps | 17 / 31 | Present | Define the route, accept/refuse, player state and cancellation |
| `gmeditor` | Route editor, points, checkpoints, settings and saving | 62 / 96 | Present | Spatial navigation, contextual actions, text and back without loss |
| `Home` | House, garage, data, albums, contacts and sub-menus | 131 / 313 | Present | Nested lists, vehicle/photo choice, text entry, confirmations and exit |
| `shops` | Purchase/test drive/rental, goods, clothes and related settings | 68 / 143 | Present | Price, compatibility, availability, validation, cancellation and back |
| `car_show` | Vehicle choice and presentation | 5 / 2 | Present | Scrolling, focus, acceptance, camera and exit |
| `avatar` | Appearance, clothes and customization choices | 51 / 72 | Present | Tabs, rotation, variants, validation and cancellation |
| `Club` | Members, requests/invitations, creation, management, challenges and rankings | 66 / 218 | Present | Rights/roles, network state, texts, confirmations and errors |
| `sandbox` | Candidate tutorial aids and flows | 3 / 58 | Present | Actual activation, steps, next/back, exit and resume |
| `gauges` | Candidate instruments and timing | 2 / 0 | Present | Who receives the action, activity states and return to the auxiliaries |
| `common` | Common bank empty in this sample | 0 / 0 | No VMF | Proves no absence of a shared interface |
| `DEMO_END` | Preserved demo resources | 3 / 34 | Present | Retail exposure not established; do not activate implicitly |
| `Viewer` | Candidate viewing/tooling commands | 8 / 0 | Present | Exposure to the player not established |

The private catalog `ui-coverage-matrix.json` keeps **478 distinct command
tokens**, **702 screen/command pairs**, **1 660 screen/localized-name
pairs**, and **1 037 raw TDU_Menus rows**. Each pair carries the ISO path,
the physical member, its hash, the string offsets, the candidate language
joins, the PC presence state and the explicit input/modality/repeat/
preconditions/test-without-keyboard-mouse columns. Unknown values remain
unknown; no `implementation_complete` status is set to true.

This count is exhaustive for the explicit occurrences retained in this
corpus, **not for the functions of the whole game**. Dynamically built
commands, undecoded VMF control flow, LowRes/nested banks, the other
languages, native handlers and OS screens remain to be covered. VMF files
are not executed during this inventory.

The language join keeps the anomalies: four US identifiers and four FR ones
carry several distinct texts in the file; eight rows of the table are not
resolved by this textual join. For example, 6063630 matches the
`m_SoldDirect` and `m_Photo` occurrences. No "first/last value wins" choice
is invented; the native lookup contract and the split by categories remain
to be established.

## Text entry, social functions and system boundary

The resources contain in particular `m_Gamertag`, `m_Chat`, `m_AddToFriend`,
`m_Invite`, `m_Mute`, `m_Unmute`, `m_Isolate`, the club names/settings and
confirmation requests. An occurrence does not prove whether the screen is
drawn by the game, called through the Xbox Guide/virtual keyboard, or unused.
The manual confirms the presence of a Guide button and interactions with its
preferences; it does not authorize treating all these text entries as FrontEnd
code of the game.

Each boundary will have to be classified: internal interface, Xbox system UI
with a PC equivalent to supply, or unqualified command. Text entry for a name,
search, message, profile, club, bookmark or created content must be
completable and cancelable with the pad in the new client if that function
is available. This may require a client-side virtual keyboard, without
promising or reproducing an external Xbox Live service.

## Remaining work and completeness criteria

- Qualify the VMF grammar, its control flow and the binding of commands to
  readable native actions; do not attribute an effect to a four-character
  token from its name alone.
- Close focus ownership, screen stack, activation/deactivation, list
  navigation, map/auxiliary sub-modes and text entry. A transition must
  retain/release inputs according to an explicit contract.
- Measure or establish the axes, thresholds, curves, presses, toggles,
  repeats and double presses for each context. The base profile supplies
  none of these missing parameters.
- Qualify the Xbox `.2DB` glyphs/atlases already listed in the banks and
  their association with actions; no atlas has been decoded or equated with
  a button from its `fla_*` name.
- Tie the installed PC resources to the DVD/MSI/cabinet members for a
  version comparison strictly derived from the two ISOs. This link is not
  implicit from the bank name or the associated executable.
- Complete LowRes variants, nested screens and actually reachable states,
  then each row of the matrix with a verifiable flow. The encrypted XEX code
  and its non-admitted backend remain distinct limits; no workaround or raw
  import with an assumed architecture has been attempted.
- Test end to end with Xbox and PlayStation, fresh start/resume, aids and
  modals, plugging/replugging, device change, text entry and cancellations,
  progression and offline/online errors. The tests must prove that no
  keyboard/mouse is required, including after an error or a screen return.
  None of these trials took place here.

A completeness report will have to count confirmed, tested, blocked and
remaining actions per screen. The only current totals of flows without
keyboard or mouse and of reproduced UI behaviors are **zero**; the delivered
result is the inventory and the documentary anchor, not a playable
experience.

## Format sources and private evidence

The format references were consulted through the GitHub CLI, then pinned by
commit and content hash:
[Xenia, XDVDFS reader](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/vfs/devices/disc_image_device.cc),
[XboxDev extract-xiso](https://github.com/XboxDev/extract-xiso/blob/3f5b62cfe68f000b0e3c8a30104973f3a297948e/extract-xiso.c),
[Xenia, public XEX types](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/kernel/util/xex2_info.h).
These sources corroborate a file grammar, not TDU behavior.

The private evidence set contains `image-identities.json`,
`xbox-xdvdfs-inventory.json`, the PC/UDF listings, the pre-extraction
selection plans, `xex-public-format-metadata.json`,
`selected-bank-inventory.json`, `ui-pairs.json`, `ui-coverage-matrix.json`,
`manual-page-index.json` and `external-format-references.json`. The extracted
game resources and the manual images remain private; they are neither copied
into the repository nor delivered as source code. No OpenTDU code, commercial
executable or resource is imported into the new engine by this work.

The goal of total replication remains active: a successful test of the
transport, the profile, the map or a single screen does not close the other
rows.

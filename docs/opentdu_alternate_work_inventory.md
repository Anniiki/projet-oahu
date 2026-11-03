# OpenTDU work kept outside the main branch

## Reference and method

This inventory is dated 2026-10-30. Reference: the earlier OpenTDU-based
fork (branch `recompilation`, HEAD
`1edb1fa0ac23c1629c05ef66e4543193fcc03b55`, base `573d3d5`).
The parent JSON describes 135 commits of this
history and 191 commits reachable through 42 other references but absent from
this ancestry. It was not modified for this analysis.

The 326 patches were compared with `git patch-id --stable`, passing the Git
output as **bytes**, without PowerShell transcoding. Result:

- 191 alternate commits, **97 patch groups**, no empty patch;
- 74 commits match exactly 54 patches present in the main history;
- 117 commits form 43 groups with no exact match in this history.

This last number is **not** the number of missing features: an adaptation, a
corrected include or a trailing line can change the patch-id. Conversely, a
shared fingerprint guarantees neither compilation on another base, nor the
required dependencies, nor the absence of a later modification. The ancestry
comparison serves only to choose the scope. The important families were also
cross-checked with `git range-diff`, against the HEAD sources and against the
private reports. The appendix keeps the 191 commits without counting each
re-application as a new feature.

The [simulation](opentdu_simulation_inventory.md) and
[display](opentdu_display_inventory.md) inventories remain complementary. The
earlier work serves as a list of requirements and counter-examples; no engine,
old save layout or complete backend is imported into the new official base.
The historical validations below are reported from their logs, without having
been replayed.

## Asynchronous textures: base integrated, safety still under trial

`39a5d08` and `b4dc7ef` share the patch-id
`de258db16c5ab245f88371ffa89e394aae2c27c3`. The base for uploading textures and
generating mips without a systematic GPU wait was therefore integrated. This
does not amount to integrating the **nine distinct corrections** of the texture
review. They are kept in `refs/clone/textures-relecture`, the measurement and
integration variants; their patches have no match in HEAD.

Main files: `source/render/vulkan/render_device.cpp` and `.h`. The private
review report on asynchronous textures distinguishes a regression
introduced by the asynchronism from earlier defects that became visible.

| Order / representative | Requirement to keep in the new backends |
|---|---|
| 01 `f13a275` | A resource stays alive until the end of **all** its recorded/submitted uploads and blits; an older image fence does not prove the end of an upload submitted after it. Per-resource usage numbers, proof of actual completion and staging collection even without a new upload. |
| 02 `d3a16c7` | After a submission/wait error, do not reset pool/fence, read a readback or continue with content assumed ready. Keep the exact result and an explicit stop; a timeout is not a GPU completion. |
| 03 `b18ac10` | If the first draw is going to read mips produced during its recording, produce them before that draw, without deferring their submission to the next frame. |
| 04 `be24075` | Re-uploading an existing texture also waits for its previous readers; the light transition of a fresh image is not enough. |
| 05 `054c522` | At full shutdown, after GPU completion, release the staging buffers and empty the deferred queues still retained. This patch does not fix the shutdown of all engine services. |
| 06 `d8825dd` | Make CPU writes visible before the upload on non-coherent memory. Do not infer portability from a single card whose memory is coherent. |
| 07 `aaeca3d` | Mips: cubemap face consistent between barriers and blits, non-square dimensions clamped to 1, 3D depth distinct from layers. |
| 08 `962e3ac` | Captures: copy→CPU read dependency, successful wait then cache invalidation; an invalidation alone does not publish GPU writes. |
| 09 `4058d84` | A dump request must not keep a destroyed C++ wrapper; explicit cancellation rather than use of a stale pointer. |

The four re-applications of each correction are grouped in the appendix. An
earlier review established these paths statically and checked application,
format and signatures, **without claiming compilation or GPU trial at that
stage**. Variants were then built and the A/B runs prepared; the
private A/B criteria for the texture review separate absence of
regression, cost of mips/fences and stability. The first series were biased by
external load or a stuck driving run; they do not prove a general validation of
GPU lifetime nor "zero stutter".

In the new base, `.2DB` decoding, D3D12 uploads/readbacks and their fixtures
exist. They do not prove all the cases above with streaming, re-upload, early
destruction, faces/volumes and GPU loss. These invariants must guide the tests
of the D3D12 and Vulkan backends; the Vulkan counters must not be copied as an
automatically valid solution for another API.
The Vulkan crash of the old trial at 27.7 s still has no established root
cause: this inventory does not attribute it retrospectively to the uploads.

## French audio, cinematics and Bink

### What has already been taken into the main history

| Variants | Main re-application | Detailed reading |
|---|---|---|
| `ca587c3` | `57920b6`, identical patch | `-introaudio` diagnostic, origin of the sound, language, bus/gain and pause/mute state. |
| `f6fbc82` | Adaptation of `57920b6` | `range-diff` shows the Flash bank name replaced by `FrontEnd/sounds/fe_snd.bnk`; the invalid method call of the first proposal is not a feature to restore. |
| `1dcefbf` | `f7b85a3`, identical patch | Global cinematic announcements read from `Sound/Voices/<language>` with their collection. |
| `132012d`, `f9fa246` | Adaptation of `f7b85a3` | `gs_cinematics.h` include added during integration: the absence of the same patch-id does not mean missing announcements. |
| `11e6333`, `3148c3a`, `7d6e0aa` | `9fab8e7`, identical patch | Actor voices attached to avatars/cinematics, collection lifetime. |
| `5e7c423`, `2dd022a`, `3c58e45` | `94025dc`, identical patch | Stereo tracks resumed after pause, named failures; does not replace the Bink stream correction below. |
| `611b00f`, `c00c9f9`, `3307fc8` | `92e8695`, identical patch | Diagnostic of default materials, not a replacement for the shaders/rendering gaps. |

Files: `game/cinematic/*`, `game/scenaric/entity/entity_avatar.*`,
`entity_cinematic.*`, `entity_sound_emitter.cpp`, `flash/audio/flash_sounds.cpp`,
`audio/gs_audio.*`, `movie/bink_audio_stream.*`. The private French-intro
reports and their follow-up notes record the header/symbol errors that were
corrected and the startup trials. The presence of these calls does not prove a
complete intro heard in French; Anniiki's listening test and the normal
path remained distinct from the diagnostics.

### Three corrections left in the trial references

| Commits equivalent to each other | Sources / requirement | Status and limit |
|---|---|---|
| `9244d0a`, `6be5733` | `game/intro/gm_intro.*`: buffers owned by the intro bank; do not overwrite the arena borrowed by location actors that are still animated. | Absent from HEAD. Memory overlap established statically; the exact actor and full in-game confirmation are not claimed. |
| `f0cbd90`, `c78964f` | `filesystem/gs_file.*`, `gm_intro.*`: distinguish request completed, read successful, bank parsed and cinematics actually present; no null dereference nor repeated reinstallation per frame. | Absent from HEAD. Direct startup is a diagnostic, not the natural LOGIN→INTRO path. No content skipped on error. |
| `4f3e715`, `c37b35e` | `movie/bink_*`, `movie/ed_movie.cpp`, `audio/implementation/ed_audio_stream_entry.*`: PCM packets kept whole, unpublished packet retained, video aligned on the audio actually consumed, end of the movie after the audio drains. | Absent from HEAD. Bounded audio reserve; a main-thread stall can still produce temporary silence, but must no longer discard content nor leave a permanent delay. |

The last patch adds `-moviesync`, observation of the audio position under lock
and removal of the underrun silence as well as of the final padding beyond the
declared PCM length. Movies without a track keep an adapted clock. It does not
replace their player with unbounded background decoding and does not claim to
reproduce opaque Bink wrappers.

Evidence and criteria: the private intro crash report and its Bink metadata,
inspection script and symbol notes, and the private intro crash criteria. The
latter defines three
paths, the expected bank events and the complete PCM end; **these are criteria,
not positive results by their mere existence**. The logs attest preparation and
integration of the variants, not a validated listening session.
The new client will have to rebuild resource ownership, decoding, voice
collection, audio clock and transitions; a KNAB reader is not enough.

## Batch 6-B: local saves, resumption and tutorial

`refs/clone/lot6b` ends at `205841a`. The five commits `d949ce7`, `1261cff`,
`65839f0`, `f21f105`, `205841a` have no patch equivalent in HEAD.

| Commit | Requirement to keep |
|---|---|
| `d949ce7` | Actual write confirmation before progression after avatar, car purchase and house; request number captured with the serialized buffer; an older buffer does not acknowledge a more recent request. |
| `1261cff` | Linking of tutorial destinations and resumptions; placement on an actually loaded road after rental; progression based on actions actually performed. |
| `65839f0` | Localized `tut_*` help texts, one opening per step, after the presentation is available; the visibility of a help text is not the end of the tutorial. |
| `f21f105` | Validation of banks/cinematics, reset of the events of a previous playback before a new one; do not announce a bank ready on a mere pointer. Overlaps part of the intent of the intro crash work, without including its three patches. |
| `205841a` | `-localdealerstock` proposal: real local catalog, verified identifiers/configurations; a local policy is explicit, not an invented infinite server stock. |

Files: `player_data/save_game_manager.*`, `game/scenaric/game_intro_tutorial.*`,
`game/intro/gm_intro.*`, `game/shop/shop_logic.*`, `game/estates/gm_estates.*`,
`game/gm_world.*`, `game/living/gm_living.cpp`, `game/scenaric/spots.cpp` and,
for the v2 corrections, `player_data/saved_garage.cpp`.

### Do not confuse the v1 branch with the v2 proposal

The Git version still keeps `pBoughtSpot` undeclared in
`GMEstates::launchTour`, confirmed in the object of `refs/clone/lot6b`.
This is therefore not a delivered career, nor even a fully compiled series.

The **private v2 patch series**, outside the 191 commits, must be kept
together with its private review report. The v2 corrects:

- stable place pointers for visit/purchase, with no retained reference to a
  stack copy, and the garage height;
- confirmations captured with the serialization; the historical "attempt made"
  return is not treated as a successful write;
- resumption of step 8 **back to the choice in 7** when the purchase did not
  take place, rather than a forced opening of the last dealership;
- reservation of slots before mutating the garage and no payment when the
  actual addition fails; it removes the extra refusals wrongly inferred from
  the v1;
- the real scope of the destination callbacks, putting back unresolved markers
  instead of claiming to have replaced them all.

Patches 03 to 05 remain identical to the v1 according to the report. The v2
announces application/format and static checks, **no compilation or complete
execution**. The confirmation number means a successful write return for the
expected files, not an atomic transaction across files nor a guarantee against
a power cut. Persistent failure, user message, cancellation and resumption
must be handled explicitly in the new base.

The historical blockers remain reconstruction requirements: Ford/bike/SG driver
attachments, the real introduction mission `RSFE0028`, the abandon/success
distinction and the return home. The finding at the time that the IGE sheets
were not found in the port is not proof that the resources are absent from the
official copy. No skip, fabricated reward or fake completed tutorial may
replace these systems. The 600 s rental rule after resumption was a limitation
kept from the port, not an exact restoration of the remaining time.

The local stock remains a **policy proposal**, not active by default in its
historical delivery: a fallback catalog is not a confirmed stock, rare cars
remain tied to their reservations and no shared server replenishment is
reconstituted by this patch. The new base will have to distinguish its offline
mode from a co-op/server stock authority.

## Zone prefetching and sector publication

| Family | What is actually distinct | State against HEAD |
|---|---|---|
| `07d40a4`, `04ca400` | `updateAreaStreaming` entry conditioned on the four zone states; removal of two global drains of all reads/instantiations. `07d40a4` also includes the pool protection so that the patch stands alone. | Streaming component absent. The mutex included in 07 is already taken separately by `5faae8b`; this overlap does not carry the streaming changes. |
| `83f5934`, `2144bb8` | Same patch: prefetching of the bytes of nine neighboring banks on an external task, then the same copy/parse/install on the planned thread. | Absent. References `clone/chargement-zone`, `clone/mesure-zone`. |
| `cec88b4` → `4d2265b` | Same patch: a refused bank must not leave a zone forever in the "in progress" state. | Already taken into HEAD, on the synchronous path. Does not mean prefetching is integrated. |
| `f3910e2`, `3936630`, `95844de` and `02125a2` | Publication of the sector at the end of its instantiation and of its associated data. | Intent already taken by `617f041`. `02125a2` is identical; `range-diff` explains the other fingerprints by physics integration context and trailing line. Not as many different fixes. |

Files: `world/streaming_manager.*`, `filesystem/bank.*`, `filesystem/gs_file.*`,
`world/sector_manager.*`, `world/gs_world.cpp`, `render/file_collection_3d.cpp`.
Prefetching transmits bytes only: mount paths/revision captured on the
main thread, uncompressed files outside BIG/contexts, consumption only for the
valid generation, stop/join before unloading the level, synchronous fallback if
unavailable. The announced limit is 27 MiB; no unlimited budget or parsing
shared between threads is justified.

The initialization, teleport and unload waits are distinct from the two drains
removed. Pool protection and sector publication are correction dependencies,
not proof of a gain on the zones. The state defect after a rejected bank must
also be handled on any new asynchronous path, without blindly reusing the
initial trial path.

Evidence: the private zone-loading report, its patches and its follow-up
review; the private A/B zone criteria. The initial
measurements show waits of 6.7/22.5 ms and frames up to 125.9 ms, **not a
validated B result**. The Z1 to Z7 thresholds and the planned passes do not
replace a verdict. The new KNAB reader is a data building block, not a world
streamer.

## Other families to preserve

| Family outside HEAD / representatives | Requirement and status |
|---|---|
| PSO `812644c`, `7d79ef3`, `604e2bc`, `dcd560c` | Detailed wait diagnostics; adjustable number of compilers (`auto`/1 to 8, default 3); `-smoothpipelines` avoids the forced wait for the non-dependent draws alone; `-finishprewarm` waits for the compilations before the road. These choices remain distinct: a late effect or a longer load are explicit trade-offs, not a demonstrated removal of all stutter. Private unseen-pipelines report, measurement criteria/branches. |
| Profiler `236d402`/`5678df1` and re-applications, then `f5c66fc` and re-applications | Stack per long frame/rare item, then twelve callers to attribute `NtCreateFile`, streaming and compilations. Do not lose this tool by keeping only the first `-sampleprofile` already in HEAD. |
| Rival driver `da82ef8` and re-applications, back on road `4c96e4b` and re-applications | Test bench requested by Anniiki: overtake stationary cars, log distance/commands and put back on the road after a prolonged block. This is not the repair of the game's traffic; a pass that is too short remains invalid. See the simulation inventory and the driving criteria. |
| Police `518de64`, `d0c677b` and re-applications | Periodic diagnostic of the police officer and origin of the navigation loss. No demonstrated correction of police behavior. |
| Physics `38752f8`, `bfb63e2`, `d49a780`, `be12e73`, scenery removal `486c256`/`ac6706d` | Parallel preparations, safe fallback, cost labels and grouped removal. Already detailed in the simulation inventory. The `range-diff` of the scenery removal distinguishes an include/format variation from a new function. The A/B verdicts remain limited. |
| Crash report `f96a401`, `ef2aa14`, `c68e097` | Same patch: operation and faulting memory address, distinct from the instruction address. Absent from HEAD despite its base crash report. The new telemetries must not lose this information. |
| Vulkan WIP `789bd2e` | Swapchain failure/minimization/recreation: avoid commands on an invalid backbuffer and preserve the reuse of CPU lists. Work kept **not compiled/not executed**; the cause of the original failure is not established. Do not count it as a delivered repair. |
| D3D12 prototype `80357aa` | Old complete experimental backend, probes/shaders/conversions and handover. Not integrated into the main fork, no validated commercial scene rendering, clipping still reported. The new base took separately the generic NativeDevice component and its own benches, not the engine/DXVK of this prototype. |

Several references named "integration" group these trials; their name proves
neither a merge into `recompilation` nor a validation of the combination.
The gearbox, gamepad, pools, option registries, framebuffer and hot path variants
already taken by identical patches appear in the appendix: their requirements
remain those of the primary inventories, without double counting.

## Resumption on the official base: list of requirements

1. Rendering: lifetime and synchronization contracts covering uploads,
   re-uploads, descriptors, mips and captures; explicit error results.
2. Content: French voices/announcements, movies with no lost audio packets,
   consistent audio clock and end; intro resources owned and validated.
3. Career: purchases and progression only after an action and an actual write,
   consistent resumption, stable pointers/identifiers, refusal without partial
   mutation.
4. Offline/co-op: local stock declared as a fallback policy; a future shared
   authoritative state kept distinct; no fake mission/tutorial result.
5. World: bounded read-ahead, cancellable generation, byte ownership, final
   state published after installation and an error that releases the wait.
6. Validation: keep the prior criteria, the rival driver and the attribution
   tools, isolate caches/load/saves, separate compilation, synthetic test and
   game.

The inventory covers the 42 references of the JSON and the private archives
cited, not any undiscovered historical folders. It announces no complete port
of these features. No game, old source or commercial executable was modified
or launched for this inventory.

## Appendix: exhaustive accounting of the groups

The following tables are produced from the two lists of the JSON. The
12-character prefix of each patch-id is unique in this computation. A match
indicates **the same patch at a commit of the main history**, not global
identity of two trees. The groups without an exact match are qualified in the
sections above; the absence of a match alone does not prove the absence of the
function.

### 54 groups with an identical patch in the main history

| Stable patch-id | Alternate commits | Main commit | Historical subject |
|---|---|---|---|
| `7bdfc2aa8572` | `2229a5d` | `e234a9f` | Review patch 01-bibliotheque-ecritures-reprises.patch (base 60d08f2) |
| `dd946306ef35` | `012b106` | `6a22aa4` | Review patch 02-arret-compilateurs-deux-sorties.patch (base 60d08f2) |
| `d8417dbffa9b` | `6e6b29a` | `3f61b2a` | Review patch 01-compteurs-coherents.patch, v2 (rebased on the profiler) |
| `96f7ced1ba1d` | `790b1b9` | `258e6eb` | Review patch 02-frontiere-et-attente-plafond.patch, v2 (rebased on the profiler) |
| `35601e4d210e` | `822bb26` | `f8ee1b1` | Review patch 03-duree-murale-non-bornee.patch, v2 |
| `8e2fceb87089` | `f9287f8` | `a0cc5b3` | Patch 01-diagnostics.patch, v2 (base 60d08f2) |
| `e7f2aae881bd` | `edcf9b2` | `e6ff4af` | Patch 02-descripteurs-par-lot.patch, v2 (base 60d08f2) |
| `b47176c8777b` | `b64b4c6` | `9df28ac` | Patch 03-index-framebuffers.patch, v2 (base 60d08f2) |
| `e61c4d7cd77f` | `acd658e` | `a2a0dc5` | Patch 04-liaisons-cpu-shaders.patch, v2 (base 60d08f2) |
| `7f07fd86f0f3` | `a534a66` | `fee8aa5` | Patch 05-references-textures.patch, v2 (base 60d08f2) |
| `dafafe73b1ae` | `12d1ea3` | `27b96b2` | Profiler: call chains |
| `70d46d908f56` | `00e051b` | `f32cea6` | Profiler: pieces of the same function merged |
| `6dbae73eda15` | `cdbc9da` | `c2cee40` | Timing: pipeline cache |
| `9d340f39bb65` | `5777f96` | `3650e61` | Recipes: overflow reported once |
| `e18db1b2b447` | `f8dac28` | `024bebc` | Formatting: lines over 80 columns |
| `a4c51b81150a` | `0743ee1` | `439ec5d` | Rendering: previous draw's render pass kept while the targets do not change |
| `18ffc7635a95` | `babbb8c` | `9591304` | Rendering: previous draw's framebuffer kept while its views do not change |
| `14c8df75ed9a` | `99a52cd` | `b26ac01` | Rendering: descriptor sets looked up by hash |
| `30dd45c6d0d1` | `5f2b105`, `6897eb1` | `65c2ce1` | Review patch 01-diagnostic-framebuffers.patch |
| `c68c131495ab` | `03c6255`, `3c5a0dc` | `a1972e8` | Review patch 02-reprise-passe-framebuffer.patch |
| `6d1858d63a7f` | `a6b0f94`, `386697e` | `448e672` | Review patch 03-reconstruction-cibles.patch |
| `43c646dd9f62` | `dc9959f`, `1abe3cd` | `51b80de` | Review patch 04-profileur-portee-suspendue.patch |
| `e5287b5fafb8` | `caae917`, `a5de108` | `2d63ecb` | Patch 01-diagnostic-attentes.patch (traffic intersections) |
| `f91175f3dcfb` | `bbb58f7` | `eb466ea` | Patch 01-etat-pipelines.patch (batch 5, hot paths) |
| `11b617716bac` | `8552e1c` | `ab85b27` | Patch 02-constantes-inchangees.patch (batch 5, hot paths) |
| `7839645c9283` | `6eec3c8` | `d3084b3` | Patch 03-commandes-reutilisees.patch (batch 5, hot paths) |
| `f5cc15532573` | `9d562d5` | `db08ddf` | Patch 01-loi-pedale.patch (automatic gearbox) |
| `931f25040184` | `04abb3f` | `60abd63` | Patch 02-journal-passages.patch (automatic gearbox) |
| `08c2265394e8` | `92b4202` | `476cde2` | Patch 03-reprises-stables.patch (automatic gearbox) |
| `0ffaecb3f23b` | `f023d49` | `3b74515` | Diagnostic: -testautopedal applies to the player's traffic driver |
| `3aa48442e53e` | `6f34c35` | `39d4b37` | Diagnostic: profile of the instantiation thread while the main thread waits for it |
| `de258db16c5a` | `39a5d08` | `b4dc7ef` | Rendering: texture upload and mip generation without waiting for the GPU |
| `a52b1d4ea136` | `ca587c3` | `57920b6` | Patch 01-diagnostics.patch (French intro) |
| `4f29c1ccab46` | `1dcefbf` | `f7b85a3` | Patch 02-annonces-cinematiques.patch (French intro) |
| `fcfaba6a3b0c` | `11e6333`, `3148c3a`, `7d6e0aa` | `9fab8e7` | Patch 03-voix-avatars.patch (French intro) |
| `dade6b36983e` | `5e7c423`, `2dd022a`, `3c58e45` | `94025dc` | Patch 05-lecture-stereo.patch (French intro) |
| `27564914f4f3` | `611b00f`, `c00c9f9`, `3307fc8` | `92e8695` | Patch 06-diagnostic-materiaux.patch (French intro) |
| `ea49fd45893d` | `88a6173`, `4db8dcb`, `3224940` | `86bb170` | Diagnostic: cost of each item over each -timing period |
| `581db5970eb6` | `6454f7a` | `743e5ae` | Patch 01-disposition.patch (gamepad) |
| `d3113b141b74` | `8194960` | `556e95a` | Patch 02-menus.patch (gamepad) |
| `c7113d5a63dd` | `40f3f56` | `ebb529c` | Patch 03-affichage.patch (gamepad) |
| `c4a95d83e847` | `f0d991f` | `c97c496` | Physics: terrain height field built at its real size |
| `6c798342ab7b` | `00a6b69` | `7966402` | Diagnostic: -continue resumes the profile like the Continue button |
| `68c916151bea` | `e3f6741` | `43fed28` | Diagnostic: -newgame restarts the profile like the New Game button |
| `b86e5c108c3b` | `7632a5b`, `75376eb`, `b860629` | `14e9c54` | Automatic gearbox: upshift held back after a sudden throttle lift (option) |
| `e35dfbbf3f50` | `1251763` | `b9747f0` | Automatic gearbox: hold measured and released before the rev limiter |
| `42f5ca00137b` | `02125a2` | `617f041` | World: sector reported installed only at the end of its instantiation |
| `f5f0e9eed0cf` | `59da993`, `81e3584`, `e25953d`, `6f6fa62` | `5faae8b` | Rendering: instance pools protected across threads |
| `cce4c357e50b` | `73d158c` | `63cadda` | Diagnostic: instance pool audit with -poolcheck |
| `807fa24b8e30` | `572dbbe` | `8be4ea5` | Memory: pool nodes aligned within their sub-blocks |
| `3b182a234fe6` | `fbaef61` | `313c4ba` | Rendering: command reserves sized with their nodes |
| `8188726b1b32` | `cec88b4` | `4d2265b` | World: release of zones whose bank was refused |
| `0f2fa9b2671b` | `dd63530` | `3cbf9a0` | Validation: placements and audits of the real pool allocator |
| `f1f34e633f80` | `3d125f7`, `1d88267`, `17b215d` | `1edb1fa` | Options: command-line registry raised to 128 entries |

### 43 groups without an exact match

| Stable patch-id | Alternate commits | Historical subject |
|---|---|---|
| `5cd44901ccdc` | `d949ce7` | Patch 01-points-de-reprise.patch (batch 6-B) |
| `47a4b44e4a30` | `1261cff` | Patch 02-raccords-et-reprise.patch (batch 6-B) |
| `6954eb59a5f2` | `65839f0` | Patch 03-dialogues-tutoriel.patch (batch 6-B) |
| `765dc6785f9f` | `f21f105` | Patch 04-cinematiques-intro.patch (batch 6-B) |
| `817ee3dbf30c` | `205841a` | Patch 05-stock-local-explicite.patch (batch 6-B) |
| `8c9b680f916b` | `f6fbc82` | Patch 01-diagnostics.patch (French intro) |
| `25381a162da8` | `132012d`, `f9fa246` | Patch 02-annonces-cinematiques.patch (French intro) |
| `f47b4edfb388` | `f13a275`, `e45b798`, `0062a94`, `89dbf7a` | Rendering: a texture is released only after its uploads finish |
| `9707a5fe9434` | `d3a16c7`, `7c925b9`, `b025081`, `7c9c093` | Rendering: hard stop on a Vulkan submission or wait failure |
| `84f0b161b410` | `b18ac10`, `e53853c`, `23a6311`, `c6575c2` | Rendering: mips generated before the draws that read them in the frame |
| `f189150f42ed` | `be24075`, `ac34fbf`, `245e754`, `b39f29c` | Rendering: a texture re-upload waits for the previous readers |
| `525b4947fac7` | `054c522`, `ee34676`, `f62719c`, `1dfdd3d` | Rendering: staging and deferred queues released at complete shutdown |
| `539a52885847` | `d8825dd`, `7c18211`, `04f437a`, `4ff5abb` | Rendering: CPU writes made visible before the texel upload |
| `5862cdebcbf9` | `aaeca3d`, `aafff91`, `19272fa`, `d44ee3b` | Rendering: mips for cube faces, non-square textures and volumes |
| `e0104ce1f977` | `962e3ac`, `3843d71`, `10bbd23`, `bceefc8` | Rendering: capture data made visible to the CPU |
| `3218d1b9b3d8` | `4058d84`, `1f6ed38`, `a5d610d`, `f4521c6` | Diagnostic: dump requests cancelled when their target is destroyed |
| `11a08b11e22b` | `38752f8`, `30abd04`, `dc5eb6c`, `5b64227` | Physics: sector collisions converted on other cores |
| `dc742093e3d9` | `bfb63e2`, `6ae05d0`, `9f335cb`, `d33cb8c` | Physics: height field shape built ahead of time |
| `b47d3791dd66` | `236d402`, `5678df1`, `f183a98`, `8dfe960`, `918ba7a`, `c165ffe` | Profiler: stacks of each long frame and rare cost items |
| `1f4e7365c38f` | `da82ef8`, `9113c4c`, `441afda`, `7613dbc`, `fc77054`, `e6a016b` | Diagnostic: open-world rival test driver (-testpad rival) |
| `130d382ba2a9` | `9244d0a`, `6be5733` | Intro: banks loaded in their own memory, no longer in the locations' memory |
| `5e9f5f52f85a` | `f0cbd90`, `c78964f` | Intro: bank ready only when its read and its cinematics are |
| `795db409e15a` | `4f3e715`, `c37b35e` | Movies: Bink audio kept whole, video aligned on the audio |
| `7fa4f94fc3fe` | `f5c66fc`, `830c908`, `2a8288f`, `e8bd893`, `309a1d0` | Profiler: twelve callers for the long-frame call chains |
| `43c50a2e62de` | `d49a780`, `7dfcf05` | Physics: sequential preparation if the asynchronous launch fails |
| `47352d948015` | `be12e73`, `204662d` | Diagnostic: terrain shape counters named for what they count |
| `dee774d59935` | `486c256`, `202e451` | Physics: scenery bodies of an exited sector removed in one pass |
| `2d87710f4f68` | `ac6706d` | Physics: scenery bodies of an exited sector removed in one pass |
| `8d8ab0c34fca` | `4c96e4b`, `76301bc`, `7977f93`, `4672438`, `951f146` | Diagnostic: rival driver put back on the road when it stays stuck |
| `268cee5702c2` | `812644c`, `c2a360f`, `79c3c0a` | Diagnostic: pipeline waits attributed one by one under -timing |
| `d08d5b93a85c` | `7d79ef3`, `efa62ef`, `c6d8061` | Pipelines: adjustable number of compilation threads (-pipelinethreads) |
| `efc011b6444f` | `604e2bc`, `6f6ef41`, `dfe5618` | Pipelines: -smoothpipelines option, no forced wait at the deadline |
| `9f02edba9cf8` | `dcd560c`, `bbd8dc8`, `663c0ac` | Pipelines: -finishprewarm option, prewarming finished before the road |
| `fa0d77133fca` | `518de64`, `24efbcc`, `4807757` | Diagnostic: traffic police officers sampled every second (-policelog) |
| `d76aec4e1f2e` | `d0c677b`, `e5b7f54`, `5fa70ff` | Diagnostic: origin of a police officer's navigation loss |
| `714437d65d6d` | `f3910e2`, `3936630` | World: sector reported installed only at the end of its instantiation |
| `fbd9fbf7dcb7` | `f96a401`, `ef2aa14`, `c68e097` | Diagnostic: nature and address of the faulting access in crash reports |
| `150ed20b65b0` | `95844de` | World: sector reported installed only at the end of its instantiation |
| `4ffe2216ff77` | `07d40a4` | World: zone change without waiting for all game reads |
| `e9912e36ca73` | `83f5934`, `2144bb8` | World: neighboring zone banks read ahead on another thread |
| `5a0d39272ea8` | `04ca400` | World: zone change without waiting for all game reads |
| `898eee3cc3c2` | `789bd2e` | WIP: Vulkan swapchain recovery after a failed recreation |
| `9ccd47cc01dd` | `80357aa` | Rendering: keep the D3D12 prototype and its standalone test benches |

### Reproducing a comparison as a binary stream

From a standard Python, use the two SHAs chosen in the JSON:

```python
import subprocess

def patch_id(repo, commit):
    patch = subprocess.run(
        ["git", "-C", str(repo), "show", "--pretty=format:commit %H",
         "--no-renames", "--binary", commit],
        capture_output=True, check=True).stdout
    return subprocess.run(
        ["git", "patch-id", "--stable"], input=patch,
        capture_output=True, check=True).stdout.decode("ascii").split()[0]
```

For adaptations, also examine `git range-diff old^..old new^..new`, then the functions in the final tree. The patch-id ignores whitespace and line numbers; it is not a semantic checker.

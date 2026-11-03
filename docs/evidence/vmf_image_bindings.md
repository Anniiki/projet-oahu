# VMF images: explicit binding to a bank catalogue

## Contract and provenance

`VmfImageBindings` associates the `VmfImageReference` entries of an already
validated movie with the exact names of the catalogue supplied by the caller. It
applies the two associations observed across the 39 movies of
[the VMF inventory](vmf_format_inventory.md#images-and-txix-trailer), with no
OpenTDU code and no new binary analysis. This component is written for the
independent client; it does not claim to reproduce a native loader whose
execution would have been observed.

```cpp
auto result = VmfImageBindings::resolve(movie, catalogue, limits);
// catalogue: span<const VmfImageAsset>{exact_name, member_index}
```

The PC adapter can supply `BankArchive.entries()[i].name` and the physical index
`i`. An already validated Xbox catalogue is also accepted. The module reads no
file, searches no other banks and decodes no texture. The adapter remains
responsible for the validity of its indices/tokens; the resolver does not assume
that they equal the catalogue order or the VMF IDs.

## Exact rules

| Image source | Name searched |
|---|---|
| `direct_fla` | `fla_%04u.2db`, with the exact `character_id` |
| `atlas_page` | `page_%03u.2db`, with the `source_number` taken from the TXIX |

The widths 4 and 3 are **minimums**, not truncations: `fla_10000.2db` and
`page_4294967295.2db` remain complete. No ID+2 offset, case change, folder
addition/removal or global search. A missing name produces `missing_asset`. Two
catalogue entries carrying a used name produce `ambiguous_asset`, even when they
share the same `member_index`. Unused duplicates play no part in this
resolution.

Resources are deduplicated **by name**, in the order of their first occurrence
in `movie.images()`. Several images of the same page therefore share one
`resource_index`. Two distinct names that alias the same token remain two
distinct resources. Bindings keep the order of the source images;
`binding_index(character_id)` uses a separate sorted ID index. A non-image or
absent ID receives no binding.

## Owned values and source metadata

The result owns names, resources, bindings, copies of `VmfRecord` and indices.
It keeps the `VmfProfile`. Each atlas image owns its `VmfAtlasEntry` and the
**32 original TXIX bytes**, in source byte order. The two opaque words, the four
floats and their bits, including negative zero, are preserved. No UV
orientation, normalisation, scaling or sampling rule is assigned to these
fields.

`resource_index` designates the `resources()` collection of the result. By
contrast, `image.record_index`, `image.atlas_entry_index`, the record's parents
and its `payload` remain **metadata of the source movie**, without rebasing and
without a borrowed view. The type20 payload is not copied in full; its offsets
do not allow bytes to be read after the movie is destroyed. The copied TXIX and
the owned names remain usable independently of that movie.

Getters are `const&`-qualified; the `const&&` overloads are deleted. Views
expire on destruction, move or assignment. The copy constructor performs an
independent copy; copy assignment builds a replacement before modifying the
destination. A failed allocation therefore keeps the whole previous validated
result. Moves are `noexcept`; a moved-from object must then only be destroyed
or reassigned.

## Quotas and errors before allocation

The preflight builds no dynamic container. It checks the source bytes, the
number of catalogue names, their individual/cumulative lengths, the images, the
**exact** number of unique resources, the owned names and the logical output
size. It also checks the image/type20/TXIX links and their bounds. The number
of resources is not artificially bounded above by the number of images, so that
a shared page is accepted under a quota of one resource.

Default values: input 64 MiB, 100,000 catalogue entries, cumulative names
16 MiB, individual name 4,096 bytes, 65,535 images/resources, owned names
16 MiB and logical output 64 MiB. The last one counts bindings, resources, name
bytes and the ID lookup indices; it excludes unused capacity, NUL,
container/allocator overhead and objects already owned by the caller.

Searches are bounded by `max_resolution_comparisons`, **50 million** by
default: equality tests of source keys and of catalogue names, half reserved
for each of the two passes. The preflight repeats the same resolution as
construction on an input that the caller keeps stable. A reached limit remains
`resource_limit`, never a resource declared missing. The bounded ID sort and the
validation are not part of this counter. The strategy can be quadratic on
references that are all distinct; it explicitly refuses an insufficient budget,
without guaranteeing constant real time.

Construction starts only after this preflight. An out-of-memory condition
returns `allocation_failure` with a static literal diagnostic; no error string
depends on a new allocation or on a view into the caller. A non-representable
container size yields `resource_limit`. No partial result is published. Copy
assignment keeps its C++ semantics: it can throw `bad_alloc`, with the strong
guarantee on the destination.

## Private validation

Batch: `<private-evidence>/reprise-2026-10-30/vmf-image-bindings-cpp/`.
`prepare_corpus.py`, `corpus-inputs.json`, `corpus-plan.txt`, `validate.py`,
`validation-final.json` and logs. Commercial data stay in their previous
private locations; only catalogues and metadata oracles are prepared for these
tests.

Private builds, **x64 and Win32**, MSVC 19.44 / SDK 10.0.26100.0,
`/std:c++latest /W4 /WX`: **2/2 CTests, 19,892 checks per architecture**.
The latest mode is a draft mode, not full C++26 conformance.

- 137 independent synthetic checks: PC/Xbox, non-physical ID order, shared
  pages, aliases, case/folders/ID+2 refused, minimal widths and full u32 page,
  raw big-endian TXIX and float bits preserved.
- Quotas refused without allocating the result, exact limits accepted, empty
  output under the matching zero quotas. Four resolution allocations and four
  copy allocations are each made to fail; static diagnostics and intact earlier
  destination verified.
- 19,755 corpus checks: **39 movies, 3,853 bindings, 2,114 resources**, zero
  used name missing or ambiguous. The 19 PC catalogues are compared against
  `BankArchive.entries()`; the 20 Xbox ones use the existing validated
  inventory. The associations are compared with `asset-name-links-v2.json`, not
  generated from the names produced by this module.
- The hashes of the 39 banks and of the 39 VMF copies remain identical to the
  initial inventory after both architectures. No game, XEX, VMF action, native
  call, GPU or Rea session is executed by this batch.

The allocation oracles observe the ordinary `operator new/new[]` of the owning
thread, not the allocator internals. Corpus coverage proves these name
associations; it validates neither the graphical content of the members, nor
the Xbox textures, nor shaders, display, hits or UI ergonomics.

## Integration and next steps

The integration adds `tdur_vmf_image_bindings`, a separate library linked
publicly to `tdur_assets`, and the `vmf_image_bindings` test. The x64 suite
passes 55/55; the new targets are also verified on Win32. The integration
then strengthens the interrupted-copy test: destination A holds three direct PC
images and source B two Xbox images with an atlas and a long name. Profiles,
cardinalities and hashes differ before the attempt; each refused allocation
must preserve A. The production code remains identical to the private batch
and the corpus expectations remain unchanged. Logs are in
`vmf-ui-parent-integration-v1/`.
The strengthened fixture passes **141 checks** per architecture; the
**19,755 corpus checks** also pass with the integrated binaries, that is
19,896 per architecture. The last complete x64 run remains 55/55.

A loader can later consume
`bindings()[i].resource_index`, open the explicitly chosen member and keep
CPU/GPU ownership separate. The interpretation of the TXIX rectangles and the
rendering of the images remain a distinct contract to establish before
displaying them.

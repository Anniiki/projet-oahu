# Native lifecycle of VMF display lists

## Provenance and scope

Contracts established statically on the Paradise **secondary PC** binary, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
They constitute neither a corroboration of the official code in memory, nor
proof of Xbox behaviour, nor a VMF host already hooked up to the game.

Private sources: `<private-evidence>/reprise-2026-10-31/rea/`:

- `vmf-host-context/session-v2/18`: 446B70, placement; 27: 4487F0,
  timeline initialisation; 32: 4418B0, initial counter; bounded dumps of the
  wrappers 44AF80/44AFC0/448790. The four earlier comparisons cover
  2,179 bytes; the old pseudocode 17–19 with fallback 84AC90 is excluded.
- `vmf-display-lifecycle/session/24..69`: 22 bodies, 3,340 bytes, plus the
  binary32 constant +1. `exact-byte-validation.json`, SHA
  `f2ab6fa58d121e00defd621d03c2ae312a00748b9d39f8441b864b915591df75`,
  compares Rea, the secondary PE and DUMPBIN. 448620 has two AddressSets
  of 13 and 65 bytes; the 3 intermediate bytes are not counted.
- Supplements 70..83: button vtable F3ED48, scheduler 448B20,
  instance constructor 448680, wrappers 44FC30/44FC70 and getter 423F20.
  `appendix-validation.json`, SHA
  `cfd55e6f3e8c939a2de3ed3fbb92675e2448f701d69cd0f3cc10d7e9f102826a`,
  brings the total to 24 bodies/3,801 bytes, 7 bytes of gaps excluded, plus
  144 bytes from three bounded reads of wrappers, 12 bytes of vtable and 4 of
  constant. These three wrappers are not functions recognised by Ghidra:
  their code is read/disassembled at the exact VAs, without fallback.
  The Rea session finished with 0 after 83 operations, export/close succeeded.

The private review `vmf-display-lifecycle-review/` keeps the fingerprints of
the exports, the transitions and an independent comparison of the Rea reads
with the dumps, without reopening any PE or executing commercial code.
The composition formulas of the transformations are treated separately.

Main assembly anchors: 446B70/v2-18
`ev_f840afc00137f11adf3fb830becc5447390a416feccd25e1880f4052295e952f`;
446FD0/24
`ev_a15908099e48cbc43814e30ffd1064b7f929b11daf904115d2131299fee40b08`;
447200/35
`ev_4d8f8d9ab4def222c0178d1194f26e39ebf39334ea517aaefa11ef40f75def28`;
4438D0/57
`ev_9d47099daac1bbab5e1b9c7c57ec1b136d3e3d91d9eedb4401ba72426097c9e9`;
447090/59
`ev_4ea41576d2d9984857c236134f1b467ed76c940546a06e346ec345000a0adc72`.
The other VAs are linked to the Evidence IDs and fingerprints in the two
private manifests, and to the numbered assembly listings of the same batch.

## Resource, node, instance: distinct types

| Element | Observed PC fields | Contract |
| --- | --- | --- |
| Definition | `context+60` is a table; the placement's WORD characterID selects a pointer | Asset identity, not instance identity |
| List | Embedded at `timeline+1C`: head+0, node pool+4, capacity+8, owner+C, cache head+10, cache count+14 | Capacity comes from the WORD `resource+E` |
| Node 0x9C | characterID+0, resource+4, ratio+8, properties+C, clip+38, child+3C, name+40, flags+60, composed properties+64, depth+90, next+94, previous+98 | Can exist without a live child |
| Child, common ABI | Vtable, pool/cache link+4, depth+8, borrowed properties+C, birth+10, parent+14, resource+18 | The virtual getters for the child type and the resource type are different |
| Timeline, virtual kind 0 | Vtable F3F7BC; frame state, list, dictionary+58, ScopeID+E0 | The ScopeID is a numbered ID32, distinct from addresses, depths and characterIDs |
| Button, virtual kind 1 | Vtable F3ED48; state+1C | The established wrappers assign no ScopeID and do not access the dictionary |

448680 is the constructor of the **timeline instance**, with vtable F3F7BC
and fields up to +C8; it is not a constructor of node 0x9C.
Among other things, it initialises the head and the counter of the cache.
448790 attaches parent and resource, then 4487F0 sets current_frame=1,
requested_frame=1, request_flag=0 and play_state=2 if FrameCount is 1,
otherwise 1. The birth equals `parent.current_frame`, or 1 without a parent.
Initialisation assigns the ScopeID32 from `context+B0`, then INC32; the root
receives 0.

44FC30 sets the same parent/birth/resource for the button, its properties
pointer to `node+78`, state 0 and depth −1. The caller then fills in the
depth. Its sequence up to RET 12 is 57 bytes long, within a read of 64.
44FC70 resets parent, resource, state and birth to 0, depth to −1; 423F20
returns 1. Do not apply the timeline constructor's writes to this type.

## Placement and node order

446B70 receives `(list, record)`. The depth is an unsigned WORD, compared as
signed with the DWORD capacity. If it is beyond capacity, return AL=1 with no
effect. Otherwise, linear search for that depth: an existing node is reused;
a missing node is taken from the pool 448560. If that pool is empty, return AL=0.

The new node receives its depth, then 448420 inserts it in ascending signed
order, before the first node of greater or equal depth. The preliminary
search avoids a duplicate on an initially valid list. 448420 does not
reinitialise the properties. 448560/4485B0 only remove/return the nodes
in the free list and adjust the counters; their other fields are not
cleared. Only the initial allocation 448480 zeroes the storage.

At the end of placement, AL is 1 if the node was absent, 0 if it existed.
**This boolean is not "success"**: the ignored depth also returns 1.
44AF80 increments `owner+CC` if AL!=0, always advances the record, then returns 1.
44AFC0 decrements that same field after a removal, even with no node found.
This field is therefore not an exact structural count on inconsistent input.

Before the flags are processed, 448620 extracts from the cache the first child
with `child.depth==depth`; it unlinks the object, decrements the count and sets
its link+4 to 0. If found, 446B70 replaces `node+3C` and reattaches `child+C`
to `node+78`. This lookup is a mutation, not a simple query.

## Resource choice and child replacement

With record flag 0x02, 446B70 writes the node's characterID and resource,
clears the first byte of the name, then handles the possible child.
The resource type getter is vtable+10; the child's is +8.

| Situation, signed comparison | Effect on the child | Effect on the node |
| --- | --- | --- |
|0x02 absent| No new child allocation by this block | Resource kept |
|0x02 present, child absent| New instance only for resource 39/timeline or 7/button | Resource assigned |
|0x02 present, birth<current_frame| Virtual destruction call, return to the pool, then new instance only for 39/7 | Same node |
|0x02 present, birth>=current_frame| Object kept, without calling the constructor again | Resource assigned, even if characterID differs |

No equality comparison of the characterID decides whether the child is kept.
Even a change of resource type does not force this path to recreate a child
whose birth allows it to be kept. The ScopeID of the kept timeline does not
change. A rule "different ID, therefore new object" would be a modification
of the observed behaviour.

The child pools are distinct: `context+A6C` for timeline, `context+A8C` for
button. 448600 removes their head; the virtual constructor +0 receives
`(parent, node, node_pool)`, then the caller writes the depth.
A null return from the child pool is not checked before native dereference,
unlike the node pool. An explicit refusal in the port is an added protection,
not an attested native success nor a guarantee of an atomic transaction.

## Flags and properties

| Placement flag | Observed writes |
| --- | --- |
|0x01 absent| `node.flags+60=0`; reset 442E20 of the 44-byte block at `node+C` and, if the child is a timeline, at `child+A0` |
|All 16 bits| OR into `node+60` after the possible reset |
|0x08| OR 0x01 into the property flags, copy of the 16 colour bytes to `node+10`, and `child+A4` for a timeline |
|0x04| OR 0x02, copy of the 24 matrix bytes to `node+20`, and `child+B4` for a timeline |
|0x40| WORD clip to `node+38` |
|0x10| WORD ratio extended to DWORD at `node+8` |
|0x20| Name copied over 32 bytes then last byte zeroed: 31 usable bytes |

The effective order is possible reset, OR of the flags, clip 0x40, colour
0x08, matrix 0x04, restoration of the local components, propagation 447E30,
ratio 0x10, then name 0x20. The table does not constitute a dispatch order.

The reset writes flags 0, four multiplier WORDs 0x100, four WORDs 0,
two floats +1 then four floats 0. It does not clean ratio, name, clip,
resource or child. Flag 0x01 does not require the node to exist beforehand;
an inconsistent placement can therefore expose leftovers from the native pool.

For a timeline kept by the **Character present** block, 446B70
saves its local properties and
`child+6C`. After the placement's writes, it restores the components
designated by that last DWORD: 0x20→WORD+AA; 0x08→float+C4; 0x10→float+C8;
0x40 or 0x100→floats+B4/+BC; 0x80 or 0x100→floats+B8/+C0.
These instance bits are not the record flags. 447E30 then propagates the
composed properties: timeline through 448940 then a 44-byte copy
`child+74→node+64`, other cases through 442E80 with the parent's properties.
No additional axis or rendering direction is inferred from these offsets.

The keep boolean is initialised to 0 before this block. A placement without
Character therefore does not save/restore these overrides, even when it finds
an existing or cached timeline again. The runtime tests explicitly distinguish
this path from Character+Move.

## Removal, rewind and total destruction

The three operations have different rules:

| Entry | Children destroyed | Children kept | Fate of the node |
| --- | --- | --- | --- |
|446FD0, removal at one depth| birth<**current_frame** | birth>=current_frame, pushed at the head of the cache | Unlinked then returned to the pool |
|447200, rebuild towards requested frame| birth>**requested_frame** | birth<=requested_frame, in the cache | All active ones returned to the pool |
|447090, total destruction| All at the end | None at the end | All returned to the pool |

446FD0 returns immediately if the active depth is absent, without touching a
cached child of the same depth. Otherwise: it extracts a cached candidate,
saves the active child, clears `node+3C`, handles the cached one then the
active one, and calls 4483B0. The two candidates are independent. 4483B0
repairs the head and neighbours before the side effect on the resource and
the return of the node to the pool.

447200 first purges the cached children born after the requested frame,
restarting from the head after each destruction. It then walks the active
nodes in depth order: keeps or destroys the child, clears the link, handles
the resource, returns the node to the pool. The active head is set to 0 at
the end, but the cache of kept children remains. Their parent and their old
properties pointer are not cleaned; reattachment resets that pointer before
reuse. The old recycled node has no guaranteed lifetime.

447090 handles the active nodes first. A birth<INT32_MAX is destroyed
immediately; a birth equal to INT32_MAX is deferred in the cache.
After clearing the active head, it pops and destroys **the whole** cache,
including these deferred objects. 446B10 and the equivalent loops call
the virtual destructor +4, then read the getter +8 again to choose the pool;
no `delete` of native storage.

448910 destroys a timeline in this order: conditional cleanup of the
variables if `instance+6C & 0x200`, then 447090 on its list. The cleanup
4438D0 selects:

- non-zero argument: entries whose ScopeID equals the argument;
- **argument 0: entries whose ScopeID is non-zero**, not the root entries 0.

It compacts the chains by typed copies, or unlinks a terminal entry,
decrements the count and resets the structural fields to 0. No pointer
payload 2/5 is freed by this body. This special rule for 0 must not
silently become a generic `erase(scope==0)`.

## Side effects and call order

For a resource 37, 44BDC0 is called before the child is kept:
resolution of the path at `resource+32`, lookup/initialisation of a variable,
then reference caches written at `resource+70/+74`. These are mutations
of the **shared runtime resource**, not fields belonging to the node.
The following batch `rea/vmf-runtime-scheduler/session/07..08` closes the helper
4436A0 (90 bytes compared). After lookup 4435D0, absence writes an empty
string and returns AL0. Type 4 copies the inline text at `entry+2C`; types 2/5
follow the pointer at that offset; the other types pass these bits to 442330
with precision 12. Presence returns AL1. These copies include the NUL and
show no destination bound in this body. Ownership of the external texts, the
runtime table and the services 37 remain to be integrated.

If bit 0x10 of `resource+2E` is absent, 44A3B0 also registers the node/owner
pair in a 32-slot registry at `context+AD8`, then handles the possible focus
activation. 44A440 handles the focus pointer first, then removes the first
matching node from the registry.
There is no deduplication of registrations. Focus changes call external
services/virtuals: they remain opaque effects, with no assumed UI or virtual
keyboard. The handling of 37 precedes the return of the node to the pool.
Placement does not explicitly unregister an old resource 37 before replacing
`node.resource`; do not add such an operation as if it were attested by this body.

448E60 and 4476C0 perform passes before/after actions on the timeline
children. 448B20 saves `context+58`, publishes the current timeline there,
handles the rewind if requested, then calls the record handlers until AL=0
depending on the mode/the request. The normal exits restore the context.
Some branches still consult 446A60; the future scheduler must preserve these
boundaries, not turn the handlers into no-ops.

## Limits and protections for the future port

These bodies assume valid pools, references and chains. They provide neither
visit budgets, nor alias/cycle detection, nor synchronisation, nor arbitrary
reentrancy. The clear extracts by depth without checking that the extracted
pointer is the selected one; inconsistent cache duplicates are not a native
case made safe. The globals and getters are read again during the calls: a
single global snapshot would add another contract.

A virtual destruction can still see `node+3C` in 447200/447090;
446FD0 clears it before the call. The code provides no rollback after a
callback, nor any guaranteed restoration of the context in case of a foreign
exception. The port must make explicit the mutations allowed during host
effects, the capacity errors and the handle generations, distinguishing them
from the native properties. Activation of handlers, texts, hit-testing,
buttons, rendering and game commands remains separate work.

The [C++ runtime for lists and instances](vmf_display_runtime.md) now carries
these explicit transitions. The services 37/variables and the complete
scheduling remain boundaries, without simulating success.

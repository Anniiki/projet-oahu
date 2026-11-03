# VMF display lists and instances in C++

## Scope

`tdur_vmf_display_runtime` builds real lists and live instances from a
`VmfProgram`. It carries the explicit operations of placement, removal,
rewind and destruction. The
[frame driver](vmf_frame_player.md) and an optional backend of
[owned variables](vmf_variable_lifecycle.md) are hooked up. Focus and
rendering remain to be integrated. An accepted operation does not prove a
displayed interface.

The native contracts are those of the [lifecycle](vmf_display_lifecycle.md),
established on the Paradise secondary `f452afcd…de6ed`. No new official
corroboration and no equivalence with the Xbox code is claimed.
Both resource profiles are consumed with these PC behaviours.

The executor can now apply five
[timeline commands](vmf_timeline_actions.md) to the live instances.
`apply_timeline_action` distinguishes the frame request, the current frame
and Play/Stop. It triggers neither rewind, nor traversal of children, nor
consumption of scripts. The frame driver remains to be hooked up.

The [target resolvers](vmf_target_resolution.md) walk the full names of the
active children and the parent links, without mutating the display. Labels
are looked up in the timeline's own resource with `_strnicmp(...,63)`
and the first match wins. `apply_goto_label` publishes requested/pending on a
match and Stop in every case. The [script context](vmf_script_context.md)
keeps origin, current target and recovery phase between resumptions.

## Program and identities

`VmfProgram::build` copies the validated film and decodes its own placements
and removals. It does not accept an arbitrary film/data pair. The shared
program is immutable, non-copyable and non-movable; its temporary getters
are refused. Its owner outlives the caller's data.

The context preallocates three bounded pools: nodes, timelines and buttons.
The root consumes a timeline slot. A cached object always consumes its slot;
no automatic eviction is added. The parent depth limits prevent unbounded
recursive expansion without forbidding dormant cycles in the resource graph.

Node and instance handles are distinct types with context, slot and
generation. A new life receives a new generation; a cached instance keeps its
identity. The native ScopeID is yet another identity: root 0, increasing
assignment to timelines, none to buttons.
The identity counters do not wrap around; an exhausted slot is retired.

The resource binding of a node is distinct from the own resource of its
child. The private field `bound` also distinguishes a null resource from the
initial pool from a valid binding to characterID 0. The runtime can therefore
keep a child of resource A under a node that now targets B, like the native code.

## Transitions and leftovers

- Placement: insertion by depth; extraction from the cache before a resource
  change; birth comparison only if Character is present.
  A birth at least equal to the current frame keeps the child.
- Removal: a birth earlier than the current frame destroys the child;
  otherwise it moves into the cache. A removal of an absent depth does not touch the cache.
- Rewind: purge of the cache according to the requested frame, then handling
  of the active ones. A birth later than this destination is destroyed; the
  other children are kept. This operation alone resets neither frames,
  cursor nor record counter to zero.
- Close: destruction of the active ones and of the whole cache, then
  invalidation of the root. Repeated closing has no effect.

The pools are not entirely reset to zero on each allocation. A recycled node
notably keeps ratio, clip and inactive fields until the planned writes. The
Character flag only clears the first byte of the name. An active name is
truncated to 31 bytes and short names are padded with zeros.

The timeline reinitialises its local properties, but its composition keeps
the flags of its old slot. The constructor then the placement successively
compose these flags. The root follows a different branch: exact copy from
local to composed. Reusing a fresh structure on each allocation would change
this behaviour.

Component overrides are restored only for a child kept by the
**Character present** block. A Move without Character does not take this
save. The node's own properties remain distinct from those of the child;
the computations use the [transformation module](vmf_transform.md).

## Driver, counters and observations

The setters `set_driver_frames` and `set_driver_properties` are explicit
integration points for the future driver. They do not claim to be the
Goto/SetProperty opcodes and launch no script. The current and requested
frames stay separate, 1-based; current=count+1 represents the End boundary
and has no source frame index. The request flag is independent and is not
normalised by the setter.

The properties setter follows propagation 448940: it recomputes the instance
and its descendants. The copy to the node that references this instance
belongs to 447E30, called during a placement/propagation of the parent; the
setter does not add it implicitly.

`place` and `remove` include the `+CC` modifications of the wrappers
44AF80/44AFC0. `record_balance` is a DWORD modulo 32, not a count of live
nodes. A removal of an absent depth can therefore take it from 0 to FFFFFFFF.
The placement's AL return is exposed separately from the operation status.

Inspection returns values, not pool views. The snapshot owns its program and
its copies of instances/nodes, including cached instances. It is ordered by
instance slot, then by node depth for each owner. Keeping it does not extend
the logical life of the objects.

## Boundaries and added protections

Resources 37 need variables and focus services and still return
`needs_services` before mutation. Flag 200 requests scope cleanup: without
a backend, it remains a boundary; with an owned backend, the ordered erasures
are verified before the topology mutation.
No missing callback is simulated.
The future mutable resource caches will have to belong to the context,
without modifying the shared immutable program.

Refusals for capacity, identity, depth or unbound state are explicit modern
guards. The requirements of a replacement take into account the slots freed
by destroying its old subtree. Refusals keep the earlier prefix and do not
consume a new ScopeID.

The closed subsystem maintains at most one child per owner/depth, active or
cached. An internal state violating this separation is refused before
extraction. This protection is not attributed to the original code.
Reentrant callbacks and arbitrary mutation of links are not yet admitted
operations. The module does not reproduce dangling pointers, output aliases
or partial writes during a native exception.

The guards for ignored depth remain documented in the code, but the validated
decoder already refuses out-of-capacity placements: the corresponding native
branch is not a public scenario reached by the tests.

## Validation

The lifetime expectations were frozen before the runtime implementation in
the private batch `reprise-2026-10-31/vmf-runtime-lifecycle-oracle/`. The
program factory had already been written, without being read by the author of
the expectations; these two chronologies are distinguished in the freeze. The
independent harness covers the transitions, leftovers, identities, overrides
and protections described above.
The execution results and the review are recorded in the private batches
`vmf-display-runtime-tests/` and `vmf-display-runtime-review/`.

The harness passes **2526 checks without failure on x64 and Win32**, on both
profiles. A preflight of 74 checks validates the synthetic files before
calling the runtime. The additional review passes 60 assertions per
architecture and finds no confirmed P1/P2 within this scope. The common
operations are tested without ordinary allocation; the factories and
snapshots are also subjected to controlled allocation failures.

Integration passes 63/63 Release x64 tests; the new runtime test also passes
under CTest Win32. The 60 executable targets are built explicitly, without
`ALL_BUILD`. These results remain component validations.

The initial versions of the harness remain preserved: literal conversions
made explicit for `/W4 /WX`, Xbox physical NUL added at the right place, then
Xbox flags limited to the observed low byte. Two raw flag expectations were
specialised per profile from the format evidence, before any execution of
the runtime; the PC values and the lifetime/transformation expectations
remain identical. The author of the harness read no production CPP.
The isolated fixture was linked only to the film and placement reader,
which distinguishes these envelope errors from a defect in instance lifetime.

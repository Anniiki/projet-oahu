# Startup initialization driver 9B1DF0

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The candidate PE table is `FA4320`, controller index 9. Its complete observed
graph contains 186 instructions and two returns. No runtime class identity or
subsystem role is inferred from the table.

## Reconstructed driver

`initialize_9b1df0` implements the prefix and reuses
[`run_9b1df0_tail`](startup_9b1df0_tail.md). The prefix assigns owner/flag, requests
the main allocation, updates the returned object's field, then calls its setup
ports. Any nonzero DWORD from `610840` triggers `610900` and the native false
return before subsequent allocations and the tail. Later allocation failures
skip their constructor but still call `6120F0` with the null value. Constructor
results, rather than original allocation tokens, are stored in the members.

The globals already represented by `StartupGlobals` are reused by reference.
Reads after callbacks remain live, including the allocation's size and later
global aliases. The final +98 argument is captured before the last `610EA0` call.
That call's object is forwarded through a typed receiver interface; a nonnull
object containing zero data stays distinct from `nullptr`. No numeric identity
duplicates its pointer identity.

Dereferenced objects have explicit modern types. Missing objects at native
dereference sites produce modern refusals after prior effects. The last object
is only forwarded and a null receiver is therefore permitted. Allocation views
do not free previous storage when assigned; their context owns the lifetime.

`Service9b1df0` exposes the initialization slot while remaining abstract for its
unreconstructed cycle callbacks. Its last outcome is absent until initialization
has actually been attempted. Registering a rejected service still counts that
member, following the controller's established cleanup contract.

## Evidence and validation

The local graph gives primary evidence identifiers for each instruction.
Its rows and complete entry reachability were independently checked against the
original Rea observations. Revision 1 supplies the reviewed C++ driver;
the initial numeric-receiver model was replaced before integration.

The x64 CTests `startup_9b1df0` and `startup_9b1df0_tail` check order, full-width
rejection, nullable allocations, constructor results, changed globals, typed
receivers and partial effects. They use owned witnesses and invoke no retail
code. All 16 direct callees and the indirect implementations still require
reconstruction; a complete caller driver is not a concrete game subsystem.
This does not establish new-game loading, world simulation or co-op operation.

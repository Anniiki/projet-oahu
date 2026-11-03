# Windows physics worker with cooperative shutdown

## Result and scope

`FixedPhysicsWorker` hosts the [90 Hz fixed owner](fixed_physics_timer.md)
on an ordinary Windows thread initialized by the CRT. It builds in
C++latest for x64 and x86 and is tested with QPC and actual wait objects.
It contains no game address, hook or Havok call. No driving trial or
actual physics decoupling of the official client is claimed.

This component provides lifetime infrastructure needed by the future
adapter. It does not replace [native lifecycle evidence](retail_physics_thread_adapter.md):
drain before `846CF0`, actual join at the `607010` boundary, publication
of historical scalars and priority handling after thread creation.
The integrator's transitive TLS/FPU compatibility remains a separate
condition. The worker installs no implicit Havok context policy.

## Usage order

1. `create(generation, integrator, flags)` creates wait objects and a
   **passive** thread. No simulation access occurs before `arm()`.
   The context is fully constructed before `_beginthreadex`; its lifetime
   does not depend on subsequently storing the returned handle.
2. `arm()` opens a single generation and establishes its QPC reference.
   Preparation time does not become simulation debt.
3. `quiesce(timeout)` closes admission and waits for an already accepted
   flag read or integration to finish. The thread remains passive after
   acknowledgment; simulation objects can then be destroyed.
4. `stop_and_join(timeout)` requests exit and waits on **the actual thread
   handle**, including CRT/TLS cleanup. Only successful completion of
   this wait is a join. Returning from the worker body is not a join.

A timeout retains the context and every handle; retry is required
before release. Calls from the worker that would wait on that same
thread are rejected without closing admission. No forced termination
is used. Destruction requests shutdown and waits without timeout;
therefore it must not occur under `DllMain`, from the worker or while
holding a lock needed by its callbacks. An integrator that never
returns prevents this cleanup: a short expired wait does not justify
freeing its objects.

The integrator and flag source must outlive the worker. Public operations
must not race its destruction. The generation token is a 64-bit integer
retained by value; the host ensures uniqueness across lifetimes.
A closed instance cannot be rearmed.

## Cadence and interruptions

The waitable timer is one-shot, without APC or an integer 11 ms period.
The next wake derives from the last QPC sample and calendar fraction;
callback time is not added to a new complete period. Conversions to
100 ns units round upward and are checked with integers. A frequency
above `UINT64_MAX / 10000000` or a deadline overflow is rejected.

The delay is only a wake mechanism: QPC and `FixedPhysicsTimer` decide
how many steps are due. Debt is retained outside pause. A notification
performs at most **one** step, then rereads shutdown and flags before
continuing. A blocked flag read must also return before drain; if
closure arrives during that read, no new integrator call is admitted
after it returns. Both zero-delta pause flags and the skip flag retain
the fixed owner's decisions.

Shutdown and closure take priority over timer wakes in waits.
A high-resolution timer is requested; if this flag is unavailable
with `ERROR_INVALID_PARAMETER`, the component requests a standard timer.
The report states the type actually created. None of these mechanisms
promises an exact wake, sustained cadence under CPU saturation or
a real-time guarantee.

Primary sources:
[_beginthreadex](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex?view=msvc-170),
[CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw),
[SetWaitableTimerEx](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimerex),
[WaitForMultipleObjects](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects).
The thread handle is retained until join; no waited-on handle is closed
during a wait.

## Errors and observations

A C++ failure in the flag source closes the worker before any additional
integration. An integrator failure retains the observation of its
uncertain call, stops admission and replays nothing. The timer's final
snapshot **before** shutdown is retained in the report, with debt/fraction
explicitly abandoned at drain. This snapshot does not promise that
the internal timer remains active after drain.

Creation, signaling, wait and timer-arming errors are reported with
their API boundary; `_beginthreadex` also retains `errno`. Allocations
may throw before publication. Exception handling is C++ `/EHsc`, without
native SEH recovery. The report is mutex-protected and no callback
executes under that mutex. Non-injected kernel API failures do not
have complete experimental coverage.

## Validation

MSVC 19.44.35229, SDK 10.0.26100, Release, `/std:c++latest /W4 /WX`:
**1 001 078 checks succeed on x64 and Win32**. Most concern the deadline
conversion matrix; this number does not count game scenarios. Lifecycle
trials use project-owned events and synthetic callbacks.

They cover passive preparation, a generation exceeding 32 bits,
rearm rejection, drain during blocked integration, closure during a
blocked flag read, pause/resume and zero delta, flag failure, uncertain
external effects without replay, rejection of self-wait and idempotent
shutdown. A deliberately blocked `thread_local` destructor demonstrates
that `body_finished` and drain are insufficient for join: it times out,
then succeeds only after unblocking that destructor.

After integration, all **30 Release/x64 CTests** pass. Run the targeted
test with:

```powershell
ctest --test-dir build -C Release -R fixed_physics_worker_lifecycle --output-on-failure
```

The official client, its saves and profiles were neither opened nor
modified by these validations.

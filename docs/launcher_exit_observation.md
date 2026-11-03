# Launcher exit observations

The Win32 diagnostic launcher reports the exact lifetime of its owned process
without using Windows Error Reporting timestamps as process-exit times. This is
diagnostic infrastructure, not a reconstructed game subsystem or a fix for the
offline new-game hang.

## Report fields

- `creation_filetime` identifies the process registered while suspended.
- `process_exit_filetime` is the UTC exit time returned by `GetProcessTimes` on
  the original process handle. Its decimal string preserves all 64 bits.
- `exit_code` comes from that same retained handle after termination.
- `job_notifications.events` contains supplementary completion-port messages.
  Each records the dequeue time, message kind and process ID. An exit code is
  queried only for the launcher's retained root process after it is signaled.

The completion port is associated before assigning the suspended process to the
empty private job. Collection never waits for notifications and stores at most
64 events; dropped events and read errors are explicit. It never opens a new
process handle based on a notification PID.

Most [Windows job notifications](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port)
are not guaranteed. A missing exit message does not establish that a process
remains alive. Dequeue timestamps describe observation time, not the exact time
of the underlying event. An abnormal-exit message classifies an exit status;
it does not independently prove an exception occurred.

## Validation

`observation_launcher_lifecycle` exercises five source-owned fixture modes:
normal exit, owned timeout, private INI argument, unrestricted play lifetime and
an explicit `ExitProcess(0xc0000005)` status. The last mode does not raise an
exception. No commercial game is launched by this test.

The Python harness keeps an independent handle from suspended registration
through termination and compares the creation time, exact exit time and exit
code with the final report. It also checks original-save immutability, injected
initialization and private play-session behavior. Notifications are checked when
delivered; they are never prerequisites for reporting a process exit.

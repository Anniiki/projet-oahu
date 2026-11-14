# Present timing diagnostics

This optional mod diagnostic measures CPU intervals between tracked Present
calls and the duration of those calls, using QueryPerformanceCounter. It does
not measure GPU completion, scanout, display latency or guaranteed frame pacing.
Both the primary device and primary swapchain entry points feed one recorder.

This feature does not launch the game by itself. It is enabled through
environment variables: `TDUR_FRAMETIME_DIAGNOSTICS=1` together with
`TDUR_D3D12_DISPLAY=1`. Missing or `0` disables it. Other values refuse startup.
The source installation must be identified. The observation log must be inside
the validated isolated documents directory and outside both the original
installation and the launched executable's directory. Reparse paths are refused.

Each device uses fixed arrays of 256 samples. A `frametime-summary` record is
written after 256 calls and at activation changes, Reset or final cleanup.
Periodic summaries keep the preceding entry timestamp; activation and Reset
start a new segment so a deliberate focus gap is not counted as a long frame.
Calls made while inactive are still measured. Abrupt termination can lose the
unfinished block. There is no per-frame file write or background worker.

Fields use microseconds: interval and Present duration p50/p95/p99/max, sample
counts, failed and occluded calls, invalid-clock count, intervals at least
50 ms and 100 ms, and the sequence of the longest interval in the block.
Percentiles use nearest rank, without interpolation. Failed and occluded
attempts remain in the timing samples when their clock readings are valid.

The process has a single quota of 128 summary records, shared by all devices
and retained across Reset or hook reinstall. Once exhausted, timing stops.
Each event is under 1,024 bytes; the existing logger limits a complete line to
2,784 bytes, so this diagnostic contributes less than 356,352 bytes per process.
Other observation events have their own limits. Fixed recording overhead and
occasional summary writes can influence the next measured interval.

Synthetic tests cover known percentile distributions, long intervals, segment
boundaries, invalid clocks, buffer saturation and concurrent quota consumption.
The injected startup fixture exercises both real COM entry points in its own
hidden window, with 17 unchanged save canaries. These tests do not establish
playability or performance in the commercial game.

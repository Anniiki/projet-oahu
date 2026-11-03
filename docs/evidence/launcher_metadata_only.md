# Launcher observations without window text

Both diagnostic and play sessions now observe process metadata only. The
launcher does not enumerate windows, read titles or controls, or send UI
messages. This applies before account entry as well as during gameplay.

Diagnostic sessions retain the existing 1 to 120 second deadline and stop only
their owned job on expiry. Play sessions retain their unlimited lifetime. Both
keep suspended-process registration, executable identity verification, source
save locks, independent exit observations and bounded Job notifications.

Reports keep `window_text_collected: false` and the legacy
`visible_windows: []` field for compatibility. No option enables text capture.

The native lifecycle fixture includes a visible source-owned window with a
private text marker during its timeout scenario. The verifier rejects any window
collection or appearance of this marker in a report, while independently
checking the timeout, exit code, exit timestamp and unchanged source canary.

The original launcher fails the new privacy assertion. The updated launcher
passes the lifecycle and late-load tests on owned C++ fixtures. These checks do
not demonstrate a successful new game in the commercial executable.

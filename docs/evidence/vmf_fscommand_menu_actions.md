# Tail services of the frok and frca commands

## Primary evidence

Rea/Ghidra 12.2 analyzed the official PC MC 1.66 A executable, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The following two bodies are readable on disk:

| Entry | Inclusive range | Size | SHA256 |
| --- | --- | --- | --- |
| `866040` | `866040..86607E` | 63 | `2f7e2fdf25fdb46eeeda264c18010dbec3a4064978c2bde26127fb366ddf43a3` |
| `866080` | `866080..8660BE` | 63 | `d8216466f1a6891e903d794156e0796c99577fb93dd1b86d884437dd8f0f9f6f` |

The private batch `reprise-2026-11-01/vmf-global-tail-services-v1/` keeps
13 verified results, the queries, exports, closure and direct comparison of
the bytes against the PE. The static manifest holds 39 reference files.
The binary is identical before and after; no game process is launched or
read during this phase. A [separate observation](official_menu_actions_runtime.md)
later corroborates the two bodies in memory and provides four readable prefixes.

## Direct effects and order

The [global handler](vmf_fscommand_global.md) reloads the owner
`10E75F0` into ESI right before the call. Each routine computes `ESI+4` to
call `635A80`. If its AL is zero, it calls `622B20` with its own distinct
constant (`F20EC8` or `F20F10`), then returns without reading `1B8` or `F98`.

Otherwise, it reads field `1B8` of the captured owner. Zero terminates without
reading `F98`. A non-zero `1B8` leads to reading `F98`; zero terminates with no
call and no store. When both words are non-zero, it pushes the captured `F98`
word and calls `634DA0` for `866040`, or `634D80` for `866080`, with `1B8` in
ECX. After a normal return, it writes zero into `F98` of the initial owner.

The reads therefore follow the first helper, but the owner remains the one
captured in ESI. A mutation of the global or of `F98` during the last helper
does not prevent this final write on the initial object. No success meaning
is attributed to the final AL: the global handler ignores it and produces
AL1 itself.

## Boundaries to resolve

`F98` is only observed as a 32-bit word passed on the stack. Its role as a
pointer, identifier or token remains unknown; the x64 port must not turn it
arbitrarily into a pointer. The type and affinity of `1B8` also remain to be
established together with the called services.

The static exports of `635A80` and `634DA0` are unusable: the first reduces
to a CALL outside the image, the second to instructions with no complete
return path. Their Ghidra bounds do not prove a usable semantic body. None of
these paths was followed or executed. The bodies `634D80` and `622B20` were
not inspected in this bounded batch.

The implicit inputs and effects of these auxiliaries remain mandatory
dependencies. An availability flag, an invented boolean or a byte comparison
does not replace them. Six caller control cases are documented as expected,
not executed. This work establishes neither the wiring of these services in
game nor the completion of the client, of authentication or of co-op.

## Later corroboration

A [later review of the memory windows](vmf_menu_action_helpers.md)
establishes the `635A80` test of a DWORD for equality with 8 and the two
wrappers `634DA0/634D80` with RET 4 and normalization of the 32-bit result. The
earlier static finding remains historical; the operations `63DC10/63DBC0`
and the continuation of `622B20` remain boundaries. No fictitious wiring is
substituted for these services.

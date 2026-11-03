# Official corroboration of menu finalizers

## Result and identity

The exclusive ui-vmf-menu-finalizers profile reads **ten code ranges, 1 188 bytes**,
covering eight complete AddressSets. All are readable and equal the official
PE on disk as well as the studied secondary. No scalar, pointer, user profile
or global language/units state is read by this probe.
This demonstrates the presence of the code without proving its execution.

Official PC MC 1.66 A: SHA-256
3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30.
Secondary:
f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed.

| Routine | VA ranges / bytes |
| --- | --- |
| Units application | 898890/205 |
| Language cache | 898B40/29 +898B60/20 |
| Mode selection | 898960/76 |
| Language classification | 9A96D0/277 |
| Language finalization | 9AB830/106 |
| Game update | 6E8A10/277 |
| Active movie lookup | 99A780/73 |
| First callback removal | 99CC40/58 +99CC80/67 |

The three alignment bytes 898B5D..5F and six alignment bytes 99CC7A..7F are excluded.
Compile-time assertions check the sums, bounds, groups and gaps.
The metadata of each capture indicates its procedure group.

## Isolated trial and evidence

Slot coop-anything0; batch
<isolated-copies>/slot0/20261031T175512-11120.
PID 9984, FILETIME 134379429479652844, base 00400000.
The Job limited to one process stops at its deadline after 100.228 seconds.
Two windows are enumerated; no visual inspection or UI action.
The temporary bridge is borderless D3D9On12/DXGI, distinct from the future native engine.
The prepared copy and the 6 697 reference files are verified;
the 17 save files are isolated and then verified unchanged.

Rea closes its new session of 19 requests normally: four bodies,
581 bytes. The remainder comes from the closed batch vmf-menu-data-bindings-v1:
four bodies, 607 bytes. The newly selected read 99CC40/131 contains
125 bytes of body and six of alignment; the verifier separates them.
Two batches added to the inbox after closure were not executed:
they are not counted as evidence. No old handle is relaunched.

The offline verifier rereads each capture, the AddressSets, the Rea bytes,
the two PEs and the ten DUMPBIN ranges. It checks the section and file
bounds, the exact catalog, the successful read of each range,
PID/FILETIME against the launcher, the snapshots and the final audits.
The reader enforces its page policies and the module base allocation;
the exact protections are not published and MEM_IMAGE is not tested
separately. The reads do not form an atomic snapshot.

Private evidence: <private-evidence>/reprise-2026-10-31/vmf-menu-finalizers-official-v1.
comparison.json carries SHA-256
167b16800e6b2855f688a8716e059c1daa9b027aee967b3b26d33612fb547ae1
and 55 artifact fingerprints. The closed Rea snapshot carries
a70fa8c9aabb05817f4bcfa71fa88b8414d60430d9838ea351159eb54c9ae806.
The trial sources, the builds, the two repository guideline files, the
executables and the banks retain their fingerprints. The commercial bytes stay outside Git.

The [C++ reconstruction](vmf_menu_finalizers.md) has its own separate tests.
Neither this trial nor the asset diagnostic validates the display of the new
menus, the full sbdd receiver, authentication or a co-op session.

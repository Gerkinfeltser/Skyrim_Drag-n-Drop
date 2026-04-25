# Drag & Drop — Codebase Audit (v0.1.92-alpha)

## Dead Code

### `ForceRagdoll()` (DragHandler.cpp:450-492)
Defined but never called. AGENTS.md documents that `ForceRagdoll()` breaks G-key grab — all attempts were rolled back. The function should be removed.

### `ZeroGrabbedVelocity()` (DragHandler.cpp:365-393)
Defined but never called. The throw path now zeros velocity inline inside the `AddTask` lambda (ThrowGrabbedObject line 431-435). Should be removed.

### `savedCollisionInfo` save/restore (DragHandler.h:129, DragHandler.cpp:321-334)
`RestoreCollisionFilters()` reads and clears `savedCollisionInfo`, but nothing ever populates it. The collision filter modification that wrote to it was removed (per AGENTS.md: "caused more problems than it solved"). The vector, the restore function, and the header member are all dead.

### `Debug()` Papyrus function (main.cpp:65-68, 76)
Registered as a Papyrus function but only logs a message. No functional use. Should be removed.

## Broken Papyrus

### `ReleaseGrabbedActor()` declared but unimplemented (DragDrop.psc:3)
Declared as `native global` but never registered in main.cpp's `RegisterPapyrusFunctions`. Will cause a Papyrus error if called at runtime. Either implement or remove the declaration.

### `RequestGrab()` uses broken delivery path (DragDrop.psc:9-18)
Line 17: `grabSpell.Cast(player, target)` uses delivery=Target. AGENTS.md explicitly documents this as broken: "Two competing grab operations → physics wigging out". This function should be removed or rewritten to use the C++ `TryGrabWithSpell` pattern (set `grabbedObject` + `CastSpellImmediate(target=player, delivery=Self)`).

## Code Smell / Maintainability

### Spring access pattern copy-pasted 6 times
The same ~10 lines of `reinterpret_cast` + offset `0x30` to get the spring's `hkpRigidBody` appear at:
- DragHandler.cpp:528-538 (hit handler velocity capture)
- DragHandler.cpp:606-619 (UpdateGrabState spring settings)
- DragHandler.cpp:715-727 (velocity clamping — find spring body)
- DragHandler.cpp:756-771 (swing impact — read spring speed)
- DragHandler.cpp:1101-1113 (DoRelease — capture spring velocity)
- DragHandler.cpp:1204-1217 (ReleaseNPC — capture spring velocity)

Should be extracted to a helper like `GetSpringRigidBody(player)` or `ForEachSpring(player, callback)`.

### `UpdateGrabState()` is 440 lines (DragHandler.cpp:587-1027)
Handles: grab detection, spring settings, velocity zeroing, sprint blocking, stamina drain, tether distance, velocity clamping, swing impact, AND throw impact tracking. Should be split into focused functions (e.g., `HandleDragFrame()`, `HandleSwingImpact()`, `HandleImpactTracking()`).

### `DoRelease()` and `ReleaseNPC()` duplicate ~80% of logic
Both capture spring velocity, dispel effects, restore speed, and clean up state. Should share a common release path.

### Magic numbers
- `0x30` — hkpEntity offset from action base (used 6 times)
- `0x48` — spring force offset (line 387)
- `0x60`, `0x64`, `0x68` — spring damping/elasticity/maxForce (lines 613-615)
- `0x1A` — sound handle flags (line 120)

Should be named constants in the anonymous namespace or a config struct.

### `std::rand()` without seed (DragHandler.cpp:518)
Used for drop chance rolls. `std::rand()` is never seeded, so the sequence is deterministic per run. Should use `<random>` with `std::mt19937` seeded from `std::random_device`, or at minimum call `std::srand()` once at startup.

## Log Spam

### `CollectAllRigidBodies()` logs every call (DragHandler.cpp:103)
Called multiple times per frame during drag and impact tracking. The `"Collected N rigid bodies"` line generates hundreds of entries per second, drowning out useful log info. Should be removed or gated behind a verbose debug flag.

## Undocumented / Hidden Settings

### `bEnableGKeyGrab` not in INI or docs (DragHandler.cpp:152)
Loaded from INI with default `true`, but no entry exists in DragAndDrop.ini and it's not mentioned in INI-SETTINGS.md or README.md. Users have no way to discover or change this without reading source code.

### `fDropOnHitChance` / `fDropOnProjectileChance` not logged at startup
The settings summary log (lines 191-200) doesn't include these new values. They're loaded but invisible in the log.

## Header Defaults vs INI Defaults

These aren't runtime bugs (INI overrides) but are confusing for code readers:

| Setting | Header default | INI default |
|---------|---------------|-------------|
| `throwImpulseMax` | `10.0f` | `20.0` |
| `throwDropWindow` | `0.5f` | `0.2` |
| `throwTimeToMax` | `4.0f` | `3.0` |
| `springMaxForce` | `500.0f` | `1000.0` |
| `dragSpeedMult` | `3.0f` | `0.5` (user-set) |

Header defaults should match INI defaults, or at minimum match the `GetINI*` fallback value.

## Misleading Names

### `impactCloakSpell` (DragHandler.h:95)
It's the PushActorAway impact spell (`DragDropImpactHitEffect`, 0x808), not a cloak spell. Name inherited from early development. Should be `impactPushSpell` or similar.

### Inconsistent bool naming
- `bEnableGKeyGrab` — b-prefix
- `bEnableMod` — b-prefix (in INI)
- `noSprint`, `noSpeedPenalty`, `blockTwoHanded` — no prefix (member vars)
- `enabled` — no prefix

The b-prefix convention is only used for some bools. Pick one style and stick to it.

## What's Clean

- **Hooks.cpp** — clean, focused, single responsibility
- **main.cpp** — clean plugin entry, proper message handling lifecycle
- **INI loading** — solid, hex FormID support via `strtol` base-0
- **Sound system** — properly null-checked, correct `BuildSoundDataFromDescriptor` → `SetObjectToFollow` → `SetVolume` → `Play` pattern
- **Hit event projectile detection** — good fallback via weapon type when `TESHitEvent::projectile` is 0
- **`ApplyClampedImpulse`** — clean, reusable, properly clamped
- **Drop chance system** — clean percentage roll with separate melee/projectile chances
- **Stamina drain** — properly frame-rate independent via `chrono` delta time
- **`PlaySoundForm`** — single helper, all sound calls go through it

# AGENTS.md - Drag & Drop

Skyrim SE SKSE plugin for grabbing, dragging, and throwing NPCs using Havok physics springs. Self-contained — no external mod dependencies.

## Project Structure

| Directory | Contents |
|-----------|----------|
| `SKSE/src/` | C++ SKSE plugin source (DragHandler.cpp/h, Hooks.cpp/h, main.cpp) |
| `SKSE/Plugins/` | Built DLL + INI config |
| `Source/Scripts/` | Papyrus source (.psc) |
| `scripts/` | Compiled Papyrus (.pex) |
| `DragAndDrop_spriggit/` | ESP source (YAML), rebuild with Spriggit |

## Build Commands

```powershell
# Build SKSE plugin
cmake --build D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build --config Release

# Deploy (symlinked into MO2)
Copy-Item D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build\Release\DragAndDrop.dll D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\Plugins\DragAndDrop.dll -Force

# Rebuild ESP from YAML (requires Spriggit)
powershell -Command "& 'D:\gerkgit\GerkinStuff\bin\spriggit.bat' deserialize -i 'D:\gerkgit\Skyrim_Drag-n-Drop\DragAndDrop_spriggit' -o 'D:\gerkgit\Skyrim_Drag-n-Drop\DragAndDrop.esp'"

# Compile Papyrus
psx compile_psc.bat "Source\Scripts\DragDrop.psc"
```

## How It Works

**LesserPower spell initiates grab, C++ handles everything else.**

1. **LesserPower** casts GrabActor effect — ESP conditions use tautology (`GetDead==1 OR GetDead==0`) so all actors pass
2. **C++ IsValidTarget()** does the real filtering based on INI config
3. **GrabActor** archetype creates Havok mouse spring (engine handles physics attachment)
4. **Action key (G by default)**: Release/drop NPC. Also initiates grab when `bEnableGKeyGrab=true`
5. **Action key hold**: Charge throw (shows "Ready to throw!" at threshold)
6. **Action key release**: If held < dropWindow → drop. If held ≥ dropWindow → throw with ramping force
7. **Throw**: On next frame after spring release, zeros all ragdoll bodies then applies impulse to every body
8. **Swing impact**: During drag, proximity detection pushes nearby actors and static objects
9. **Throw/drop impact tracking**: After release, tracks thrown NPC and applies knockback to actors it passes near

## INI Config (`SKSE/Plugins/DragAndDrop.ini`)

Loaded via Win32 `GetPrivateProfileString` using DLL module handle path resolution. Works under MO2.

```ini
[General]
bEnableMod = true              ; Master enable toggle
fGrabRange = 150.0             ; Max distance to initiate grab
bGrabAnyone = false            ; Allow grabbing any NPC (overrides followers/children)
bGrabFollowers = true          ; Allow grabbing player teammates
bGrabChildren = false          ; Allow grabbing child actors
bGrabHostile = false           ; Require hostile to grab (respects follower/child overrides)
fStaminaDrainRate = 5.0        ; Stamina drain per second while dragging (stub, not wired)
fDragSpeedMult = 1.0           ; Speed multiplier while dragging
bNoSpeedPenalty = true         ; Prevent engine drag speed penalty
iActionKey = 34                ; Action key scancode (34=G, 19=R)
bUseShoutKeyForRelease = false ; Use Shout key for release instead of action key
bDropOnPlayerHit = true        ; Auto-drop NPC if player takes damage
fSpringDamping = 1.5           ; Havok mouse spring damping
fSpringElasticity = 0.05       ; Havok mouse spring elasticity
fSpringMaxForce = 1000.0       ; Havok mouse spring max force
fDragMaxVelocity = 5.0         ; Max ragdoll body velocity during drag (prevents moon-gravity flings)
fGrabTetherDist = 600.0        ; Auto-drop if NPC exceeds this distance from player

[Throw]
fThrowImpulseMax = 20.0        ; Max throw force
fThrowDropWindow = 0.2         ; Seconds before charge starts (tap = drop)
fThrowTimeToMax = 3.0          ; Seconds from charge start to reach max force

[Impact]
fImpactRadius = 120            ; Proximity detection radius for impact
fImpactDuration = 3.0          ; Max impact tracking time after release
fImpactMinVelocity = 0.5       ; Stop tracking below this speed
fImpactForce = 300.0           ; Havok impulse magnitude (ragdoll targets)
fImpactPushForceMax = 5.0      ; PushActorAway force (standing targets)
fImpactDamage = 0.0            ; Damage dealt on impact
fImpactDamageThrownMult = 1.0  ; Damage multiplier for thrown NPC (self-damage)
bImpactOnDrop = false          ; Enable impact tracking on drops (not just throws)
fSwingImpactRadiusMult = 0.6   ; Swing impact radius = fImpactRadius * this
fSwingImpactCooldown = 0.5     ; Seconds between swing hits on same target
bSwingImpactStatics = true     ; Push dynamic statics (baskets, clutter) during swing
fRagdollMaxVelocity = 5.0      ; Max velocity for impact impulse clamping
fImpactForceSpeedScale = 1.0   ; Impact force scales by speed * this
fImpactDamageSpeedScale = 1.0  ; Impact damage scales by speed * this
```

## Key Scancodes

```
G = 0x22  (default action key — release/drop, also initiates grab when bEnableGKeyGrab=true)
R = 0x13  (alternative, configurable via iActionKey)
```

## Spell Delivery Findings

**Delivery = Self (correct):**
- Spell fires on player only
- C++ `TryGrabWithSpell` sets `player->grabbedObject` + fires `CastSpellImmediate`
- Engine grab system uses the C++-set `grabbedObject` — no conflict, smooth drag
- `DragDropGrabScript` with `OnEffectStart` (PushActorAway trick) can target NPC via `akCaster`

**Delivery = Target (broken):**
- Spell fires on NPC, engine tries to grab from player simultaneously
- Two competing grab operations → physics wigging out

**CastSpellImmediate target arg matters:**
- `CastSpellImmediate(spell, false, target, ...)` — the `target` arg overrides delivery
- Passing `target=player` with delivery=Self causes the effect to fire on player, engine uses `grabbedObject` → **smooth drag, no jitter**
- This is NOT a dual-grab conflict — the engine uses `grabbedObject` (set by C++) for the actual grab target

**Why fresh KO'd NPCs work but reloaded KO'd NPCs don't:**
- Fresh KO: ragdoll state is "live", physics run normally, GrabActor works smooth
- Reloaded KO: ragdoll state is "baked" into save, doesn't resume properly → stiff
- `ForceRagdoll()` breaks G-key grab — calling it before `CastSpellImmediate` causes the grab to fail silently

**G-key grab path vs power menu path:**
- G-key (with bEnableGKeyGrab=true): fires `TryGrabWithSpell` → `grabbedObject` set → `CastSpellImmediate(target=player, delivery=Self)` — smooth drag
- Power menu: fires `Actor::CastSpell` directly → bypasses `TryGrabWithSpell` — smooth drag

## Papyrus API

```papyrus
DragDrop.ReleaseNPC()              ; Drop NPC (no impulse)
DragDrop.ThrowNPC(float force)     ; Throw NPC with force
DragDrop.GetGrabbedNPC()           ; Returns current grabbed Actor
DragDrop.IsDragging()              ; Returns bool
```

## Havok Physics Details

- Spring access: `player->GetPlayerRuntimeData().grabSpring` (BSTSmallArray of hkRefPtr<bhkMouseSpringAction>)
- Entity at raw offset +0x30 from `hkpMouseSpringAction` (hkpEntity* m_entity)
- Spring settings at offsets: damping +0x60, elasticity +0x64, maxForce +0x68
- Ragdoll body collection: `BSVisit::TraverseScenegraphCollision` walks all bhkNiCollisionObject → bhkCollisionObject → GetRigidBody → referencedObject (hkpRigidBody)
- Mass: `hkpRigidBody->motion.GetMass()` (typical NPC ~7.0)
- Impulse applied to ALL ragdoll bodies (not just spring body) — prevents constraint damping
- Delayed task via `SKSE::GetTaskInterface()->AddTask()` runs next frame after engine processes spring release
- Spring body velocity reflects camera swing speed — used for drop momentum inheritance and swing impact detection

## Features

### Impact Knockback
- After throw/drop, `State::TrackingImpact` monitors thrown NPC via ragdoll body velocities
- 2D distance (X/Y only) for proximity checks — ragdoll center is at torso height, `GetPosition()` returns feet
- Standing actors: `PushActorAway` via Papyrus spell (0x808 → DragDropImpactScript)
- Dead/ragdolled actors: Havok impulse to all rigid bodies
- Hit-once tracking via `unordered_set<FormID>` prevents double-hits
- Speed-scaled force and damage: scales by `1.0 + (speed * scale)`
- Impact damage applies to both the hit actor and the thrown NPC (with multiplier)

### Swing Impact (During Drag)
- During drag state, proximity detection pushes nearby actors and static objects
- Uses `TESObjectCELL::ForEachReferenceInRange` for spatial queries
- Cooldown per target prevents spam
- 0.5s grace period after grab starts before swing impact activates
- Static objects: only applies impulse to bodies with `motion.type == kDynamic`

### Drop Momentum
- On drop (tap release), captures spring body velocity before destroying springs
- Applies captured velocity to all ragdoll bodies next frame — NPC inherits camera swing momentum

### Velocity Clamping (During Drag)
- Every frame during drag, all ragdoll body velocities clamped to `fDragMaxVelocity`
- Prevents moon-gravity flings from fast camera swings
- Separate from impact impulse clamping (`fRagdollMaxVelocity`)

### Tether Distance
- If NPC ragdoll center exceeds `fGrabTetherDist` from player, auto-drops
- Safety net for NPCs getting knocked away during combat

### Drop on Player Hit
- `DragHandler` inherits from `BSTEventSink<TESHitEvent>`
- When player is hit while dragging, drop is delayed one frame via `AddTask` to avoid slow-mo ragdoll
- Togglable via `bDropOnPlayerHit`

### Engine Grab Gate
- `UpdateGrabState()` validates engine-initiated grabs via `IsValidTarget()`
- Invalid grabs: destroy springs, zero velocity, dispel — prevents fly-away pop
- Power menu bypasses IsValidTarget (dev mode)

### Speed Boost
- While dragging, player SpeedMult is multiplied by `fDragSpeedMult`
- Restored on release

## Log File

```
C:\Users\vector\Documents\My Games\Skyrim.INI\SKSE\DragAndDrop.log
```

## Dependencies

- SKSE64
- Address Library for SKSE Plugins
- CommonLibSSE-NG (build dependency via vcpkg, `x64-windows-static` triplet)

## Build Gotchas

- Warning `overriding '/DENABLE_SKYRIM_VR=1' with '/UENABLE_SKYRIM_VR'` is expected and harmless
- LSP errors about `RE/Skyrim.h` not found are false — headers resolve at build time via CMake
- Papyrus compiler needs `-f='D:\Modlists\ADT\Game Root\Data\Source\Scripts\TESV_Papyrus_Flags.flg'`
- Spriggit is at `D:\gerkgit\GerkinStuff\bin\spriggit.bat`
- Version bump: update VERSION in main.cpp
- `EndGrabObject()` is gated behind `#ifndef ENABLE_SKYRIM_VR` — use `DestroyMouseSprings()` instead
- Windows `min` macro conflicts with `std::min` — use `(std::min)(...)` parenthesized form

## Key References

- GrabAndThrow by powerof3: Havok spring access, throw impulse pattern, player->grabSpring
- CatchAndRelease: GrabActor spell config (FireAndForget + Duration 999999)

## Known Issues

- **Stiff KO'd NPCs (reloaded from save) — UNSOLVED.** 10 attempts in v0.1.54–v0.1.65, all rolled back. See `docs/STIFF-NPC-DEBUGLOG.md`. Fresh KO'd NPCs drag fine. Reloaded KO'd NPCs are stiff/frozen. Dispelling paralysis effects works to unstick but NPC wakes up.
- **Power menu cast bypasses IsValidTarget.** Power menu uses `Actor::CastSpell` directly. Power menu = dev mode (always casts, no gate). G-key respects all settings.
- **ForceRagdoll breaks G-key grab.** Calling `ForceRagdoll(target)` before `CastSpellImmediate` causes grab to fail silently.
- **PushActorAway force is hardcoded** in DragDropImpactScript.psc at 5.0 — not configurable via INI yet.
- **Flinging non-para/dead NPCs on grab via spell** (low priority) — minor issue with living NPC grab impulse.

## Not Yet Implemented

- Stamina drain while dragging (stub exists, not wired to frame tick)
- Shield behavior (held NPC blocks arrows)
- Knockdown from thrown NPCs
- Velocity-scaled PushActorAway force
- SETTINGS-REFACTOR-PLAN (ESP globals + Papyrus MCM) — see `docs/plans/SETTINGS-REFACTOR-PLAN.md`
- Knockout mod reload fix (stashed in Skyrim_KnockoutPatched)

## Save Game Safety

Mod is save-game safe. DLL and scripts don't bake anything into saves. ESP only adds a LesserPower spell and magic effects — no quests, aliases, or permanent cell edits.

On removal:
- Grab spell stays in player's spell list (harmless, won't do anything)
- If removed mid-drag, SpeedMult boost won't restore (fix: `player.setav speedmult 100`)
- Otherwise clean — load the save without the mod and it works fine

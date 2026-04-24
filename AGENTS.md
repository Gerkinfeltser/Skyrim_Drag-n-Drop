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
4. **G key**: Release/drop NPC (initiate grab via power menu or spell — G-key grab enabled for testing)
5. **R key hold**: Charge throw (shows "Ready to throw!" at threshold)
6. **R key release**: If held < dropWindow → drop. If held ≥ dropWindow → throw with ramping force
7. **Throw**: On next frame after spring release, zeros all ragdoll bodies then applies impulse to every body

## INI Config (`SKSE/Plugins/DragAndDrop.ini`)

> **Deprecated** — INI parsing was removed in v0.1.42-alpha. Settings are now hardcoded defaults in `LoadSettings()`. Future settings will be driven by ESP globals + Papyrus MCM.

```ini
[General]
bEnableMod = true
fGrabRange = 150.0
fStaminaDrainRate = 5.0
bGrabFollowers = true
bGrabChildren = false
bGrabAnyone = false

[Throw]
fThrowImpulseMax = 10.0     ; max throw force
fThrowDropWindow = 0.5       ; seconds before charge starts (tap = drop)
fThrowTimeToMax = 4.0        ; seconds from charge start to reach max force
```

## Key Scancodes

```
G = 0x22  (release/drop — also initiates grab when bEnableGKeyGrab=true)
R = 0x13  (hold to charge throw)
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
- Script fires on NPC but the dual-grab conflict ruins it

**CastSpellImmediate target arg matters:**
- `CastSpellImmediate(spell, false, target, ...)` — the `target` arg overrides delivery
- Passing `target=NPC` with delivery=Self causes the effect to fire on the NPC (Papyrus logs show "Start effect on NPC cast by Player") → **causes jitter/dual-grab**
- Passing `target=player` with delivery=Self causes the effect to fire on player, engine uses `grabbedObject` → **smooth drag, no jitter** ✅
- This is NOT a dual-grab conflict — the engine uses `grabbedObject` (set by C++) for the actual grab target

**Why fresh KO'd NPCs work but reloaded KO'd NPCs don't:**
- Fresh KO: ragdoll state is "live", physics run normally, GrabActor works smooth
- Reloaded KO: ragdoll state is "baked" into save, doesn't resume properly → stiff
- `ForceRagdoll()` breaks G-key grab — calling it before `CastSpellImmediate` causes the grab to fail silently (IsGrabbing=true in logs but NPC doesn't move)

**G-key grab path vs power menu path:**
- G-key (with bEnableGKeyGrab=true): fires `TryGrabWithSpell` → `grabbedObject` set → `CastSpellImmediate(target=player, delivery=Self)` — smooth drag ✅
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
- Ragdoll body collection: `BSVisit::TraverseScenegraphCollision` walks all bhkNiCollisionObject → bhkCollisionObject → GetRigidBody → referencedObject (hkpRigidBody)
- Mass: `hkpRigidBody->motion.GetMass()` (typical NPC ~7.0)
- Impulse applied to ALL ragdoll bodies (not just spring body) — prevents constraint damping
- Delayed task via `SKSE::GetTaskInterface()->AddTask()` runs next frame after engine processes spring release

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

## Key References

- GrabAndThrow by powerof3: Havok spring access, throw impulse pattern, player->grabSpring
- CatchAndRelease: GrabActor spell config (FireAndForget + Duration 999999)

## Not Yet Implemented

- **G-key grab jitter — SOLVED (v0.1.53).** Fix: `CastSpellImmediate(grabSpell, false, player, ...)` — pass `target=player` with delivery=Self. See `docs/G-KEY-JITTER-BUG-DEBUGLOG.md`.
- **Stiff KO'd NPCs (reloaded from save) — UNSOLVED.** 10 attempts in v0.1.54–v0.1.65, all rolled back. See `docs/STIFF-NPC-DEBUGLOG.md`. Fresh KO'd NPCs drag fine. Reloaded KO'd NPCs are stiff/frozen. Dispelling paralysis effects works to unstick but NPC wakes up.
- **Power menu cast bypasses IsValidTarget.** Power menu uses `Actor::CastSpell` directly. **Power menu = dev mode** (always casts, no gate). G-key respects all settings.
- **ForceRagdoll breaks G-key grab.** Calling `ForceRagdoll(target)` before `CastSpellImmediate` causes grab to fail silently. Must NOT call in the grab path. Post-grab timing may work (untried).
- Stamina drain while dragging (stub exists, not wired to frame tick)
- Throw damage/stagger to other NPCs on hit
- SETTINGS-REFACTOR-PLAN (ESP globals + Papyrus MCM)
- Knockout mod reload fix (stashed in Skyrim_KnockoutPatched)

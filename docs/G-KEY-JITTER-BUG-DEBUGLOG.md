# Drag & Drop — G-Key Jitter Bug: Debug Log

**Date:** Thu Apr 23 2026  
**Problem:** G-key grab via CastSpellImmediate produces jittery/vibrating NPCs. Power-menu (Z + spell) grab is smooth.

---

## Symptom

- `mousePosHK` alternates between correct position `(~306, ~62, ~-64)` and near-origin `(~0.2, ~-1.7, ~1.8)` on **every other frame**
- Flicker starts immediately at grab
- `totalVelocityMag` climbs from 0 → 50+ as oscillation builds
- NPC visibly vibrates/shakes

---

## Root Cause (Identified — MULTI-PHASE)

**Phase 1 (effect's near-zero write):** GrabActor effect's `Update()` writes `m_mouseWorldPos` at +0x50 every frame. On odd frames it writes near-origin `(0.2, -1.7, 1.8)` instead of the correct player-aim position. This is the primary driver of the alternation.

**Phase 2 (our write timing):** Our `UpdateGrabState` writes mousePos AFTER the effect's update. When effect writes near-zero on odd frames, our write happens too late to prevent Havok from reading the bad value that frame. On even frames, our write lands correctly.

**Phase 3 (spring force doesn't help):** Zeroing springForce on frames with near-zero mousePos doesn't stop the alternation — the bad mousePos value itself causes Havok to compute wrong target positions regardless of spring force magnitude.

**Key difference:** G-key cast happens from `OnKeyDown` → `CastSpellImmediate` (very early in frame). Power-menu cast happens after Z-key opens the menu and navigates to spell — completely different code path and timing.

---

## Attempted Fixes (15 attempts)

| # | Approach | Date | Result |
|---|----------|------|--------|
| 1 | Remove all our mousePos writes — let GrabActor effect manage it entirely | Apr 23 | **Still jittery** — effect itself produces the alternation |
| 2 | Pre-set `grabbedObject`, `grabDistance`, `grabObjectWeight=0` before CastSpellImmediate | Apr 23 | **Still jittery** |
| 3 | `DispatchStaticCall` → Papyrus `Spell.Cast` (async, different pipeline) | Apr 23 | **Still jittery** |
| 4 | `StartGrabObject()` instead of spell cast | Apr 23 | **Doesn't create spring for NPCs** (early test, no pre-set grabDistance/grabbedObject) |
| 15 | StartGrabObject() primary + CastSpellImmediate fallback + VMT skip-original hook | Apr 23 | **Still jittery** — StartGrabObject creates spring (size>0 confirmed), spring still jitters. CastSpellImmediate fallback also jitters |
| 5 | `AddTask` to defer the cast to next frame tick (kInstant caster) | Apr 23 | **Still jittery** |
| 6 | `AddTask` with `kRightHand` caster (mimic power-menu hand) | Apr 23 | **Still jittery** |
| 7 | Dispel GrabActor effect immediately after grab | Apr 23 | **Breaks grab** — IsGrabbing goes false, spring destroyed |
| 8 | Re-enable mousePos overwrite in UpdateGrabState | Apr 23 | **Still jittery** — effect's alternating near-zero write still corrupts on odd frames |
| 9 | Add "Write mousePos" debug logging in UpdateGrabState | Apr 23 | **Still jittery** — discovered velocity climbs even with stable mousePos |
| 10 | Tune spring: damping 0.9→2.5, elasticity 0.1→0.01, maxForce 1000→200 | Apr 23 | **NPC frozen** — too stiff, no movement |
| 11 | Tune spring: damping 1.5, elasticity 0.05, maxForce 500 | Apr 23 | **Still jittery** — near-zero alternation persists, velocity climbs |
| 12 | Zero springForce on bad frames (mousePos near-zero), restore on good frames | Apr 23 | **Still jittery** — alternation pattern continues regardless of force zeroing |
| 13 | Write mousePos FIRST, then check if it was near-zero and zero springForce if so | Apr 23 | **Still jittery** — effect's write still corrupts on odd frames |
| 14 | VMT hook on GrabActorEffect::Update — calls original then fixes mousePos | Apr 23 | **Still jittery** — Havok reads before our fix |
| 15 | VMT hook — skip original Update, write mousePos ourselves each frame | Apr 23 | **Still jittery** — same timing problem |

---

## Confirmed Findings (Apr 23)

### GrabActorEffect location
The effect is on the **NPC** (target), not the player. Correct scanning pattern:
```cpp
for (auto it = npcEffectList->begin(); it != npcEffectList->end(); ++it)
```

### Effect Update function
- GrabActorEffect on NPC at vtable `0x7ff604fccd40`, Update fn `0x7ff603cfcb90` (consistent across grabs)
- Virtual index 4 (offset 0x28 in x64 vtable) — ActiveEffect::Update
- Effect archetype: `RE::EffectArchetype::kGrabActor = 45`

### mousePos alternation pattern
- Correct value: `(~306, ~62, ~-64)` HK units → `(~21454, ~4334, ~-4468)` BS units
- Near-zero value: `(0.2, -1.7, 1.8)` HK units → `(14.2, -118.9, 128.0)` BS units
- Alternation: correct on even frames, near-zero on odd frames
- Effect writes near-zero AFTER our write on odd frames, so Havok reads the bad value

### BSSimpleList iteration
Use `begin()`/`end()` — not `.Begin()` or `.next`:
```cpp
for (auto it = effectList->begin(); it != effectList->end(); ++it)
```

---

## Next Steps (Priority Order)

### Option B: Manual bhkMouseSpringAction creation (RECOMMENDED)
Bypass both CastSpellImmediate and StartGrabObject. Create the spring manually with correct parameters. No effect → no oscillation → no jitter.

**Needs:** `bhkMouseSpringAction` constructor signature — requires IDA/x64dbg on Skyrim binary. RTTI IDs known: hkp=397791, bhk=394744.

### Short-term: Accept power-menu grab
Power-menu grab (Z + spell) works smooth. G-key remains experimental/jittery. Users can use power menu for dragging.

### Not worth pursuing further
- VMT hooks (attempts 14-15): oscillation originates in native grab system, not the effect's Update
- StartGrabObject (attempt 15): creates springs correctly but they still jitter — the problem is deeper than the effect
- Spring parameter tuning: doesn't fix the oscillation driver

---

## Key Offsets

| Offset | Field | Value |
|--------|-------|-------|
| +0x30 | `hkpEntity*` (grabbed body) | — |
| +0x38 | Local attachment point | Always (0,0,0) |
| +0x48 | `float m_springForce` | ~0.1556 |
| +0x4C | `float m_strength` | 1.0 |
| +0x50 | `hkVector4 m_mouseWorldPos` | (~306, ~62, ~-64) HK units |
| +0x60 | `float m_damping` | 0.9 (default) |
| +0x64 | `float m_elasticity` | 0.1 (default) |
| +0x68 | `float m_maxForce` | 1000 (default) |

Scale: `BS_TO_HK_SCALE = 0.0142875f`, `HK_TO_BS_SCALE = 69.991251f`

---

## Attempt #14 — VMT Hook: Skip Original Update, Write MousePos Ourselves (Apr 23) — FAILED

**Status:** Built and deployed, but **does not fix jitter**.

### What was built

- VMT hook installed via `WriteProcessMemory` + `VirtualProtect`
- Hook **skips calling** `g_originalGrabActorUpdate()` entirely
- Each frame: computes `mousePos = playerPos + cameraForward * grabDist` in BS units, converts to HK units (÷70), writes directly to `actionBase + 0x50`
- Camera forward extracted from `PlayerCamera::GetSingleton()->cameraRoot->world.rotate`

### Why it doesn't fix the jitter

Even bypassing the original Update doesn't stop the jitter. Possible reasons:
- Coordinate space mismatch: our mousePos computation might not match what Havok expects
- `grabDistance` from `player->GetPlayerRuntimeData().grabDistance` might not reflect actual spring length
- The spring reads other fields we haven't corrected (attachment point, other parameters)
- The grab state itself (grabbedObject, grabDistance) drives oscillation regardless of mousePos

### Attempt #15 — StartGrabObject Test (Apr 23) — FAILED

**What was tested:** Modified `TryGrabWithSpell()` to call `player->StartGrabObject()` before falling back to CastSpellImmediate.

**Flow:**
1. Set `grabObjectWeight=0`, `grabDistance=150`, `grabbedObject=crosshairTarget`
2. Call `player->StartGrabObject()` (native grab)
3. Check `grabSpring.size()` — if > 0, skip CastSpellImmediate entirely
4. If spring not created, fall back to CastSpellImmediate

**Result:** `StartGrabObject()` **DOES create a spring** for NPCs (contrary to old attempt #4 note). Log confirms `grabSpring.size > 0` after StartGrabObject. But the grab still jitters.

**Correction to old attempt #4:** "Doesn't work — native grab doesn't pick up NPCs" was wrong. StartGrabObject creates a spring on NPCs. The problem is the jitter, not the grab initiation.

**Conclusion:** The jitter is NOT caused by the GrabActor effect's Update alternating. StartGrabObject doesn't use GrabActor effect at all (no CastSpellImmediate in the success path), yet the spring still jitters. The oscillation originates in the **native grab system itself** — `player->StartGrabObject()` → spring → jitter. The GrabActor effect is a red herring.

---

## Current State

**SOLVED in v0.1.53-alpha.** The jitter was NOT caused by the GrabActor effect's Update alternation. The fix was: `CastSpellImmediate(grabSpell, false, player, ...)` — pass `target=player` instead of `target=NPC`. With delivery=Self on the spell, the effect fires on the player, and the engine uses the C++-set `grabbedObject` for the actual grab target. Smooth drag, no jitter.

The 15 attempts above were all trying to fix the symptom (mousePos alternation) while the real fix was simply changing the `target` argument in `CastSpellImmediate`.

**Key finding:** `CastSpellImmediate` 3rd arg (`target`) overrides the spell's delivery setting. Passing `target=NPC` with delivery=Self causes the GrabActor effect to fire on the NPC → dual grab conflict → jitter. Passing `target=player` makes the effect fire on the player, engine uses `grabbedObject` → smooth.

---

## Key Files

- `SKSE/src/DragHandler.cpp` — Core grab/drag/throw logic + VMT hook implementation
- `SKSE/src/DragHandler.h` — Class definition  
- `SKSE/src/Hooks.cpp` — Input event sink + `InstallGrabActorEffectHook()` via WriteProcessMemory
- `SKSE/src/main.cpp` — Plugin entry, registers hook install
- `SKSE/Plugins/DragAndDrop.ini` — Configuration
- `DragAndDrop_spriggit/` — ESP YAML source
- `docs/G-KEY-JITTER-BUG-DEBUGLOG.md` — This file
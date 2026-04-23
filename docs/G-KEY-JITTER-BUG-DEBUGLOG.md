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

## Attempted Fixes (13 attempts)

| # | Approach | Date | Result |
|---|----------|------|--------|
| 1 | Remove all our mousePos writes — let GrabActor effect manage it entirely | Apr 23 | **Still jittery** — effect itself produces the alternation |
| 2 | Pre-set `grabbedObject`, `grabDistance`, `grabObjectWeight=0` before CastSpellImmediate | Apr 23 | **Still jittery** |
| 3 | `DispatchStaticCall` → Papyrus `Spell.Cast` (async, different pipeline) | Apr 23 | **Still jittery** |
| 4 | `StartGrabObject()` instead of spell cast | Apr 23 | **Doesn't work** — native grab doesn't pick up NPCs with weight limits |
| 5 | `AddTask` to defer the cast to next frame tick (kInstant caster) | Apr 23 | **Still jittery** |
| 6 | `AddTask` with `kRightHand` caster (mimic power-menu hand) | Apr 23 | **Still jittery** |
| 7 | Dispel GrabActor effect immediately after grab | Apr 23 | **Breaks grab** — IsGrabbing goes false, spring destroyed |
| 8 | Re-enable mousePos overwrite in UpdateGrabState | Apr 23 | **Still jittery** — effect's alternating near-zero write still corrupts on odd frames |
| 9 | Add "Write mousePos" debug logging in UpdateGrabState | Apr 23 | **Still jittery** — discovered velocity climbs even with stable mousePos |
| 10 | Tune spring: damping 0.9→2.5, elasticity 0.1→0.01, maxForce 1000→200 | Apr 23 | **NPC frozen** — too stiff, no movement |
| 11 | Tune spring: damping 1.5, elasticity 0.05, maxForce 500 | Apr 23 | **Still jittery** — near-zero alternation persists, velocity climbs |
| 12 | Zero springForce on bad frames (mousePos near-zero), restore on good frames | Apr 23 | **Still jittery** — alternation pattern continues regardless of force zeroing |
| 13 | Write mousePos FIRST, then check if it was near-zero and zero springForce if so | Apr 23 | **Still jittery** — effect's write still corrupts on odd frames |

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

### Option A: Hook GrabActorEffect::Update (VMT patching)
Patch the vtable at `0x7ff603cfcb90` (virtual index 4 = offset 0x28) to intercept the near-zero mousePos write.

**Steps:**
1. Allocate trampoline via `SKSE::AllocTrampoline(128)`
2. Write a 5-byte jmp from UpdateFn to our detour
3. In detour: check if m_mouseWorldPos would be written to near-zero; if so, skip the write

**Risks:** Version-dependent, crashes if vtable offset is wrong

### Option B: Create Havok mouse spring manually
Bypass GrabActor entirely. Create `bhkMouseSpringAction` manually and add to player's `grabSpring` array.

**Needs:** `bhkMouseSpringAction` constructor signature (not yet found), RTTI IDs (hkp=397791, bhk=394744)

### Option C: Run power-menu grab to measure velocity baseline
Does power-menu grab stay stable from t=0? If yes, the difference reveals what initialization path affects.

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

## Attempt #14 — VMT Hook on GrabActorEffect::Update (Apr 23) — FAILED

**Status:** Built and working (hook fires every frame), but **does not fix jitter**.

### What was built

- Hook installed via `WriteProcessMemory` + `VirtualProtect` on vtable slot at `VTABLE_GrabActorEffect[0] + 0x28`
- Hook function calls original Update first, then checks if mousePos (+0x50) is near-zero (all components < 5.0f)
- When near-zero detected, computes correct mousePos from player position + yaw and writes it; also sets springForce to 0.1556
- Hook fires every frame on all GrabActorEffect instances

### Why it doesn't fix the jitter

**The fundamental timing problem:**
1. Effect's Update runs → writes mousePos (sometimes near-zero `(0.4, -0.5, 0.2)`, sometimes correct `(310, 68, -64)`)
2. Hook runs AFTER effect's Update → detects near-zero → writes correct value
3. **Havok already read the near-zero value in step 1** before our hook could fix it
4. Next frame: effect writes near-zero again → cycle repeats

The effect's Update alternation appears to be **internally driven** — same effect instance (`0x280c4de34c0`) produces correct on one Update call and near-zero on the next, suggesting the effect reads from some cached/flopping player state during its computation.

**Evidence from log:**
```
HOOK_UPDATE[60]: effect=0x280c4de34c0 grabbed=true isOnPlayer=false isOnNPC=true mousePos=(312.2,68.6,-63.3) force=0.1556
HOOK_UPDATE[120]: effect=0x280c4de34c0 grabbed=true isOnPlayer=false isOnNPC=true mousePos=(1.2,0.3,0.6) force=0.1556
```
Same effect instance, same grabbed=true, same isOnNPC=true — but alternates between correct and near-zero.

**Velocity keeps climbing** (5.55 → 34 → 79 avgPerBody) even with the hook fixing mousePos, because the alternating force direction from the oscillation keeps adding energy. The hook can't prevent Havok from having already read the bad value.

### Key technical findings

- **Hook installation**: `VirtualProtect(PAGE_EXECUTE_READWRITE)` + `WriteProcessMemory` to write 8-byte pointer — works reliably
- **Hook fires every frame** — not just every 60 frames (counter in logs was for separate debug output interval)
- **Effect on NPC, not player** — confirmed again: `isOnPlayer=false isOnNPC=true`
- **Effect alternation is internal** — not caused by player movement or frame timing; same player position produces different results on successive Update calls

### Remaining options

- **Option B (manual spring creation)**: Bypass GrabActor effect entirely. Create `bhkMouseSpringAction` manually and manage spring lifecycle independently. No effect → no effect's bad Update → no oscillation.
- **Accept power-menu grab** for smooth dragging, G-key remains jittery
- **Stop trying to fix mousePos** — accept that we can't intercept between effect write and Havok read with this approach

---

## Current State

VMT hook (attempt #14) is functional but does NOT fix G-key grab jitter. The hook correctly identifies and fixes near-zero mousePos, but Havok reads the bad value before our fix can apply, and the effect's alternating write continues every frame. Velocity escalation persists.

**Recommended next step: Option B — manual spring creation.** Creating the spring manually bypasses the effect entirely and eliminates the oscillation at its source.

---

## Key Files

- `SKSE/src/DragHandler.cpp` — Core grab/drag/throw logic + VMT hook implementation
- `SKSE/src/DragHandler.h` — Class definition  
- `SKSE/src/Hooks.cpp` — Input event sink + `InstallGrabActorEffectHook()` via WriteProcessMemory
- `SKSE/src/main.cpp` — Plugin entry, registers hook install
- `SKSE/Plugins/DragAndDrop.ini` — Configuration
- `DragAndDrop_spriggit/` — ESP YAML source
- `docs/G-KEY-JITTER-BUG-DEBUGLOG.md` — This file
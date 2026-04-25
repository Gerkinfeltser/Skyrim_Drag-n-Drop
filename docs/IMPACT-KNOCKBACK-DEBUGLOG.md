# Drag & Drop — Impact Knockback: Design & Debug Log

**Date:** Fri Apr 24 2026
**Status:** WORKING (v0.1.85-alpha)

---

## Goal

When a thrown/dropped/swung NPC hits a standing actor, apply knockback via `PushActorAway`.

---

## The Problem

Ragdoll bodies don't collide with standing actor capsules in Havok — thrown ragdoll NPCs pass right through standing actors. No engine collision event, no physics response.

---

## What Doesn't Work

1. **Cloak spells on thrown NPCs** — Dead/ragdolled NPCs can't process magic effects. Cloak never fires.
2. **Havok impulse on standing actor bodies** — `CollectAllRigidBodies` returns bodies but they're keyframed by animation. Impulse has no visible effect.
3. **`Actor::Stagger`** — Doesn't exist in CommonLibSSE. No C++ knock/push/stagger method on Actor.
4. **Direct `PushActorAway`** — Papyrus-only. Can't call from C++.

---

## The Solution

C++ proximity detection + Papyrus spell cast for standing actors, Havok impulse for ragdolled actors.

### Impact Detection (C++, State::TrackingImpact)

`UpdateGrabState()` enters `State::TrackingImpact` after every release (throw, or drop if `bImpactOnDrop=true`). Each frame:

1. Collect ragdoll body velocities from thrown NPC
2. If `avgSpeed < impactMinVelocity` → NPC stopped, end tracking
3. If elapsed > `impactDuration` → timeout, end tracking
4. Get thrown NPC position from `Get3D()->world.translate` (NOT `GetPosition()` — returns original position, doesn't track ragdoll)
5. Iterate all actors via `ProcessLists::ForAllActors`
6. Skip: thrown NPC itself, player, dead actors, teammates
7. Check 2D distance (X/Y only) from thrown NPC to each actor
8. If within `impactRadius` and not already hit → apply knockback

### Knockback

- **Standing actors** (alive, not ragdolled): Cast spell `0x808` (DragDropImpactSpell) from player onto target via `CastSpellImmediate`. The spell MGEF `0x807` fires `DragDropImpactScript.OnEffectStart` → `akCaster.PushActorAway(akTarget, 5.0)`
- **Dead/ragdolled actors**: Havok impulse to all rigid bodies via `ApplyClampedImpulse` (clamped to `fRagdollMaxVelocity`)

### Hit-Once Tracking

`unordered_set<FormID> impactHitActors` prevents double-hits. Cleared at start of each tracking session.

### Speed Scaling

- Force scales by `avgSpeed * impactForceSpeedScale` for ragdoll impulse
- Damage scales by `1.0 + (speed * impactDamageSpeedScale)` for both thrown and swing impacts
- `speed` = `avgSpeed` (throw tracking) or `springSpeed` (swing)

### Swing Impact (During Drag)

During drag state, proximity detection pushes nearby actors and static objects:

1. 0.5s grace period after grab starts before swing impact activates
2. Reads spring body velocity (reflects camera swing speed)
3. Uses `TESObjectCELL::ForEachReferenceInRange` for spatial queries
4. Swing radius = `fImpactRadius * fSwingImpactRadiusMult`
5. Standing actors: same PushActorAway spell cast
6. Ragdolled actors: Havok impulse with speed-scaled force
7. Static objects: only applies impulse to `kDynamic` bodies (baskets, clutter)
8. Cooldown per target (`swingCooldowns` map) prevents spam
9. Damage applies to both hit actor and dragged NPC (with thrown multiplier)

### Velocity Clamping (During Drag)

Every frame during drag, all ragdoll body velocities (except the spring body) are clamped to `fDragMaxVelocity`. The spring body is excluded so it retains real velocity for drop momentum. Prevents moon-gravity flings from fast camera swings. Separate from impact impulse clamping (`fRagdollMaxVelocity`).

### Tether Distance

If NPC ragdoll center exceeds `fGrabTetherDist` from player during drag, auto-drops. Safety net for NPCs getting knocked away during combat.

---

## Key Discoveries

### `Actor::GetPosition()` vs `Get3D()->world.translate`

`GetPosition()` returns the actor's **logical** position — where they were when ragdolled. Does NOT update as ragdoll moves. Must use `Get3D()->world.translate` for actual ragdoll position.

### `Actor::GetLinearVelocity()` returns ~0 for ragdolled NPCs

Must read velocity from individual ragdoll bodies via `body->motion.linearVelocity`. Average across all bodies for speed threshold check.

### 2D distance for proximity checks

Ragdoll center is at torso height while `GetPosition()` returns feet level. 3D distance fails — must use X/Y only (2D distance).

### `Actor::IsHostileToActor(player)`

Exists in CommonLibSSE for hostility checks. Used by `bGrabHostile` setting.

### `CastSpellImmediate` target arg

`caster->CastSpellImmediate(spell, false, &targetActor, 1.0f, false, 0.0f, nullptr)` — the `targetActor` arg is who receives the spell. With Delivery: TargetActor, `akTarget` in Papyrus = the standing actor, `akCaster` = player.

### Collision filter (tried, removed)

Tried clearing lower 16 bits of `collisionFilterInfo` during drag to prevent ragdoll tangling. Cleared ALL bits which let bodies phase through the world. Removed — caused more problems than it solved.

---

## ESP Records

| FormID | Type | EditorID | Purpose |
|--------|------|----------|---------|
| 0x806 | MGEF | DragDropImpactEffect | Cloak archetype (not used in current C++ path) |
| 0x807 | MGEF | DragDropImpactHitEffect | Script archetype, binds DragDropImpactScript via VirtualMachineAdapter |
| 0x808 | Spell | DragDropImpactSpell | FireAndForget, Delivery: TargetActor, contains MGEF 0x807 |
| 0x809 | Spell | DragDropImpactCloakSpell | Cloak wrapper (not used) |

---

## Config (INI — `SKSE/Plugins/DragAndDrop.ini`)

```ini
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

[General]
fDragMaxVelocity = 5.0         ; Max ragdoll body velocity during drag
fGrabTetherDist = 600.0        ; Auto-drop if NPC exceeds this distance from player
```

---

## Flow Diagram

```
DoRelease() → state = TrackingImpact
     │
     ▼
UpdateGrabState() [each frame]
     │
     ├─ timeout? → state = None
     ├─ NPC stopped? → state = None
     │
     ├─ ForAllActors:
     │     ├─ skip: self, player, dead, teammate
     │     ├─ 2D dist < radius && not already hit?
     │     │     ├─ IsDead() || IsInRagdollState()?
     │     │     │     └─ Apply Havok impulse to ragdoll bodies (clamped)
     │     │     └─ else (standing actor)
     │     │           └─ CastSpellImmediate(0x808, target=actor)
     │     │                 └─ DragDropImpactScript.OnEffectStart
     │     │                       └─ player.PushActorAway(actor, 5.0)
     │     ├─ impactDamage > 0? → damage hit actor + thrown NPC
     │     └─ add to impactHitActors

During Drag (State::Dragging):
     │
     ├─ Clamp all ragdoll velocities to fDragMaxVelocity
     ├─ If dist > fGrabTetherDist → auto-drop
     ├─ If grabElapsed < 0.5s → skip swing impact (grace period)
     ├─ Read spring body velocity
     ├─ If springSpeed < impactMinVelocity → skip
     ├─ ForEachReferenceInRange(swingRadius):
     │     ├─ Standing actor → PushActorAway spell + damage
     │     ├─ Ragdolled actor → Havok impulse (clamped) + damage
     │     └─ Static (kDynamic only) → Havok impulse (clamped)
     └─ Cooldown per target
```

---

## Files

| File | Role |
|------|------|
| `SKSE/src/DragHandler.cpp` | Impact tracking in `UpdateGrabState()`, swing impact during drag, velocity clamping, tether check |
| `SKSE/src/DragHandler.h` | Config members: all impact/drag/throw settings |
| `Source/Scripts/DragDropImpactScript.psc` | Papyrus: PushActorAway with null/self/teammate/dead guards |
| `scripts/DragDropImpactScript.pex` | Compiled |
| `SKSE/Plugins/DragAndDrop.ini` | All configurable settings |
| `DragAndDrop_spriggit/MagicEffects/DragDropImpactHitEffect - 000807_DragAndDrop.esp.yaml` | MGEF with VirtualMachineAdapter |
| `DragAndDrop_spriggit/Spells/DragDropImpactSpell - 000808_DragAndDrop.esp.yaml` | Spell with TargetActor delivery |

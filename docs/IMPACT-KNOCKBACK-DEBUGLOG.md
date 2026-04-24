# Drag & Drop — Impact Knockback: Design & Debug Log

**Date:** Fri Apr 24 2026
**Status:** WORKING (v0.1.58-alpha)

---

## Goal

When a thrown/dropped NPC hits a standing actor, apply visible knockback to that actor.

---

## The Problem

Ragdoll bodies don't collide with standing actor capsules in Havok — thrown ragdoll NPCs pass right through standing actors. No engine collision event, no physics response.

---

## What Doesn't Work

1. **Cloak spells on thrown NPCs** — Dead/ragdolled NPCs can't process magic effects. Cloak never fires.
2. **Havok impulse on standing actor bodies** — `CollectAllRigidBodies` returns 18 bodies for standing actors, but they're keyframed by animation. `ApplyLinearImpulse` has no visible effect.
3. **`Actor::Stagger`** — Doesn't exist in CommonLibSSE. No C++ knock/push/stagger method on Actor.
4. **Direct `PushActorAway`** — Papyrus-only. Can't call from C++.

---

## The Solution

C++ proximity detection + Papyrus spell cast.

### Detection (C++)

`UpdateGrabState()` enters `State::TrackingImpact` after every release (drop or throw). Each frame:

1. Collect ragdoll body velocities from thrown NPC
2. If `avgSpeed < impactMinVelocity` → NPC stopped, end tracking
3. Get thrown NPC position from `Get3D()->world.translate` (NOT `GetPosition()` — that returns original position, doesn't track ragdoll movement)
4. Iterate all actors via `ProcessLists::ForAllActors`
5. Skip: thrown NPC itself, player, dead actors, teammates
6. Check distance from thrown NPC to each actor
7. If within `impactRadius` and not already hit → apply knockback

### Knockback (Papyrus)

For **standing actors** (alive, not ragdolled): Cast spell `0x808` (DragDropImpactSpell) from player onto target via `CastSpellImmediate`. The spell has:
- Delivery: `TargetActor`
- MGEF `0x807` (DragDropImpactHitEffect): Script archetype with `VirtualMachineAdapter` binding `DragDropImpactScript`
- `OnEffectStart(akTarget, akCaster)` → `akCaster.PushActorAway(akTarget, 5.0)`

For **dead/ragdolled actors**: Apply Havok impulse to all rigid bodies (ragdoll-to-ragdoll knockback).

### Hit-Once Tracking

`unordered_set<FormID> impactHitActors` prevents double-hits. Cleared at start of each tracking session.

---

## Key Discoveries

### `Actor::GetPosition()` vs `Get3D()->world.translate`

`GetPosition()` returns the actor's **logical** position — where they were when ragdolled. Does NOT update as ragdoll moves. Must use `Get3D()->world.translate` for actual ragdoll position.

### `Actor::GetLinearVelocity()` returns ~0 for ragdolled NPCs

Must read velocity from individual ragdoll bodies via `body->motion.linearVelocity`. Average across all bodies for speed threshold check.

### `hkpMotion` has no `position` or `centerOfMass` member

Only virtual setters (`SetPosition`, `SetCenterOfMassInLocal`). Use NiNode world position instead.

### `CollectAllRigidBodies` returns bodies for standing actors too

Standing actors have ~18 collision bodies (keyframed by animation). Impulse has no visible effect on these. Must check `IsDead() || IsInRagdollState()` before trying ragdoll impulse path.

### `CastSpellImmediate` target arg

`caster->CastSpellImmediate(spell, false, &targetActor, 1.0f, false, 0.0f, nullptr)` — the `targetActor` arg is who receives the spell. With Delivery: TargetActor, `akTarget` in Papyrus = the standing actor, `akCaster` = player.

---

## ESP Records

| FormID | Type | EditorID | Purpose |
|--------|------|----------|---------|
| 0x806 | MGEF | DragDropImpactEffect | Cloak archetype (not used in current C++ path) |
| 0x807 | MGEF | DragDropImpactHitEffect | Script archetype, binds DragDropImpactScript via VirtualMachineAdapter |
| 0x808 | Spell | DragDropImpactSpell | FireAndForget, Delivery: TargetActor, contains MGEF 0x807 |
| 0x809 | Spell | DragDropImpactCloakSpell | Cloak wrapper (not used) |

---

## Config (hardcoded in DragHandler.h)

```
impactRadius = 200.0f       ; proximity detection radius
impactDuration = 3.0f       ; max tracking time after release
impactMinVelocity = 0.5f    ; stop tracking below this speed
impactForce = 300.0f        ; Havok impulse magnitude (ragdoll targets only)
PushActorAway force = 5.0   ; Papyrus knockback force (standing targets)
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
     │     ├─ dist < radius && not already hit?
     │     │     ├─ IsDead() || IsInRagdollState()?
     │     │     │     └─ Apply Havok impulse to ragdoll bodies
     │     │     └─ else (standing actor)
     │     │           └─ CastSpellImmediate(0x808, target=actor)
     │     │                 └─ DragDropImpactScript.OnEffectStart
     │     │                       └─ player.PushActorAway(actor, 5.0)
     │     └─ add to impactHitActors
```

---

## Files

| File | Role |
|------|------|
| `SKSE/src/DragHandler.cpp` | Impact detection in `UpdateGrabState()` (State::TrackingImpact), starts in `DoRelease()` |
| `SKSE/src/DragHandler.h` | Config: impactRadius, impactDuration, impactMinVelocity, impactForce, impactHitActors |
| `Source/Scripts/DragDropImpactScript.psc` | Papyrus: PushActorAway with null/self/teammate/dead guards |
| `scripts/DragDropImpactScript.pex` | Compiled |
| `DragAndDrop_spriggit/MagicEffects/DragDropImpactHitEffect - 000807_DragAndDrop.esp.yaml` | MGEF with VirtualMachineAdapter |
| `DragAndDrop_spriggit/Spells/DragDropImpactSpell - 000808_DragAndDrop.esp.yaml` | Spell with TargetActor delivery |

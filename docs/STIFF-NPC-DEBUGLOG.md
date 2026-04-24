# Stiff NPC (KO/Paralyzed) — Debug Log

**Date:** Thu Apr 24 2026  
**Problem:** Reloaded KO'd NPCs are stiff/frozen when grabbed. Fresh KO'd NPCs drag fine.

---

## Symptom

- Fresh KO'd NPCs ragdoll normally when grabbed — smooth drag
- Reloaded KO'd NPCs (from save) are stiff/frozen — grab attaches but NPC doesn't move with the spring
- The KO mod applies `Paralysis=1.0` continuously via an active magic effect
- On reload, the ragdoll state is "baked" into the save and doesn't resume properly

---

## Attempted Fixes (v0.1.54–v0.1.65, rolled back to v0.1.53)

| # | Version | Approach | Result |
|---|---------|----------|--------|
| 1 | 0.1.54 | `SetActorValue(kParalysis, 0)` before CastSpellImmediate | No effect — KO mod re-applies paralysis every frame |
| 2 | 0.1.56 | Debug logging for paralysis, actor values, state | Confirmed KO mod stacks paralysis (1.0→2.0→3.0...) on each cast |
| 3 | 0.1.57 | Two-spell approach: DragDropRagdollSpell (Script archetype) + PushActorAway | Papyrus script fires, NPC jiggles but stays stiff. KO mod re-applies paralysis |
| 4 | 0.1.58 | MGEF archetype changed from Light→Script (Light didn't fire scripts) | Script fires correctly now. Still stiff. |
| 5 | 0.1.60 | `DispelEffectsWithArchetype(kParalysis)` in C++ before ragdoll spell | Dispel works! NPC ragdolls properly. But NPC wakes up (conscious) after dispel. |
| 6 | 0.1.61 | Dispel + ForceRagdoll (NotifyAnimationGraph + AddRagdollToWorld + SetMotionType) | NPC moved briefly then **game hard froze** |
| 7 | 0.1.62 | Dispel only (no ForceRagdoll) + cast ragdoll spell | NPC ragdolls and wakes up. No freeze. Still stiff during grab. |
| 8 | 0.1.63 | Save KO paralysis spells before dispel, re-add via AddSpell on release | Spells saved correctly, AddSpell fires, but NPC still wakes up on release (conscious, not re-KO'd) |
| 9 | 0.1.64 | Revert to simple dispel + SetActorValue(kParalysis, 0) on release | NPC wakes up on drop/throw. Still stiff during drag. |
| 10 | 0.1.65 | WakeRagdollBodies (set all rigid bodies to kDynamic, no animation graph) + dispel | G-key: nothing visible. Power menu cast: **game froze**. Rolled back. |

---

## Key Findings

### What works
- **DispelEffectsWithArchetype(kParalysis)** — actually removes the KO mod's paralysis effect. NPC ragdolls properly after dispel.
- **PushActorAway via Papyrus** — gives a physical nudge that helps unstick bodies
- **CastSpellImmediate(target=player)** — the jitter fix from v0.1.53 remains solid

### What doesn't work
- **SetActorValue(kParalysis, 0)** alone — KO mod re-applies immediately, value sticks at 1.0
- **ForceRagdoll (full)** — game freeze (NotifyAnimationGraph + AddRagdollToWorld too aggressive)
- **WakeRagdollBodies (bodies only)** — either does nothing visible or causes freeze depending on code path
- **Saving and re-adding spells** — AddSpell fires but NPC doesn't re-enter KO state properly
- **Any physics body manipulation during/after grab** — either crashes or no visible effect

### The core problem
The ragdoll state being "baked" into the save is the real issue. The KO mod's paralysis keeps the NPC frozen, and on reload the ragdoll physics don't resume. Dispelling the paralysis works to unstick, but then the NPC wakes up. The two goals (keep NPC KO'd + unstick ragdoll) are in conflict.

---

## Potential Approaches (Not Yet Tried)

1. **Post-grab dispel with delayed re-paralysis** — Dispel right before grab, then re-apply paralysis after grab completes (a few frames later). Might keep the NPC KO'd while allowing initial ragdoll to resume.

2. **Manual spring creation (Option B from jitter research)** — Bypass the entire GrabActor/spell system. Create `bhkMouseSpringAction` manually. Would need reverse engineering the constructor. Could potentially handle stiff NPCs differently since we control the spring directly.

3. **Investigate what the KO mod actually does** — Use SkyLinkAI MCP or Papyrus to enumerate active effects on the KO'd NPC. Find the specific spell/effect FormID and try targeted `RemoveSpell` + `AddSpell` with timing.

4. **Accept stiff grab for KO'd NPCs** — Document as known limitation. Only fresh KO'd NPCs drag smoothly. Reloaded KO'd NPCs are stiff but still movable (the grab does attach, just rigidly).

5. **Post-grab timing for ForceRagdoll** — Previous tests showed ForceRagdoll before CastSpellImmediate breaks the grab. But calling it AFTER the grab establishes (in UpdateGrabState when state transitions to Dragging) might work without conflicting with the spell cast.

---

## Files Added Then Rolled Back

These were created during experiments and removed in the rollback to v0.1.53:
- `DragAndDrop_spriggit/MagicEffects/DragDropRagdollEffect - 000806_DragAndDrop.esp.yaml` — Script archetype MGEF for unstick
- `DragAndDrop_spriggit/Spells/DragDropRagdollSpell - 000807_DragAndDrop.esp.yaml` — Ragdoll unstick spell
- `Source/Scripts/DragDropRagdollScript.psc` — Papyrus script (PushActorAway + paralysis clear)
- `scripts/DragDropRagdollScript.pex` — Compiled version
- ESP forms 0x806 (MGEF) and 0x807 (Spell) — no longer in ESP

All exist in git history (commits after a0345bb) if needed again.

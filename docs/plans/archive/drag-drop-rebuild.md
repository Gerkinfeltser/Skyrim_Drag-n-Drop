---
plan name: drag-drop-rebuild
plan description: Self-contained Havok drag mod
plan status: active
---

## Idea
Rebuild Drag & Drop as a self-contained Skyrim SE mod that drags/throws NPCs using Havok physics springs. Two-phase approach:

**Phase 1 — Self-Contained LesserPower (proven, ship fast)**
Ditch the Seize NPCs dependency entirely. Create our own GrabActor spell effect in the ESP based on CatchAndRelease's proven configuration (FireAndForget, Duration 999999, own GrabActor effect with proper conditions). For release/throw, adopt GrabAndThrow's direct Havok approach: access `player->grabSpring` to apply impulse via `hkpMouseSpringAction` and `ApplyLinearImpulse()`, then `DestroyMouseSprings()`. G key for release, hold R to charge throw.

**Phase 2 — Pure C++ (investigate later)**
CatchAndRelease's code calls `player->StartGrabObject()` directly from Papyrus native. If this works on ragdoll/dead NPCs, we could eliminate the spell entirely and trigger grabs from a hotkey. This is a future goal, not Phase 1.

The core architecture change: stop fighting GrabActor spell quirks for the release side. Use spells ONLY to initiate the grab (make NPC grabbable), then handle everything else in C++ via direct Havok manipulation.

## Implementation
- Create self-contained GrabActor MagicEffect (0x801) with CatchAndRelease's exact config: FireAndForget CastType, NoDuration UNCHECKED, GrabActor archetype, keyword 07F404, Hostile+Recover+Painless flags, EquipAbility chain, proper target conditions (dead/paralyzed/follower/child, distance <= 150, no ghosts/immune-to-paralysis)
- Create EquipAbility (0x805) and bare Script equip effect (0x804) matching Seize NPCs' chain structure so GrabActor archetype has the full engine-expected effect chain
- Update Spell (0x800) to reference our OWN 0x801 effect instead of Seize NPCs — remove Seize NPCs master dependency entirely. Keep FireAndForget + Duration 999999. Add stamina condition on spell level.
- Implement GrabAndThrow-style Havok throw in DragHandler.cpp: access player->grabSpring array, cast to hkpMouseSpringAction, zero linear/angular velocity, compute impulse from camera forward * force * mass * BS_TO_HK_SCALE, call ApplyLinearImpulse(), then DestroyMouseSprings()
- Add R key hold tracking for throw charge: track hold duration on key down, on release call ThrowGrabbedObject with held duration. Scale force from throwImpulseBase up to throwImpulseMax based on hold time. G key = instant release (no impulse). Both use DestroyMouseSprings for cleanup.
- Add valid target conditions to the GrabActor effect YAML matching CatchAndRelease exactly: Stamina > 2, AND (ActorBase == 0x007 OR Paralysis > 0 OR IsDead OR IsPlayerTeammate OR IsChild OR NOT GetDetected), AND NOT HasKeyword(Ghost/ImmuneParalysis), AND NOT GetPairedAnimation, AND GetDistance <= 80
- Update Hooks.cpp: add R key (DIK_R = 0x13) tracking for throw charge. On R key down start timer, on R key up if dragging apply throw. Keep G key for release. Remove spell-based release from Papyrus.
- Remove Seize NPCs from ESP masters, remove broken 0x801 reference, update RecordData.yaml. Remove DispelEffectsWithArchetype from ReleaseNPC — only DestroyMouseSprings needed. Clean up unused Papyrus release script.

## Required Specs
<!-- SPECS_START -->
- grabactor-spell-config
- havok-physics-api
<!-- SPECS_END -->
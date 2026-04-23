# Spec: grabactor-spell-config

Scope: feature

# Self-Contained GrabActor Spell Configuration

## Reference: CatchAndRelease (working, proven)

### Effect Chain Architecture
```
Spell (0x800, LesserPower, FireAndForget)
  └── Effect 1: GrabActor MagicEffect (0x801, FireAndForget, Duration 999999)
        └── EquipAbility: 0x805 (Ability)
              └── EquipEffect: 0x804 (Script, bare)
  └── Effect 2: ReleaseEffect (0x803, Script, FireAndForget)
```

### GrabActor MagicEffect (0x801) — Critical Fields
```yaml
Keywords:
  - 07F404:Skyrim.esm          # MagicGrabActor keyword — REQUIRED
Flags:
  - Hostile                     # REQUIRED for GrabActor
  - Recover                     # REQUIRED
  - Painless                    # No aggro
  # NO NoDuration              # Must be UNCHECKED for Duration to work
  # NO NoMagnitude             # Must be UNCHECKED
  # NO NoArea                  # Must be UNCHECKED
Archetype:
  Type: GrabActor
CastType: FireAndForget          # NOT Concentration — FireAndForget with Duration
EquipAbility: 000802/805         # Points to the ability that wraps equip effect
CastingSoundLevel: Silent
```

### Target Conditions (on the GrabActor effect)
```
AND  GetActorValue Stamina > 2        (player has stamina)
AND (
  OR  GetIsID 0x007                   (is NPC base)
  OR  GetActorValue Paralysis > 0     (is paralyzed)
  OR  GetDead = 1                     (is dead)
  OR  GetPlayerTeammate = 1           (is follower)
  OR  IsChild = 1                     (is child)
)
AND  GetDetected(Target=Player) = 0   (target hasn't detected player) — OR covered above
AND NOT HasKeyword 0x0D205E           (not ghost)
AND NOT HasKeyword 0x0F23C5           (not immune to paralysis)
AND NOT GetPairedAnimation            (not in paired animation)
AND  GetDistance <= 80                 (within grab range)
```

### Spell (0x800) Configuration
```yaml
Type: LesserPower
CastType: FireAndForget
Flags:
  - NoAbsorbOrReflect
EquipmentType: 013F44:Skyrim.esm     # Both hands
Effects:
  - BaseEffect: 0x801 (our GrabActor effect)
    Data:
      Duration: 999999
  - BaseEffect: 0x803 (our release script effect)
    Data: {}
```

### EquipAbility Chain
```yaml
# 0x805 — Ability
Type: Ability
Effects:
  - BaseEffect: 0x804 (equip effect)
    Conditions:
      - IsCasting = 1

# 0x804 — Script effect (bare, no script attached initially)
Archetype: Script
Flags: NoHitEvent, NoDuration, NoMagnitude, NoArea, NoRecast, Painless, NoHitEffect
```

## Critical Differences from Current DragAndDrop ESP

| Field | Current (broken) | Required (working) |
|-------|-----------------|-------------------|
| Effect 0x801 CastType | Concentration | **FireAndForget** |
| Effect 0x801 NoDuration | Checked | **UNCHECKED** |
| Effect 0x801 NoMagnitude | Checked | **UNCHECKED** |
| Effect 0x801 NoArea | Checked | **UNCHECKED** |
| Spell references | 000801:Seize NPCs.esp | **000801:DragAndDrop.esp** (our own) |
| Masters | Skyrim + Update + Seize NPCs | **Skyrim + Update only** |
| Target conditions | Missing | **Full condition set** |
| Equip effect script | None | **May need GrabActorEquipEffectScript** |
| Spell TargetType | TargetActor | **Remove (self-target)** |

## Spriggit Gotchas
- Spriggit **cannot** reproduce working GrabActor binary structure from scratch
- Workaround: configure all fields correctly in YAML, deserialize to ESP
- If Spriggit-generated ESP still doesn't work, create the MagicEffect in xEdit manually using CatchAndRelease's effect as template
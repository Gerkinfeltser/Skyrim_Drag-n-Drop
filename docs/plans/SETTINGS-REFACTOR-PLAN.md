# Plan: Settings Refactor — Globals + Papyrus as Single Source of Truth

## Problem Statement

1. **INI path bug**: `bGrabAnyone=true` not loading (incorrect path resolved for MO2)
2. **Spell hand bypass**: `CastSpellImmediate` hook doesn't fire — casting from hand uses `Actor::CastSpell` directly, bypassing `IsValidTarget()`. G-key is jittery and unreliable as primary grab.
3. **Dual enforcement**: Boolean settings were enforced in C++ (`IsValidTarget`) for G-key, and ineffective for spell hand cast. Numeric settings lived in INI. No single source of truth.

## Goal

Refactor so:
- **Spell hand (right/left/both)** = primary (smooth) grab path, respects all settings via spell conditions checking globals
- **G-key** = drop/throw key — pressed briefly = drop, held = charge throw, release = throw. G-key no longer initiates grab.
- All boolean settings via **ESP globals** — checked by spell conditions (hand cast) and G-key hook
- All numeric settings via **C++ state** — accessed by G-key hook and throw/drop code
- **MCM** updates both globals (booleans) and C++ state (numerics) via Papyrus functions
- No INI — no path bugs, no dual enforcement

## Settings Inventory

### Boolean Settings (Globals — who can be grabbed)

| Original INI Key | Global Name | Type | Notes |
|-----------------|-------------|------|-------|
| `bGrabAnyone` | `DragDrop_AllowAll` | Bool | Allow non-follower, non-dead, non-child |
| `bGrabFollowers` | `DragDrop_AllowFollowers` | Bool | Allow player teammates |
| `bGrabChildren` | `DragDrop_AllowChildren` | Bool | Allow child actors |
| `bGrabDead` | `DragDrop_AllowDead` | Bool | Allow dead actors (bonus) |

**Free conditions (no global needed):** `GetDead()`, `GetActorValue(Paralysis) > 0`, `IsChild()` — native Skyrim conditions.

### Numeric Settings (C++ + Papyrus)

| Original INI Key | Papyrus Variable | Type | Default | Notes |
|-----------------|-------------------|------|---------|-------|
| `bEnableMod` | `ModEnabled` | Bool | true | Enable/disable grab entirely |
| `fGrabRange` | `GrabRange` | Float | 150.0 | Max distance to grab |
| `fThrowImpulseMax` | `ThrowImpulseMax` | Float | 10.0 | Max throw force |
| `fThrowDropWindow` | `ThrowDropWindow` | Float | 0.5 | Tap R = drop, hold > window = throw |
| `fThrowTimeToMax` | `ThrowTimeToMax` | Float | 4.0 | Seconds to charge max throw |
| `fActionKey` | `ActionKey` | Int | 34 | G-key scancode |

**Note:** `fStaminaDrainRate` exists in INI but is stub-only (not wired to frame tick). Skip for now.

### All Settings Summary

| Key | Type | Storage | Access |
|-----|------|---------|--------|
| `bEnableMod` | Bool | C++ state | G-key hook checks |
| `bGrabAnyone` | Bool | Global | Spell condition + hook |
| `bGrabFollowers` | Bool | Global | Spell condition + hook |
| `bGrabChildren` | Bool | Global | Spell condition + hook |
| `bGrabDead` | Bool | Global | Spell condition + hook |
| `fGrabRange` | Float | C++ state (Papyrus) | Hook + throw code |
| `fThrowImpulseMax` | Float | C++ state (Papyrus) | Throw code |
| `fThrowDropWindow` | Float | C++ state (Papyrus) | G-key release timing |
| `fThrowTimeToMax` | Float | C++ state (Papyrus) | Charge calculation |
| `fActionKey` | Int | C++ state | G-key scancode |

## Architecture

### Globals Needed (ESP)

```
DragDrop_AllowAll        (Bool) := bGrabAnyone
DragDrop_AllowFollowers (Bool) := bGrabFollowers  
DragDrop_AllowChildren  (Bool) := bGrabChildren
DragDrop_AllowDead       (Bool) := bGrabDead (for completeness)
```

### Papyrus Quest (`DragDrop`)

A persistent quest script holding numeric settings as member variables:

```papyrus
Scriptname DragDrop extends Quest

float GrabRange = 150.0
float ThrowImpulseMax = 10.0
float ThrowDropWindow = 0.5
float ThrowTimeToMax = 4.0
int ActionKey = 34
bool ModEnabled = true

function SetGrabRange(float value)
    GrabRange = value
endFunction

float function GetGrabRange()
    return GrabRange
endFunction

; similar getters/setters for all numeric settings
```

### Spell Conditions (GrabActor Effect)

```
GetGlobalValue(DragDrop_AllowAll) == 1
OR GetDead()
OR GetActorValue(Paralysis) > 0
OR (GetGlobalValue(DragDrop_AllowFollowers) == 1 AND IsPlayerTeammate())
OR (GetGlobalValue(DragDrop_AllowChildren) == 1 AND IsChild())
```

Note: `GetDead()` and `IsChild()` and `Paralysis` are free — no globals needed.

## Pros

- **Single source of truth**: globals + C++ state, no INI path issues
- **Spell hand respects all boolean settings**: spell conditions check globals
- **G-key uses same gating**: hook reads globals
- **MCM works naturally**: writes globals (Papyrus) and C++ state (Papyrus getters)
- **No duplicate enforcement logic**: boolean gating is only in spell conditions and hook
- **Globals are per-character**: settings persist per save
- **G-key repurposed**: no longer tries to grab (which jittered), now purely drop/throw

## Cons

- **Need to rebuild ESP** to add new globals — more involved than editing INI
- **Numeric settings still in C++** — no Papyrus-native storage, must go through getters/setters
- **Drop/throw still ungated** — can't hook native functions without IDA

## Implementation Steps

### Phase 1: ESP Changes (Globals + Spell Conditions)

1. Add 4 globals to `DragAndDrop_spriggit/Quests/DragDropQuest.yaml`:
   - `DragDrop_AllowAll` (Bool)
   - `DragDrop_AllowFollowers` (Bool)
   - `DragDrop_AllowChildren` (Bool)
   - `DragDrop_AllowDead` (Bool)
2. Update `DragDropGrabEffect.yaml` conditions to check globals as described
3. Rebuild ESP via Spriggit
4. Rebuild plugin (`cmake --build`)

### Phase 2: C++ Changes (Remove INI, Repurpose G-key)

1. Remove `LoadSettings()` and INI parsing entirely from `DragHandler.cpp`
2. Add `IsGlobalAllowed(actor)` function that reads the 4 globals
3. Update G-key logic — no longer casts spell, only handles drop/throw
4. Keep CastSpellImmediate hooks — still work for G-key (now reading globals instead of INI)
5. Numeric state stored in C++, shared with Papyrus via trampoline

### Phase 3: Papyrus API

1. Add `DragDrop.psc` Papyrus script with:
   - Member variables for all numeric settings
   - Getter/setter functions for each
   - `OnInit()` to register with plugin
2. Plugin registers functions via `SKSE::GetPapyrusInterface()->Register()`
3. Plugin exposes global state pointer to Papyrus via trampoline

### Phase 4: MCM Integration

1. MCM calls Papyrus functions to update settings
2. Numeric updates go through Papyrus getters/setters
3. Boolean updates go through global setters via Papyrus

## Files to Modify

- `DragAndDrop_spriggit/Quests/DragDropQuest.yaml` — add globals
- `DragAndDrop_spriggit/MagicEffects/DragDropGrabEffect.yaml` — update conditions
- `SKSE/src/DragHandler.cpp` — remove INI, add global reads, repurpose G-key
- `SKSE/src/main.cpp` — version bump
- `Source/Scripts/DragDrop.psc` — Papyrus quest with getters/setters
- `docs/` — update specs to reflect new architecture

## Open Questions

1. **Per-character vs global**: Globals are per-save. Is this the desired behavior for all settings?
2. **Initial defaults**: ESP globals define defaults, MCM overrides per-session. Acceptable?

## Limitations

**Drop/throw can't be gated without IDA.** `Actor::ThrowObject()` and `DestroyMouseSprings()` are native game functions. To hook them we'd need their memory addresses, which requires reverse-engineering the Skyrim binary with a disassembler like **IDA Pro** (Hex-Rays). We can't obtain those addresses through safe code inspection alone.

**G-key jitter remains.** The alternating mousePos problem in `GrabActorEffect::Update` is internal to the game's effect. Spell hand becomes the primary grab path because it's smooth. G-key is repurposed as the drop/throw key — tap = drop, hold = charge throw, release = throw. G-key no longer initiates grab.
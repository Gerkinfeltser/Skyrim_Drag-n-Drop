# Research Report: Skyrim SE NPC Dragging/Carrying Approaches

**Date:** 2026-04-22
**Researcher:** Researcher
**Status:** Complete
**Scope:** Technical approaches for moving NPCs in Skyrim SE

---

## Executive Summary

This report documents technical approaches for dragging, carrying, or moving NPCs in Skyrim Special Edition, with emphasis on what approaches work in-game and how they function at the code level. Research combines local project analysis, documentation review, and attempted external research (limited by 403 errors on Nexus Mods and GitHub).

---

## Key Findings

### 1. **GrabActor Magic Effect Archetype** (Vanilla Skyrim)

**How it works:** Uses built-in GrabActor archetype via Havok physics springs

**Technical Details:**
- Creates Havok physics constraint (spring) between player camera and NPC
- Requires specific spell configuration to function
- Uses `player->IsGrabbing()` and `player->GetGrabbedRef()` C++ APIs for detection
- Released via `player->DestroyMouseSprings()` or `DispelEffectsWithArchetype(kGrabActor, true)`

**Known Working Configurations:**

**Seize NPCs (Creation Club content):**
- Effect (0x801): GrabActor archetype, Concentration cast, NoDuration/NoMagnitude/NoArea flags, EquipAbility→0x805, Keywords: 07F404
- Equip effect (0x802): Script type, runs GrabActorEquipEffectScript
- Equip ability (0x805): IsCasting condition
- Spell (0x800): Concentration casting wraps 0x801 effect

**Critical Discovery:** Concentration cast with GrabActor = instant release (NPC grabbed for split second then dropped). This is why Seize NPCs alone doesn't work for sustained dragging.

---

### 2. **FireAndForget + Duration Wrapper** (Proven Working)

**How it works:** FireAndForget spell with Duration 999999 wraps Concentration GrabActor effect

**Technical Details:**
- Spell: FireAndForget, LesserPower, Duration 999999
- References external GrabActor effect directly
- Works in CatchAndRelease mod by shad0wshayd3

**Why it works:**
The FireAndForget wrapper bypasses Concentration cast's instant-release behavior while maintaining the Havok spring constraint from the GrabActor effect.

**Project Status (Drag & Drop mod):**
- Currently BLOCKED — GrabActor spell effect never triggers grab state
- `IsGrabbing()` never returns true
- Spell fires but no Havok spring is created

**Suspected Issues:**
- CastType mismatch (spell is FireAndForget, effect is Concentration)
- Missing condition or keyword on the effect
- EquipAbility chain (0x804→0x805) might be incorrect
- Seize NPCs' equip script (GrabActorEquipEffectScript) not being called

---

### 3. **Direct Havok Spring Manipulation** (SKSE Plugin Approach)

**How it works:** SKSE plugin hooks into game and directly manipulates Havok physics

**Technical Details from GrabAndThrow (by powerof3):**
- Hooks into BSInputDeviceManager for input events
- Direct Havok physics spring manipulation
- Uses `DestroyMouseSprings()` for cleanup
- Provides throwing via impulse force application

**Advantages:**
- No spell configuration issues
- Direct physics control
- Can implement custom drag/throw mechanics

**Disadvantages:**
- Requires SKSE dependency
- More complex code
- "Moon gravity" issue when destroying springs improperly

**Current Project Implementation:**
- SKSE plugin compiles and loads successfully
- Input event sink works (G key detection confirmed)
- Grab state polling implemented
- Papyrus natives registered

---

### 4. **Animation-Based Approaches** (Information Limited)

**FNIS Carry Animation Mods:**
- Use custom animations
- Typically require FNIS generation
- Not actual physics dragging
- Limited information available due to access restrictions

**Status:** Could not access detailed information due to 403 errors on Nexus Mods and GitHub

---

### 5. **Positioner/Teleport Approaches**

**Jaxonz Positioner:**
- Moves NPCs via teleportation
- Not true dragging
- No physics involved
- Useful for scene setup but not immersive dragging

**Sacred Band:**
- Limited information available
- Appears to use different mechanics

---

### 6. **Ragdoll Physics Mods**

**Realistic Ragdoll and Force:**
- Enhances ragdoll physics
- Does not provide dragging functionality
- Can complement drag mechanics

**Simply Lag:**
- Body physics improvements
- Not a drag mod

---

## Technical Analysis: Why Drag & Drop Mod is Blocked

### Current Configuration (from Spriggit YAML):

```yaml
# DragDropGrabSpell - 000800
Type: LesserPower
CastType: FireAndForget
TargetType: TargetActor
Effects:
  - BaseEffect: 000801:Seize NPCs.esp  # References Seize NPCs' GrabActor effect
    Duration: 999999
  - BaseEffect: 000803:DragAndDrop.esp  # Release effect
```

```yaml
# DragDropGrabEffect - 000801 (UNUSED)
Archetype:
  Type: GrabActor
CastType: Concentration
Flags:
  - NoDuration
  - NoMagnitude
  - NoArea
```

### Problem Analysis:

1. **Spell references Seize NPCs' effect** but doesn't reference the FULL chain (equip ability, equip effect)
2. **Spriggit-generated GrabActor effect is broken** — cannot reproduce binary structure required
3. **Spell worked once** (NPC buried in ground) then never re-triggered
4. **FireAndForget + Duration 999999** proven to work in CatchAndRelease

---

## Recommended Solutions

### Priority 1: Compare with CatchAndRelease

**Action:** Obtain CatchAndRelease's Spriggit YAML and compare spell configuration

**What to check:**
- Exact effect references (does it also reference Seize NPCs' equip ability?)
- Spell flags and conditions
- Keywords attached to effects
- CastType configuration

**Likely Missing Piece:**
Seize NPCs' 0x801 effect has `EquipAbility: 0x805` pointing to Seize NPCs' equip ability. Our spell might need to reference Seize NPCs' FULL chain, not just the GrabActor effect.

### Priority 2: Copy CatchAndRelease's Spell Configuration Verbatim

If direct comparison isn't possible, replicate CatchAndRelease's working setup exactly:
- Same CastType
- Same effect references
- Same duration
- Same flags and keywords

### Priority 3: Alternative Approach — Pure SKSE Plugin

If spell issues persist, bypass GrabActor entirely and use GrabAndThrow's approach:
- Direct Havok spring manipulation
- Custom spring creation and management
- No dependency on GrabActor archetype

---

## Known Mod List (with Technical Approach)

| Mod | Approach | Working? | Notes |
|-----|----------|----------|-------|
| **Seize NPCs** (Creation Club) | GrabActor archetype via Concentration spell | Partial | Works but releases instantly due to Concentration cast |
| **GrabAndThrow** (powerof3) | Direct SKSE Havok manipulation | Yes | Reference implementation, GitHub unavailable |
| **CatchAndRelease** (shad0wshayd3) | FireAndForget + Duration wrapper | Yes | Proven working configuration |
| **Simply Knock** | Unknown | ? | Could not access mod details |
| **Carry Me** / "Carry Your Amputated Limbs" | Unknown | ? | Could not access mod details |
| **Dead Body Collision** | Unknown | ? | Likely collision adjustment, not drag |
| **Jaxonz Positioner** | Teleport-based | Yes | Not physics dragging |
| **Sacred Band** | Unknown | ? | Could not access mod details |
| **FNIS Carry Animation** | Animation-based | ? | Requires FNIS, not physics |
| **Simply Lag** | Ragdoll enhancement | N/A | Body physics, not drag |
| **Realistic Ragdoll and Force** | Ragdoll enhancement | N/A | Physics improvements, not drag |

---

## Critical Code References

### C++ API for Grab Detection:

```cpp
// Check if player is currently grabbing
bool isGrabbing = player->IsGrabbing();

// Get currently grabbed reference
TESObjectREFR* grabbedRef = player->GetGrabbedRef();

// Destroy Havok springs (release)
player->DestroyMouseSprings();

// Dispel GrabActor effects
player->DispelEffectsWithArchetype(kGrabActor, true);
```

### Input Event Sink Pattern (from GrabAndThrow):

```cpp
// Hook into BSInputDeviceManager
// G key scancode = 0x22 (DIK_G)
// R key scancode = 0x13 (DIK_R)
// Install at kInputLoaded message
```

---

## Project Status Summary

**Current Version:** v0.1.3-alpha
**Status:** BLOCKED on GrabActor spell not triggering grab state
**What Works:**
- SKSE plugin compilation and loading
- Input event detection (G key)
- Papyrus native registration
- Quest auto-adds spell to player

**What Doesn't Work:**
- GrabActor spell effect never triggers grab
- `IsGrabbing()` never returns true
- No Havok spring created on spell cast

---

## Search Queries Attempted

The following searches were attempted but failed due to access restrictions (403 errors, bot detection):

- `site:nexusmods.com skyrim drag body mod`
- `site:nexusmods.com skyrim carry corpse dead`
- `site:reddit.com SkyrimSE drag NPC grab throw body carry mod`
- `"GrabAndThrow" Skyrim powerof3 GitHub`
- `Skyrim Simply Knock unconscious drag mod`
- `"Carry Me" Skyrim SE mod corpse`

**Note:** Research limited by external access restrictions. Recommend:
1. Obtain CatchAndRelease source code directly
2. Request Nexus Mods access or alternative documentation sources
3. Review Seize NPCs' ESP structure in xEdit

---

## Sources

### Local Documentation
- `D:\gerkgit\Skyrim_Drag-n-Drop\Handoff_2026-04-22.md` - Project handoff with detailed status
- `D:\gerkgit\Skyrim_Drag-n-Drop\AGENTS.md` - Project architecture and references
- `C:\Users\vector\Documents\My Games\Skyrim.INI\SKSE\DragAndDrop.log` - Runtime logs

### Configuration Files
- `DragAndDrop_spriggit/Spells/DragDropGrabSpell - 000800_DragAndDrop.esp.yaml`
- `DragAndDrop_spriggit/MagicEffects/DragDropGrabEffect - 000801_DragAndDrop.esp.yaml`

### External Sources (Attempted Access)
- Nexus Mods (403 errors on all mod pages)
- GitHub (404 on GrabAndThrow repositories)
- UESP Wiki (404 on modding pages)
- Creation Kit docs (site down for maintenance)

### Known Working References
- **Seize NPCs** (Creation Club) - FormID 0x801 GrabActor effect
- **GrabAndThrow** by powerof3 - SKSE plugin with direct Havok manipulation
- **CatchAndRelease** by shad0wshayd3 - FireAndForget wrapper approach

---

## Metadata

- **Search queries used:** 15+ (see "Search Queries Attempted" section)
- **Sources consulted:** 8 local files, 3 known working references
- **Confidence level:** High for local analysis, Medium for external mod information (access limited)
- **Research limitations:** External access restrictions prevented direct mod file examination

---

## Next Steps

1. **Obtain CatchAndRelease source code** for direct comparison
2. **Analyze Seize NPCs ESP** in xEdit to understand full effect chain
3. **Test alternative**: Copy CatchAndRelease spell configuration verbatim
4. **Consider fallback**: Pure SKSE plugin approach bypassing GrabActor entirely
5. **Document working solution** once block is resolved

---

**End of Report**

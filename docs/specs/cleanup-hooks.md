# Spec: cleanup-hooks

Scope: repo

# Spec: Cleanup Dead Hook Code

## Goal

Remove all code from the failed CastSpellImmediate hook attempt and INI parsing. Leave working grab infrastructure intact. G-key grab stays in code, gated by a C++ bool `bEnableGKeyGrab`.

## Step-by-Step

### Step 1: Remove Hook Infrastructure (Hooks.cpp)

**Remove entirely:**
- `CastSpellImmediateFn g_originalCastSpellImmediate = nullptr;`
- `CastSpellImmediate_Hook()` function (the entire function)
- `InstallCastSpellHook()` function (the entire function)
- `#include "RE/A/ActorMagicCaster.h"` (only needed for hook)
- All vtable hook logic (the `tryHook` lambda, ActorMagicCaster hooks, NonActorMagicCaster hooks)
- Verbose log lines added during hook debugging (`CastSpell hook: VTABLE=...`, `CastSpell hook installed...`, etc.)

**Keep:**
- `InputEventSink` + `Install()` — these work, no changes needed

### Step 2: Update Hooks.h

**Remove:**
- `void InstallCastSpellHook();`
- `extern CastSpellImmediateFn g_originalCastSpellImmediate;`
- `using CastSpellImmediateFn = ...` typedef

**Keep:**
- `void Install();` — only remaining function

### Step 3: Update main.cpp

**Remove:**
- `Hooks::InstallCastSpellHook();` call at kInputLoaded

**Keep:**
- Everything else (version bump, plugin start, etc.)

### Step 4: Clean DragHandler.cpp

**Remove:**
- `LoadSettings()` function + all INI path code
- `grabFollowers`, `grabChildren`, `grabAnyone` bool members from class — replaced by global reads
- All verbose `TryGrabWithSpell` log lines added during debugging

**Modify `IsValidTarget()`:**
- Read globals via `TESGlobal::GetGlobalValue()` instead of INI bools
- Keep same logic structure: allow if dead/paralyzed, allow if follower+allowFollowers, allow if child+allowChildren, allow if anyone

**Keep unchanged:**
- `TryGrabWithSpell()` function
- G-key `OnKeyDown` / `OnKeyUp` handlers — but add `bEnableGKeyGrab` check before grab attempt
- Spring tuning (damping/elasticity/maxForce) in `UpdateGrabState`
- Velocity zeroing in `UpdateGrabState`
- All other state machine logic

### Step 5: Add `bEnableGKeyGrab` Bool

In `DragHandler.h`:
```cpp
bool bEnableGKeyGrab{ false };  // Set true to re-enable G-key grab
```

In `DragHandler.cpp` `OnKeyDown`:
```cpp
if (bEnableGKeyGrab) {
    TryGrabWithSpell();
}
```

This is a quick compile-time toggle — not persisted. To re-enable G-key grab, set this to `true` and rebuild.

### Step 6: Verify Build

- `cmake --build D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build --config Release`
- Should compile without errors
- Deploy

## Files to Modify

1. `SKSE/src/Hooks.cpp` — remove hook infra, keep input sink
2. `SKSE/src/Hooks.h` — remove hook declarations
3. `SKSE/src/main.cpp` — remove hook install call
4. `SKSE/src/DragHandler.cpp` — remove INI/hook code, modify IsValidTarget
5. `SKSE/src/DragHandler.h` — remove dead INI bools, add bEnableGKeyGrab

## Files NOT Modified

- `SKSE/src/DragHandler.cpp` — `TryGrabWithSpell()` stays
- `SKSE/src/DragHandler.cpp` — drop/throw logic stays
- `SKSE/src/DragHandler.cpp` — spring tuning stays
- `SKSE/src/DragHandler.cpp` — velocity zeroing stays
- `Source/Scripts/DragDrop.psc` — Papyrus unchanged
- `DragAndDrop_spriggit/` — ESP unchanged (globals + conditions come later)

## Verification

After build:
- `TryGrabWithSpell` function still exists but is gated by `bEnableGKeyGrab`
- No CastSpellImmediate hook runs at kInputLoaded
- G-key only handles drop/throw (no grab initiation visible)
- Spell hand cast goes through spell conditions (to be connected via globals later)
- No INI parsing code exists
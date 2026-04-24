# CastSpellImmediate Hook Debug Log

## Problem

Power menu cast of `DragDropGrabSpell` bypasses `IsValidTarget()`. The spell's ESP conditions are a tautology (`GetDead==1 OR GetDead==0`), so they don't enforce `bGrabAnyone`, `bGrabFollowers`, etc. G-key path is gated by `IsValidTarget` before casting. Power menu goes straight to `Actor::CastSpell` without passing through our C++ validation.

## Goal

Make power menu cast also respect `IsValidTarget()` — block the grab if the target fails validation.

## Attempted Fixes

### Attempt 1-2: Hook CastSpellImmediate on MagicCaster vtable

**Hook:** `WriteProcessMemory` + `VirtualProtect` on `VTABLE_MagicCaster[0] + 0x08` (vfunc index 1).

**Result:** Hook installed successfully but never fired. G-key cast uses it; power menu doesn't.

### Attempt 3: Hook ActorMagicCaster vtables

**Hook:** Same technique on `ActorMagicCaster[0]`, `[1]`, `[2]` at vfunc +0x08.

**Result:** Hook fires for G-key cast (`spellID=16000800`). But power menu still doesn't fire this hook.

### Attempt 4: Hook NonActorMagicCaster vtables

**Hook:** Same technique on `NonActorMagicCaster[0]`, `[1]`.

**Result:** Hooks installed. Still no power menu firing.

### Attempt 5: FormID mask fix

**Issue:** Hook was comparing `a_spell->GetFormID() == 0x800` but loaded FormID is `0x16000800` (mod index prefix).

**Fix:** Compare `(a_spell->GetFormID() & 0x00FFFFFF) == 0x800`.

**Result:** G-key path now correctly identifies the spell. Power menu still bypasses all hooks.

## Root Cause

Power menu spell casting does NOT go through `MagicCaster::CastSpellImmediate`. It calls `Actor::CastSpell` directly — a non-virtual member function on the Actor class. There is no central hookable function in that path.

## Vtables Successfully Hooked (v0.1.41-alpha)

| VTable | Address | Installed |
|--------|---------|-----------|
| MagicCaster[0] | 0x7ff604fcddf8 | Yes |
| ActorMagicCaster[0] | 0x7ff604fcb8b0 | Yes |
| ActorMagicCaster[1] | 0x7ff604fcb9a8 | Yes |
| ActorMagicCaster[2] | 0x7ff604fcba50 | Yes |
| NonActorMagicCaster[0] | 0x7ff604fcf358 | Yes |
| NonActorMagicCaster[1] | 0x7ff604fcf378 | Yes |

## Next Steps

### Option A: Hook ActiveEffect::OnUpdate
The GrabActor effect fires on the NPC's active effect list. Hook `ActiveEffect::OnUpdate` (vfunc index 0 or similar) to validate the target when the effect updates. If `IsValidTarget()` fails, dispel the effect.

**Risks:** Effect might already have started before validation can stop it. May cause flicker.

### Option B: Hook ActiveEffect::Start()
Intercept when the effect starts on a target. If our spell and invalid target, don't call original.

**Risks:** Same timing issue — effect might apply before hook can block.

### Option C: Accept power menu = dev mode
Document that power menu cast bypasses `IsValidTarget`. G-key respects settings. Users wanting enforced settings use G-key.

### Option D: Find Actor::CastSpell address
Use RELOCATION_ID or similar to find `Actor::CastSpell` and hook it directly. Requires reverse engineering the function signature.

## Current State

G-key path (v0.1.41-alpha): Hook working, `IsValidTarget` enforced. G-key respects `bGrabAnyone`, `bGrabFollowers`, etc.

Power menu path: Still bypasses `IsValidTarget`. All `CastSpellImmediate` vtables hooked but power menu doesn't use that function.

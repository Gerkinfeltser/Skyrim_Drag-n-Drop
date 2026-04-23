# AGENTS.md - Drag & Drop

Skyrim SE SKSE plugin for grabbing, dragging, and throwing NPCs using Havok physics springs. Self-contained — no external mod dependencies.

## Project Structure

| Directory | Contents |
|-----------|----------|
| `SKSE/src/` | C++ SKSE plugin source (DragHandler.cpp/h, Hooks.cpp/h, main.cpp) |
| `SKSE/Plugins/` | Built DLL + INI config |
| `Source/Scripts/` | Papyrus source (.psc) |
| `scripts/` | Compiled Papyrus (.pex) |
| `DragAndDrop_spriggit/` | ESP source (YAML), rebuild with Spriggit |

## Build Commands

```powershell
# Build SKSE plugin
cmake --build D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build --config Release

# Deploy (symlinked into MO2)
Copy-Item D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build\Release\DragAndDrop.dll D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\Plugins\DragAndDrop.dll -Force

# Rebuild ESP from YAML (requires Spriggit)
powershell -Command "& 'D:\gerkgit\GerkinStuff\bin\spriggit.bat' deserialize -i 'D:\gerkgit\Skyrim_Drag-n-Drop\DragAndDrop_spriggit' -o 'D:\gerkgit\Skyrim_Drag-n-Drop\DragAndDrop.esp'"

# Compile Papyrus
psx compile_psc.bat "Source\Scripts\DragDrop.psc"
```

## How It Works

**LesserPower spell initiates grab, C++ handles everything else.**

1. **LesserPower** casts GrabActor effect — ESP conditions use tautology (`GetDead==1 OR GetDead==0`) so all actors pass
2. **C++ IsValidTarget()** does the real filtering based on INI config
3. **GrabActor** archetype creates Havok mouse spring (engine handles physics attachment)
4. **G key**: Drop NPC — destroys springs, dispels GrabActor, delayed velocity zero on all ragdoll bodies
5. **R key hold**: Charge throw (shows "Ready to throw!" at threshold)
6. **R key release**: If held < dropWindow → drop. If held ≥ dropWindow → throw with ramping force
7. **Throw**: On next frame after spring release, zeros all ragdoll bodies then applies impulse to every body

## INI Config (`SKSE/Plugins/DragAndDrop.ini`)

```ini
[General]
bEnableMod = true
fGrabRange = 150.0
fStaminaDrainRate = 5.0
bGrabFollowers = true
bGrabChildren = false
bGrabAnyone = false

[Throw]
fThrowImpulseMax = 10.0     ; max throw force
fThrowDropWindow = 0.5       ; seconds before charge starts (tap = drop)
fThrowTimeToMax = 4.0        ; seconds from charge start to reach max force
```

## Key Scancodes

```
G = 0x22  (release/drop)
R = 0x13  (hold to charge throw)
```

## Papyrus API

```papyrus
DragDrop.ReleaseNPC()              ; Drop NPC (no impulse)
DragDrop.ThrowNPC(float force)     ; Throw NPC with force
DragDrop.GetGrabbedNPC()           ; Returns current grabbed Actor
DragDrop.IsDragging()              ; Returns bool
```

## Havok Physics Details

- Spring access: `player->GetPlayerRuntimeData().grabSpring` (BSTSmallArray of hkRefPtr<bhkMouseSpringAction>)
- Entity at raw offset +0x30 from `hkpMouseSpringAction` (hkpEntity* m_entity)
- Ragdoll body collection: `BSVisit::TraverseScenegraphCollision` walks all bhkNiCollisionObject → bhkCollisionObject → GetRigidBody → referencedObject (hkpRigidBody)
- Mass: `hkpRigidBody->motion.GetMass()` (typical NPC ~7.0)
- Impulse applied to ALL ragdoll bodies (not just spring body) — prevents constraint damping
- Delayed task via `SKSE::GetTaskInterface()->AddTask()` runs next frame after engine processes spring release

## Log File

```
C:\Users\vector\Documents\My Games\Skyrim.INI\SKSE\DragAndDrop.log
```

## Dependencies

- SKSE64
- Address Library for SKSE Plugins
- CommonLibSSE-NG (build dependency via vcpkg, `x64-windows-static` triplet)

## Build Gotchas

- Warning `overriding '/DENABLE_SKYRIM_VR=1' with '/UENABLE_SKYRIM_VR'` is expected and harmless
- LSP errors about `RE/Skyrim.h` not found are false — headers resolve at build time via CMake
- Papyrus compiler needs `-f='D:\Modlists\ADT\Game Root\Data\Source\Scripts\TESV_Papyrus_Flags.flg'`
- Spriggit is at `D:\gerkgit\GerkinStuff\bin\spriggit.bat`
- Version bump: update VERSION in main.cpp
- `EndGrabObject()` is gated behind `#ifndef ENABLE_SKYRIM_VR` — use `DestroyMouseSprings()` instead

## Key References

- GrabAndThrow by powerof3: Havok spring access, throw impulse pattern, player->grabSpring
- CatchAndRelease: GrabActor spell config (FireAndForget + Duration 999999)

## Not Yet Implemented

- **G-key grab jitter — VMT hook tried (attempt #14, FAILED).** Hook fires and fixes near-zero mousePos, but Havok reads the bad value before fix applies. Effect's alternating write is internal — same effect instance alternates correct/near-zero every Update call. **Option B (manual spring creation) is the recommended path forward.**
- Stamina drain while dragging (stub exists, not wired to frame tick)
- Force ragdoll on stiff/standing dead NPCs (ForceRagdoll exists but causes issues when called at grab time)
- Throw damage/stagger to other NPCs on hit
- Knockout mod reload fix (stashed in Skyrim_KnockoutPatched)

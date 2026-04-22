# AGENTS.md - Drag & Drop

Skyrim SE mod for dragging unconscious NPCs and followers using Havok physics springs.

## Project Structure

| Directory | Contents |
|-----------|----------|
| `SKSE/src/` | C++ SKSE plugin source |
| `SKSE/Plugins/` | Built DLL + INI config |
| `Source/Scripts/` | Papyrus source (.psc) |
| `scripts/` | Compiled Papyrus (.pex) |
| `DragAndDrop_spriggit/` | ESP source (YAML) |

## Build Commands

```powershell
# Build SKSE plugin
cmake --build D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build --config Release

# Deploy (symlinked into MO2)
Copy-Item D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\build\Release\DragAndDrop.dll D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\Plugins\DragAndDrop.dll -Force

# Rebuild ESP from YAML (requires Spriggit)
D:\gerkgit\GerkinStuff\bin\spriggit.bat deserialize -i DragAndDrop_spriggit -o DragAndDrop.esp

# Compile Papyrus
.\compile_psc.bat "Source\Scripts\DragDrop.psc"
.\compile_psc.bat "Source\Scripts\DragDropQuest.psc"
```

## How It Works

**Spell initiates grab, C++ handles everything else.**

- **LesserPower** (self-contained, no Seize NPCs dependency) casts GrabActor effect on valid NPC
- **GrabActor** archetype creates Havok mouse spring (engine handles physics attachment)
- **G key**: Drop NPC (DestroyMouseSprings, no impulse)
- **R key**: Hold to charge throw, release to throw (ApplyLinearImpulse via camera forward)
- Valid targets: dead, paralyzed/unconscious, or followers within range
- Stamina drains while dragging, auto-releases at 0

## Papyrus API

```papyrus
DragDrop.ReleaseNPC()              ; Drop NPC (no impulse)
DragDrop.ThrowNPC(float force)     ; Throw NPC with force
DragDrop.GetGrabbedNPC()           ; Returns current grabbed Actor
DragDrop.IsDragging()              ; Returns bool
```

## Key Scancodes

```
G = 0x22  (release/drop)
R = 0x13  (hold to charge throw)
```

## Havok Scale Constants

```cpp
BS_TO_HK_SCALE = 0.0142875f
HK_TO_BS_SCALE = 69.991251f
```

## Log File

```
C:\Users\vector\Documents\My Games\Skyrim.INI\SKSE\DragAndDrop.log
```

## Key References

- GrabAndThrow by powerof3: Havok spring access, throw impulse pattern, player->grabSpring
- CatchAndRelease: GrabActor spell config (FireAndForget + Duration 999999)

## Dependencies

- SKSE64
- Address Library for SKSE Plugins
- CommonLibSSE-NG (build dependency via vcpkg)

## Build Gotchas

- vcpkg must use `x64-windows-static` triplet
- Papyrus compiler needs `-f='D:\Modlists\ADT\Game Root\Data\Source\Scripts\TESV_Papyrus_Flags.flg'`
- Spriggit is at `D:\gerkgit\GerkinStuff\bin\spriggit.bat`
- Version bump: update VERSION in main.cpp

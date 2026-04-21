# AGENTS.md - Drag & Drop

Skyrim SE mod for dragging unconscious NPCs and followers using Havok physics springs.

## Project Structure

| Directory | Contents |
|-----------|----------|
| `SKSE/src/` | C++ SKSE plugin source |
| `SKSE/Plugins/` | Built DLL + INI config |
| `Source/Scripts/` | Papyrus source (.psc) |
| `scripts/` | Compiled Papyrus (.pex) |

## Build Commands

```powershell
# Build SKSE plugin
cd SKSE
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE='C:\tools\vcpkg\scripts\buildsystems\vcpkg.cmake'
cmake --build build --config Release

# Compile Papyrus
.\compile_psc.bat "Source\Scripts\DragDrop.psc"
.\compile_psc.bat "Source\Scripts\DragDropQuest.psc"
```

## How It Works

**NOT using GrabActor spell archetype** — uses direct Havok physics springs via C++ SKSE plugin.

- **E key**: Grab nearest valid NPC (hold) / Drop (release)
- **R key**: While dragging, hold R then release to throw NPC (force scales with hold duration)
- Valid targets: dead, paralyzed/unconscious, or followers within range
- Stamina drains while dragging, auto-releases at 0

## Papyrus API

```papyrus
DragDrop.GrabNPC(Actor target)    ; Attach physics spring to NPC
DragDrop.ReleaseNPC()              ; Destroy spring, NPC drops
DragDrop.ThrowNPC(float force)     ; Destroy spring + apply impulse
DragDrop.GetGrabbedNPC()           ; Returns current grabbed Actor
DragDrop.IsDragging()              ; Returns bool
```

## Key References

- GrabAndThrow by powerof3: Hooks pattern, physics springs, DestroyMouseSprings()
- CatchAndRelease (previous attempt): What NOT to do (GrabActor archetype)

## Dependencies

- SKSE64
- Address Library for SKSE Plugins
- CommonLibSSE-NG (build dependency via vcpkg)

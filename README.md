# Drag & Drop

A Skyrim SE mod that lets you grab, drag, and throw NPCs using Havok physics. Self-contained — no other mods required (but pairs well with [Knockout and Surrender](https://www.nexusmods.com/skyrimspecialedition/mods/40556))

## Requirements

- Skyrim Special Edition (tested on 1.6.1170 AE)
- SKSE64
- Address Library for SKSE Plugins

## Install

Drop the contents of the zip into your `Data` folder, or install via MO2/Vortex. That's it.

## Uninstall

Remove whenever you like. The mod is save-game safe — no quests, aliases, or permanent cell edits. The grab spell stays in your spell list after removal but does nothing. If removed mid-drag, your speed may not restore (fix with `player.setav speedmult 100` in console).

## How to Use

The mod adds a Lesser Power to your character. It works automatically — no need to equip or activate anything from the power menu.

### Controls (G key by default)

**To grab:**
- Look at an NPC and hold **G** — the grab starts on keydown

**To drop:**
- While dragging, **tap G** — the NPC drops with momentum from your camera swing

**To throw:**
- While dragging, **hold G** — a throw charges up (you'll see a notification)
- **Release G** — the NPC launches with ramping force. Longer hold = bigger throw

**Grab-hold-drop:**
- Hold G on an NPC to grab
- Release G quickly (within the hold timeout) — NPC stays grabbed
- Release G slowly (after the timeout) — NPC drops with momentum automatically

### What Can You Grab?

By default you can grab:
- **Dead NPCs** — always
- **Paralyzed NPCs** — always
- **Followers** — enabled by default
- **Hostile NPCs** — disabled by default, see INI

You cannot grab ghosts, children (by default), or NPCs with paralysis immunity keywords.

### Swing Impact

While dragging, the NPC acts as a battering ram. Swing them into nearby actors and they'll get knocked back. Clutter and dynamic objects also get pushed around.

### Throw Impact

After throwing, the NPC is tracked for a few seconds. Any actors it passes near get knocked back. Damage can also be configured in the INI.

## INI Settings

All settings are in `SKSE/Plugins/DragAndDrop.ini`. Edit with any text editor. Changes require a game restart.

**Important:** Do not add inline comments with semicolons — they break the parser. Keep values clean:

```ini
bEnableMod = true
```

### Quick Reference

| Setting | Default | What it does |
|---------|---------|--------------|
| `bEnableMod` | `true` | Master on/off switch |
| `fGrabRange` | `150.0` | Max distance to grab an NPC |
| `bGrabAnyone` | `false` | Grab any NPC (overrides other filters) |
| `bGrabFollowers` | `true` | Allow grabbing followers |
| `bGrabChildren` | `false` | Allow grabbing children |
| `bGrabHostile` | `false` | Allow grabbing hostile NPCs |
| `iActionKey` | `34` | Key scancode (34 = G, 19 = R) |
| `fGrabHoldTimeout` | `0.5` | Seconds before hold-release drops |
| `bDropOnPlayerHit` | `true` | Auto-drop if you take damage |
| `bNoSprintWhileDragging` | `true` | Prevent sprinting while dragging |
| `bShowNotifications` | `true` | Show on-screen messages |
| `bEnableLogging` | `false` | Enable detailed log file |

### Throw Settings `[Throw]`

| Setting | Default | What it does |
|---------|---------|--------------|
| `fThrowImpulseMax` | `20.0` | Max throw force |
| `fThrowDropWindow` | `0.2` | Grace period before charging starts |
| `fThrowTimeToMax` | `3.0` | Seconds to reach max force |

### Impact Settings `[Impact]`

| Setting | Default | What it does |
|---------|---------|--------------|
| `fImpactRadius` | `120` | How close an actor must be to get hit |
| `fImpactDuration` | `3.0` | How long impact tracking lasts after throw |
| `fImpactForce` | `300.0` | Knockback force on ragdolled targets |
| `fImpactDamage` | `0.0` | Damage dealt on impact (0 = none) |
| `bImpactOnDrop` | `false` | Whether drops also cause impact knockback |
| `fSwingImpactRadiusMult` | `0.6` | Swing radius multiplier (smaller than throw) |
| `fSwingImpactCooldown` | `0.5` | Seconds between hits on same target |
| `bSwingImpactStatics` | `true` | Push clutter/objects during swing |

### Sound Settings `[Sound]`

| Setting | Default | What it does |
|---------|---------|--------------|
| `iGrabFailSound` | `0` | Sound when grab fails (hex FormID from Skyrim.esm, 0 = off) |
| `iGrabSound` | `0` | Sound on successful grab |
| `iDropSound` | `0` | Sound on drop |
| `iThrowSound` | `0` | Sound on throw |

Sound values are hex FormIDs of sound descriptors from Skyrim.esm (e.g., `0x3D0D3`). Look them up in xEdit or the Creation Kit. Set to `0` to disable.

### Physics Tweaks `[General]`

| Setting | Default | What it does |
|---------|---------|--------------|
| `fSpringDamping` | `1.5` | Spring damping (higher = sluggish) |
| `fSpringElasticity` | `0.05` | Spring stiffness (higher = snappier) |
| `fSpringMaxForce` | `1000.0` | Max spring pull force |
| `fDragMaxVelocity` | `5.0` | Ragdoll speed cap during drag |
| `fGrabTetherDist` | `600.0` | Auto-drop if NPC drifts this far |

## Known Issues

- **Power menu bypasses filters.** Casting the grab spell from the power menu ignores target restrictions (dev/debug mode).

## Early Access

This mod is in early development. Back up your saves. While the mod is designed to be save-safe and shouldn't cause issues, removing it mid-drag could leave your speed altered (see Uninstall above).

## Credits

- [GrabAndThrow](https://www.nexusmods.com/skyrimspecialedition/mods/120460) by powerof3 — Havok spring access and throw impulse patterns
- [Seize NPCs](https://www.nexusmods.com/skyrimspecialedition/mods/135703) — Mod inspiration & reference

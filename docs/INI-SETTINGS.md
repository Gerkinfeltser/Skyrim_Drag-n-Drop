# Drag & Drop — INI Settings Reference

All settings live in `SKSE/Plugins/DragAndDrop.ini`. Loaded at startup via Win32 `GetPrivateProfileString` using the DLL's own module path — works correctly under MO2.

**IMPORTANT:** Do NOT add inline comments with semicolons or heavy padding. `GetPrivateProfileString` returns the full value string including trailing text, which breaks bool/float parsing. Keep values clean: `bEnableMod = true`

---

## [General]

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `bEnableMod` | bool | `true` | Master enable toggle. When `false`, the entire mod is disabled — no grab, no throw, no impact. |
| `fGrabRange` | float | `150.0` | Maximum distance (in game units) from the player to initiate a grab via the G-key. Crosshair target must be within this range. |
| `bGrabAnyone` | bool | `false` | Allow grabbing **any** NPC regardless of state. Overrides `bGrabFollowers`, `bGrabChildren`, and `bGrabHostile`. Dead and paralyzed actors are always grabbable regardless. |
| `bGrabFollowers` | bool | `true` | Allow grabbing player teammates (followers, hirelings, etc.). |
| `bGrabChildren` | bool | `false` | Allow grabbing child actors. |
| `bGrabHostile` | bool | `false` | Allow grabbing actors hostile to the player. Respects follower/child overrides — followers and children can still be grabbed even if not hostile. |
| `fStaminaDrainRate` | float | `5.0` | Stamina drain per second while dragging. **Stub — not yet wired to the frame tick.** Currently has no effect. |
| `fDragSpeedMult` | float | `1.0` | Player speed multiplier while dragging. Applied to `SpeedMult` actor value on grab start, restored on release. Set to `1.0` for no change, `2.0` to move twice as fast, etc. |
| `bNoSpeedPenalty` | bool | `true` | When `true`, prevents the engine's default drag speed penalty. The engine normally slows the player while grabbing objects — this overrides it. |
| `iActionKey` | int | `34` | DI scancode for the action key. `34` = G key, `19` = R key. This is the key used for grab/drop/throw. Use a DirectInput scancode, not a virtual key code. |
| `bUseShoutKeyForRelease` | bool | `false` | When `true`, the Shout/Power key (left mouse by default) is used for release/throw instead of (or in addition to) the action key. |
| `bDropOnPlayerHit` | bool | `true` | Auto-drop the grabbed NPC if the player takes damage while dragging. Captures spring velocity immediately, defers spring destruction to next frame to avoid CTD. |
| `bNoSprintWhileDragging` | bool | `true` | When `true`, drains player stamina to 0 each frame while dragging. The game's sprint system requires stamina, so this naturally prevents sprinting. |
| `bShowNotifications` | bool | `true` | Show debug notifications ("Ready to throw!", "Dropped", "Threw!", etc.). Set to `false` to suppress all notifications. |
| `fGrabHoldTimeout` | float | `0.5` | Seconds to hold G on initial grab before releasing triggers a drop. If you release G after this timeout, the NPC drops with momentum. Release before timeout = NPC stays grabbed (tap-grab). |
| `bBlockTwoHanded` | bool | `true` | Prevents grab when wielding two-handed weapons or bows while weapons are unsheathed. Checks both left and right hand for weapon types: TwoHandSword, TwoHandAxe, Bow, Crossbow. |
| `bBlockUnsheathed` | bool | `false` | Prevents grab when **any** weapon is drawn. Stronger restriction than `bBlockTwoHanded` — blocks all weapon types, not just two-handed. |
| `bEnableLogging` | bool | `false` | Enable detailed log output to `DragAndDrop.log`. When `false`, only warnings and errors are logged. When `true`, all info-level messages are logged including frame-by-frame physics data. |
| `fSpringDamping` | float | `1.5` | Havok mouse spring damping. Higher values = spring resists motion more (sluggish feel). Lower values = spring is more responsive but can oscillate. |
| `fSpringElasticity` | float | `0.05` | Havok mouse spring elasticity (stiffness). Higher values = spring snaps harder to the hold point. Very high values cause jitter. |
| `fSpringMaxForce` | float | `1000.0` | Maximum force the Havok mouse spring can exert. Higher values = spring can pull harder against physics forces (combat impacts, gravity). If NPCs escape during combat, try increasing this. |
| `fDragMaxVelocity` | float | `5.0` | Maximum velocity (in Havok units) for ragdoll bodies **during drag**. Every frame, all ragdoll body velocities except the spring body are clamped to this value. Prevents moon-gravity flings from fast camera swings. The spring body is excluded so it retains real velocity for drop momentum. Set to `0` to disable clamping. |
| `fGrabTetherDist` | float | `600.0` | Maximum distance (in game units) the grabbed NPC can drift from the player before auto-dropping. Safety net — if the ragdoll gets knocked far away (combat, physics glitches), it drops cleanly instead of stretching the spring infinitely. |

---

## [Throw]

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `fThrowImpulseMax` | float | `20.0` | Maximum throw force. When the action key is held for `fThrowTimeToMax` seconds, this is the force applied. Force ramps linearly from 0 to this value over the charge duration. |
| `fThrowDropWindow` | float | `0.2` | Time in seconds before throw charging begins. If the action key is released within this window, it counts as a **drop** (no throw impulse). If held longer, charging begins. A "Ready to throw!" notification appears when the window expires. |
| `fThrowTimeToMax` | float | `3.0` | Time in seconds from when charging starts (after `fThrowDropWindow`) to reach maximum throw force. Holding longer than this doesn't increase force further. |

---

## [Sound]

All sound values are **hex FormIDs** of sound descriptors from Skyrim.esm (e.g., `0x3D0D3`). Look them up in xEdit or the Creation Kit. Set to `0` to disable. Uses `BSSoundHandle` with `SetObjectToFollow` at the player's 3D node.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `iGrabFailSound` | hex | `0x0` | Sound played when grab fails (target exists but fails `IsValidTarget`). |
| `iGrabSound` | hex | `0x0` | Sound played on successful grab. |
| `iDropSound` | hex | `0x0` | Sound played when NPC is dropped (tap release or ReleaseNPC). |
| `iThrowSound` | hex | `0x0` | Sound played when NPC is thrown (charged release or ThrowNPC). |

---

## [Impact]

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `fImpactRadius` | float | `120` | Proximity detection radius (in game units) for impact knockback. When a thrown/dropped NPC passes within this 2D distance of another actor, knockback is triggered. Uses X/Y distance only (ignores height difference). |
| `fImpactDuration` | float | `3.0` | Maximum time in seconds to track a thrown/dropped NPC for impacts. After this duration, tracking stops regardless of NPC speed. |
| `fImpactMinVelocity` | float | `0.5` | Minimum ragdoll body speed (Havok units) to continue impact tracking. When the thrown NPC's average body speed drops below this, tracking stops. Also used as the minimum spring speed threshold for swing impact during drag. |
| `fImpactForce` | float | `300.0` | Base Havok impulse magnitude applied to **ragdolled** target actors on impact. This is per-body, scaled by body mass. For standing actors, `PushActorAway` is used instead (force hardcoded at 5.0 in Papyrus script). |
| `fImpactPushForceMax` | float | `5.0` | **Reserved** — intended to configure the `PushActorAway` force for standing actors. Currently the Papyrus script (`DragDropImpactScript.psc`) has this hardcoded at 5.0. This INI value is loaded but not yet passed to the script. |
| `fImpactDamage` | float | `0.0` | Base damage dealt to actors hit by a thrown/swung NPC. Set to `0` to disable damage. Damage is scaled by speed (see `fImpactDamageSpeedScale`). |
| `fImpactDamageThrownMult` | float | `1.0` | Damage multiplier applied to the **thrown NPC itself** on impact. The thrown NPC takes `fImpactDamage * fImpactDamageThrownMult * speedScale` self-damage when it hits someone. Set to `0` to make the thrown NPC take no self-damage. |
| `bImpactOnDrop` | bool | `false` | Enable impact tracking on drops (tap release). When `false`, only throws trigger impact tracking. When `true`, even simple drops will knock back actors the NPC passes near. |
| `fSwingImpactRadiusMult` | float | `0.6` | Multiplier for the swing impact radius during drag. Actual swing radius = `fImpactRadius * this value`. Separate from throw impact radius because the NPC is closer to the player during drag. |
| `fSwingImpactCooldown` | float | `0.5` | Seconds between swing impact hits on the same target. Prevents rapid-fire damage/impulse from a single swing. Each target has its own cooldown timer. |
| `bSwingImpactStatics` | bool | `true` | Apply impulse to dynamic static objects (baskets, clutter, pots) during swing. Only affects bodies with `motion.type == kDynamic`. Set to `false` to only affect actors. |
| `fRagdollMaxVelocity` | float | `5.0` | Maximum velocity (Havok units) for impact impulse clamping via `ApplyClampedImpulse`. After applying an impulse to a ragdoll body (from throw/swing impact), the body's velocity is clamped to this value. Prevents impact victims from getting launched into orbit. |
| `fImpactForceSpeedScale` | float | `1.0` | Force scales with speed: `baseForce * (speed * this value)`. Higher values = faster throws/swings produce dramatically more knockback force on ragdoll targets. |
| `fImpactDamageSpeedScale` | float | `1.0` | Damage scales with speed: `baseDamage * (1.0 + speed * this value)`. Higher values = faster throws/swings produce more damage. The `1.0` base ensures even slow impacts deal minimum damage. |

---

## Notes

- All float values use standard decimal notation (e.g., `5.0`, `0.05`, `300.0`)
- Boolean values accept `true`/`false` or `1`/`0`
- Integer values accept decimal or hex with `0x` prefix (e.g., `34` or `0x22` for G key)
- Sound FormIDs should use hex notation (e.g., `0x3D0D3`) — look up in xEdit or Creation Kit
- The INI file is loaded once at startup from the same directory as the DLL. Changes require a game restart.
- Default values shown above are the code defaults — your INI file may have different values if you've customized them.
- **Do NOT add inline comments with semicolons** — `GetPrivateProfileString` returns the full value string including trailing text, which breaks bool/float parsing.

# Research Report: GrabAndThrow by powerof3

**Date:** 2025-01-22
**Researcher:** Researcher
**Status:** Complete

## Executive Summary

GrabAndThrow by powerof3 is a Skyrim SE SKSE plugin that implements object grabbing and throwing using **native Havok mouse springs** via C++ hooks. It **bypasses the GrabActor spell archetype entirely** and manipulates the player's `grabSpring` system directly through physics engine calls.

**Key Finding:** This mod does NOT use GrabActor spell archetype. It leverages the vanilla mouse spring system that the E key uses for drag physics, then destroys springs and applies custom impulse for throwing.

---

## Key Findings

### 1. GrabActor Spell Archetype: NOT USED

**Finding:** GrabAndThrow completely bypasses the GrabActor spell archetype system.

**Evidence from source code:**
- No references to `RE::EffectArchetype::kGrabActor`
- No spell casting or magic effect dispersion
- Works directly with `player->grabSpring` array (native Havok system)
- Uses `player->DestroyMouseSprings()` to release objects

**Why this matters:** The GrabActor archetype (used by vanilla E key drag) has hardcoded limitations for NPCs. GrabAndThrow bypasses these by accessing the underlying Havok physics springs directly.

---

### 2. Core Technical Architecture

#### A. Physics System: Mouse Springs

**Key classes:**
```cpp
RE::PlayerCharacter::grabSpring  // Array of mouse spring actions
RE::hkpMouseSpringAction         // Havok mouse spring action
RE::hkpRigidBody                 // Havok rigid body (physics object)
```

**How grab works:**
1. Player uses vanilla E key to grab object (engine's normal grab system)
2. `ReadyWeaponHandler::ProcessButton` hook detects when grab key is released
3. If player is in normal grab mode, hook suppresses default behavior and calls custom throw logic

**Code reference (Hooks.cpp:27-41):**
```cpp
struct ProcessButton
{
    static void thunk(RE::ReadyWeaponHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
    {
        auto player = RE::PlayerCharacter::GetSingleton();

        if (player->grabType == RE::PlayerCharacter::GrabbingType::kNormal) {
            if (a_event->IsUp()) {
                GrabThrowHandler::GetSingleton()->ThrowGrabbedObject(player, a_event->HeldDuration());
                player->DestroyMouseSprings();
            }
            return;
        }
        return func(a_this, a_event, a_data);
    }
}
```

#### B. Throw Mechanics: Custom Impulse Application

**Throw process (GrabThrowHandler.cpp:237-272):**

1. **Calculate force based on hold duration:**
   ```cpp
   float force = (heldDuration * strengthMult) + impulseBase;
   force = min(force, impulseMax);
   ```

2. **Apply fake mass for damage calculations:**
   ```cpp
   auto mass = hkpRigidBody->motion.GetMass();
   if (mass < fPhysicsDamage1Mass) {
       RE::TESHavokUtilities::PushTemporaryMass(bhkRigidBody, fPhysicsDamage1Mass);
       mass = fPhysicsDamage1Mass;  // Boosts damage calc
   }
   ```

3. **Mark object as "thrown" using Havok property:**
   ```cpp
   constexpr std::uint32_t HK_PROPERTY_GRABTHROWNOBJECT{ 628318 };
   hkpRigidBody->SetProperty(HK_PROPERTY_GRABTHROWNOBJECT, heldDuration);
   hkpRigidBody->AddContactListener(this);  // For collision callbacks
   ```

4. **Calculate and apply impulse in camera direction:**
   ```cpp
   RE::hkVector4 impulse = GetImpulse(force, mass);
   hkpRigidBody->SetLinearVelocity(RE::hkVector4());      // Zero velocity
   hkpRigidBody->SetAngularVelocity(RE::hkVector4());     // Zero angular velocity
   hkpRigidBody->ApplyLinearImpulse(impulse);             // Apply throw force
   ```

5. **Destroy mouse spring to release object:**
   ```cpp
   player->DestroyMouseSprings();
   ```

**Impulse calculation (GrabThrowHandler.cpp:218-226):**
```cpp
RE::hkVector4 GetImpulse(float a_force, float a_mass) const
{
    RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
    float x = (matrix.entry[0][1] * a_force) * BS_TO_HK_SCALE;
    float y = (matrix.entry[1][1] * a_force) * BS_TO_HK_SCALE;
    float z = (matrix.entry[2][1] * a_force) * BS_TO_HK_SCALE;

    RE::hkVector4 velocity(x, y, z, 0);
    RE::hkVector4 impulse = velocity * a_mass;
    return impulse;
}
```

---

### 3. C++ Hooks Used

GrabAndThrow installs **5 hooks** to intercept game behavior:

#### Hook 1: `ReadyWeaponHandler::ProcessButton` (vfunc index 4)
**Purpose:** Intercept E key release during grab
**Location:** Virtual function table hook
**Function:** Detects when player releases grab key, triggers throw instead of default drop

#### Hook 2: `AttackBlockHandler::ProcessInput` (vfunc index 1)
**Purpose:** Suppress attack/block input while grabbing
**Function:** Returns `false` when player is in normal grab mode, preventing combat actions

#### Hook 3: `HitData::InitializeImpactData` (RELOCATION_ID 42836/44005 + 0x50)
**Purpose:** Modify damage calculations for thrown objects
**Function:**
- Checks if object has `HK_PROPERTY_GRABTHROWNOBJECT`
- Calculates damage based on mass + velocity
- Applies damage multiplier from config

**Code (Hooks.cpp:77-96):**
```cpp
struct InitializeImpactData
{
    static void thunk(RE::HitData* a_hitData, std::uint64_t a_unk02, RE::TESObjectREFR* a_ref,
                      float a_damageFromImpact, RE::DamageImpactData* a_impactDamageData)
    {
        float damageFromImpact = a_damageFromImpact;
        RE::bhkRigidBody* body = a_impactDamageData->body.get();
        bool isThrownObject = GrabThrowHandler::HasThrownObject(body);

        if (body && isThrownObject) {
            if (damageFromImpact == 0.0f) {
                auto hkpBody = body->GetRigidBody();
                auto mass = hkpBody->motion.GetMass();
                auto speed = a_impactDamageData->velocity.Length();
                damageFromImpact = GrabThrowHandler::GetSingleton()->GetFinalDamageForImpact(mass, speed);
            } else {
                damageFromImpact = GrabThrowHandler::GetSingleton()->GetFinalDamageForImpact(damageFromImpact);
            }
            damageFromImpact *= GrabThrowHandler::GetThrownObjectValue(body);
        }

        func(a_hitData, a_unk02, a_ref, damageFromImpact, a_impactDamageData);

        if (isThrownObject) {
            a_hitData->aggressor = RE::PlayerCharacter::GetSingleton()->GetHandle();
        }
    }
}
```

#### Hook 4: `IsTelekinesisObject` call in `HitData::InitializeImpactData`
**Purpose:** Override telekinesis damage flag
**Function:** Returns `false` for thrown objects to prevent telekinesis damage logic

#### Hook 5: `FOCollisionListener::ReferenceDeactivated` (RELOCATION_ID 25327/25850 + 0xE4/0xF4)
**Purpose:** Cleanup thrown object property when reference is unloaded
**Function:** Removes `HK_PROPERTY_GRABTHROWNOBJECT` from Havok rigid body

---

### 4. Havok Contact Listener System

GrabAndThrow implements **`RE::hkpContactListener`** to detect collisions of thrown objects:

**Class declaration (GrabThrowHandler.h:6):**
```cpp
class GrabThrowHandler :
    public REX::Singleton<GrabThrowHandler>,
    public RE::hkpContactListener
```

**Collision callback (GrabThrowHandler.cpp:149-235):**

When a thrown object hits something:
1. **Check collision validity:** Ensure contact point exists and isn't disabled
2. **Identify bodies:** Distinguish thrown object from target
3. **Actor collision:** Process hurtful body damage through character controller
4. **Object collision:**
   - Send detection events (for stealth distraction)
   - Send hit events to Papyrus
   - Apply destructible object damage (optional feature)
5. **Sound level:** Calculate based on mass (Quiet → Very Loud)

**Detection event code (GrabThrowHandler.cpp:195-209):**
```cpp
if (sendDetectionEvents) {
    const auto& contactPos = a_event.contactPoint->position;
    RE::NiPoint3 position = RE::NiPoint3(
        contactPos.quad.m128_f32[0],
        contactPos.quad.m128_f32[1],
        contactPos.quad.m128_f32[2]) * HK_TO_BS_SCALE;

    if (auto currentProcess = RE::PlayerCharacter::GetSingleton()->currentProcess) {
        SKSE::GetTaskInterface()->AddTask([position, thrownObjectMass, thrownObject]() {
            auto player = RE::PlayerCharacter::GetSingleton();
            auto soundLevel = GrabThrowHandler::GetSingleton()->GetSoundLevel(thrownObjectMass);
            auto soundLevelValue = RE::AIFormulas::GetSoundLevelValue(soundLevel);

            player->currentProcess->SetActorsDetectionEvent(player, position, soundLevelValue, thrownObject.get());
        });
    }
}
```

---

### 5. Game Settings Modified

GrabAndThrow modifies vanilla GMST settings to enhance grabbing behavior:

**Modified settings (GrabThrowHandler.cpp:44-52):**
```cpp
set_gmst("fZKeyMaxForce", fZKeyMaxForce);                  // Max grab force
set_gmst("fZKeyMaxForceWeightHigh", fZKeyMaxForceWeightHigh);
set_gmst("fZKeyMaxForceWeightLow", fZKeyMaxForceWeightLow);
set_gmst("fZKeyMaxContactDistance", fZKeyMaxContactDistance);  // Spring break distance
set_gmst("fZKeyObjectDamping", fZKeyObjectDamping);
set_gmst("fZKeySpringDamping", fZKeySpringDamping);
set_gmst("fZKeySpringElasticity", fZKeySpringElasticity);
set_gmst("fZKeyHeavyWeight", fZKeyHeavyWeight);
set_gmst("fZKeyComplexHelperWeightMax", fZKeyComplexHelperWeightMax);
```

These control the physics properties of the mouse spring connection between player and grabbed object.

---

## Comparison: GrabAndThrow vs Drag & Drop

### Current Drag & Drop Implementation

**Current approach (from your codebase):**
- Uses `RE::PlayerCharacter::IsGrabbing()` to detect when player grabs NPC
- Uses `DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor)` to release
- Uses `DestroyMouseSprings()` to destroy physics connection
- No custom impulse application for throwing
- No collision detection or damage system

**Problem:** Your current code assumes the GrabActor spell archetype is being used (lines 152-156 in DragHandler.cpp). But GrabAndThrow shows this is unnecessary.

### Key Differences

| Aspect | GrabAndThrow | Drag & Drop (current) |
|--------|--------------|----------------------|
| Grab mechanism | Vanilla E key (engine) | Vanilla E key (engine) |
| Grab detection | `player->grabType == kNormal` | `player->IsGrabbing()` |
| Release method | `DestroyMouseSprings()` | `DestroyMouseSprings()` + Dispel GrabActor |
| Throw impulse | Custom `ApplyLinearImpulse()` | Placeholder only |
| Collision tracking | `hkpContactListener` | None |
| Damage system | Mass/velocity-based + contact events | None |
| GrabActor archetype | **Not used** | Used (unnecessarily) |

---

## Recommendations for Drag & Drop

### 1. Remove GrabActor Archetype Dependencies

**Change DragHandler.cpp:152-156:**
```cpp
// REMOVE THESE LINES (unnecessary):
auto magicTarget = player->AsMagicTarget();
if (magicTarget) {
    magicTarget->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
}
```

**Rationale:** GrabAndThrow proves the grab system works purely through Havok mouse springs. The GrabActor archetype is NOT involved in the physics connection.

### 2. Implement Direct Impulse Application

**Add to DragHandler.cpp:**
```cpp
void DragHandler::ApplyThrowImpulse(float a_force)
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || !grabbedActor) return;

    auto cell = player->GetParentCell();
    auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
    if (!bhkWorld) return;

    RE::BSWriteLockGuard locker(bhkWorld->worldLock);

    for (const auto& mouseSpring : player->grabSpring) {
        if (mouseSpring && mouseSpring->referencedObject) {
            if (auto hkMouseSpring = skyrim_cast<RE::hkpMouseSpringAction*>(mouseSpring->referencedObject.get())) {
                if (auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(hkMouseSpring->entity)) {
                    auto bhkRigidBody = reinterpret_cast<RE::bhkRigidBody*>(hkpRigidBody->userData);

                    // Calculate impulse in camera direction
                    RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
                    float force = a_force * 0.0142875f;  // BS_TO_HK_SCALE
                    float x = matrix.entry[0][1] * force;
                    float y = matrix.entry[1][1] * force;
                    float z = matrix.entry[2][1] * force;

                    RE::hkVector4 velocity(x, y, z, 0);
                    auto mass = hkpRigidBody->motion.GetMass();
                    RE::hkVector4 impulse = velocity * mass;

                    hkpRigidBody->SetLinearVelocity(RE::hkVector4());
                    hkpRigidBody->SetAngularVelocity(RE::hkVector4());
                    hkpRigidBody->ApplyLinearImpulse(impulse);
                }
            }
        }
    }
}
```

### 3. Implement Contact Listener (Optional)

For collision detection and stealth mechanics:

```cpp
class DragHandler : public RE::hkpContactListener
{
    // ... existing code ...

    void ContactPointCallback(const RE::hkpContactPointEvent& a_event) override;
};
```

This would enable:
- Detection events when thrown NPC hits objects
- Sound level calculation for stealth
- Damage on impact (if desired)

### 4. Add Input Hook for Throw Button

GrabAndThrow uses `ReadyWeaponHandler::ProcessButton` hook. For your R key throw:

**Alternative approach:** Keep your current `InputEventSink` in Hooks.cpp, but add throw charging logic:

```cpp
if (a_key == KEY_R_KEYBOARD && state == State::Dragging) {
    throwHoldTime += dt;
    if (btn->IsUp()) {
        float force = CalculateThrowForce(throwHoldTime);
        ReleaseNPC(true, force);
        throwHoldTime = 0.0f;
    }
}
```

---

## Technical Summary

### What GrabAndThrow Does NOT Use:
- ❌ GrabActor spell archetype
- ❌ Magic effects
- ❌ Spell dispersion
- ❌ `player->GrabObject()` method
- ❌ Telekinesis system

### What GrabAndThrow DOES Use:
- ✅ `player->grabSpring` array (native mouse springs)
- ✅ `hkpMouseSpringAction` (Havok physics springs)
- ✅ `player->DestroyMouseSprings()` (release mechanism)
- ✅ `hkpRigidBody->ApplyLinearImpulse()` (throw physics)
- ✅ `hkpContactListener` (collision callbacks)
- ✅ `hkpRigidBody->SetProperty()` (mark thrown objects)
- ✅ `RE::TESHavokUtilities::PushTemporaryMass()` (fake mass for damage)

### Architecture Pattern:
```
Vanilla E Key → Mouse Spring Created → Hook Detects Release → Apply Impulse → Destroy Spring
```

---

## Sources

1. [GrabAndThrow GitHub Repository](https://github.com/powerof3/GrabAndThrow) - Full source code
   - `src/GrabThrowHandler.h` - Core handler class with contact listener
   - `src/GrabThrowHandler.cpp` - Implementation of throw physics and collision
   - `src/Hooks.h` - Hook declarations
   - `src/Hooks.cpp` - All 5 hook implementations
   - `src/main.cpp` - Plugin initialization and SKSE registration

2. [Grab and throw create distraction - r/skyrimvr](https://www.reddit.com/r/skyrimvr/comments/1d5ip78/grab_and_throw_create_distraction/) - Community discussion (Jan 2024)

3. [HIGGS Update - Throw things to distract people - r/skyrimvr](https://www.reddit.com/r/skyrimvr/comments/1dksn36/higgs_update_throw_things_to_distract_people/) - HIGGS VR integration inspired by GrabAndThrow (Jan 2024)

4. CommonLibSSE Documentation - RE::PlayerCharacter, bhkWorld, hkpRigidBody APIs
   - `player->grabSpring` member
   - `player->DestroyMouseSprings()` method
   - `RE::hkpContactListener` interface
   - `RE::TESHavokUtilities::PushTemporaryMass()`

---

## Metadata

- **Search queries used:**
  - "GrabAndThrow powerof3 Skyrim SE SKSE"
  - "site:reddit.com GrabAndThrow Skyrim mod powerof3"
  - "powerof3 GrabAndThrow Skyrim SE mod mechanics"
  - "PlayerCharacter::grabSpring SKSE CommonLibSSE"

- **Sources consulted:** 4 (GitHub source code, 2 Reddit threads, CommonLibSSE docs)

- **Confidence level:** High - Analysis based on direct source code inspection of GrabAndThrow repository

- **Key code files analyzed:**
  - `src/GrabThrowHandler.h` (47 lines)
  - `src/GrabThrowHandler.cpp` (272 lines)
  - `src/Hooks.h` (11 lines)
  - `src/Hooks.cpp` (140 lines)
  - `src/main.cpp` (126 lines)

---

## Next Steps

1. **Verify current Drag & Drop behavior** - Test if removing GrabActor dispel breaks anything
2. **Implement impulse throwing** - Add `ApplyThrowImpulse()` method using GrabAndThrow pattern
3. **Test throw physics** - Verify NPC throws work with calculated impulse
4. **Optional: Add contact listener** - Implement collision detection for stealth mechanics
5. **Compare with your current implementation** - Identify remaining differences

---

**Report generated:** 2025-01-22
**Analysis complete:** ✅

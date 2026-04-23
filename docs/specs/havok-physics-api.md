# Spec: havok-physics-api

Scope: feature

# Havok Physics API Reference for Drag & Drop

## Scale Constants
```cpp
constexpr float BS_TO_HK_SCALE{ 0.0142875f };  // Bethesda → Havok unit conversion
constexpr float HK_TO_BS_SCALE{ 69.991251f };   // Havok → Bethesda unit conversion
```

## Player Grab System (CommonLibSSE)
```cpp
// Grab state detection
player->IsGrabbing()              // Returns bool — is player currently grabbing?
player->GetGrabbedRef()           // Returns TESObjectREFR* — the grabbed reference
player->grabType                  // GrabbingType enum: kNone, kNormal, kTelekinesis
player->grabSpring                // Array of niPointer<bhkMouseSpringAction>
player->DestroyMouseSprings()     // Kills all active grab springs (releases object)

// Grab initiation (investigate for Phase 2)
player->StartGrabObject()         // Initiates a grab on crosshair target
```

## Havok Spring Access (from GrabAndThrow)
```cpp
// Iterate player's grab springs
for (const auto& mouseSpring : player->grabSpring) {
    if (mouseSpring && mouseSpring->referencedObject) {
        auto hkMouseSpring = skyrim_cast<RE::hkpMouseSpringAction*>(mouseSpring->referencedObject.get());
        if (hkMouseSpring) {
            auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(hkMouseSpring->entity);
            auto bhkRigidBody = reinterpret_cast<RE::bhkRigidBody*>(hkpRigidBody->userData);
            // Now have full access to rigid body
        }
    }
}
```

## Throw Impulse (from GrabAndThrow)
```cpp
// Zero existing velocities
hkpRigidBody->SetLinearVelocity(RE::hkVector4());
hkpRigidBody->SetAngularVelocity(RE::hkVector4());

// Calculate impulse from camera forward direction
RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
float x = (matrix.entry[0][1] * force) * BS_TO_HK_SCALE;
float y = (matrix.entry[1][1] * force) * BS_TO_HK_SCALE;
float z = (matrix.entry[2][1] * force) * BS_TO_HK_SCALE;
RE::hkVector4 velocity(x, y, z, 0);
RE::hkVector4 impulse = velocity * mass;

// Apply
hkpRigidBody->ApplyLinearImpulse(impulse);
```

## Mass Access
```cpp
float mass = hkpRigidBody->motion.GetMass();

// For NPCs that are too light (< 20 units), push temporary mass for damage calc
RE::TESHavokUtilities::PushTemporaryMass(bhkRigidBody, minMass);
RE::TESHavokUtilities::PopTemporaryMass(bhkRigidBody);
```

## Physics Lock (REQUIRED before any Havok access)
```cpp
auto cell = player->GetParentCell();
auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
if (bhkWorld) {
    RE::BSWriteLockGuard locker(bhkWorld->worldLock);
    // All Havok operations here
}
```

## Key Scancodes
```
DIK_G = 0x22  (G key — release)
DIK_R = 0x13  (R key — throw charge)
```

## GMST Tuning (from GrabAndThrow INI)
```
fZKeyMaxContactDistance = 30.0    // Max distance before spring breaks
fZKeyMaxForce = 175.0             // Max spring force
fZKeyObjectDamping = 0.75         // Object damping
fZKeySpringDamping = 0.5          // Spring damping
fZKeySpringElasticity = 0.2       // Spring elasticity
fZKeyHeavyWeight = 100.0          // Heavy object threshold
```
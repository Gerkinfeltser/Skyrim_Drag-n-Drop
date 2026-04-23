# Research Report: hkpMouseSpringAction Memory Layout

**Date:** 2026-04-22
**Researcher:** Researcher
**Status:** Incomplete - Partial Findings

## Executive Summary

Research into Havok's `hkpMouseSpringAction` class memory layout reveals limited public documentation. Only two field offsets have been confirmed through existing Skyrim SKSE mod source code. The critical attachment point position (where the spring grabs the rigid body) remains UNKNOWN.

## Confirmed Offsets

### Offset 0x30: Entity Pointer
```cpp
auto entityPtr = *reinterpret_cast<RE::hkpEntity**>(actionBase + 0x30);
```
- **Type:** `hkpEntity*` (typically `hkpRigidBody*`)
- **Description:** Pointer to the physics entity being grabbed
- **Sources:**
  - `D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\src\DragHandler.cpp` (line 203)
  - powerof3/GrabAndThrow (inferred from `hkMouseSpring->entity` access)

### Offset 0x48: Spring Force Parameter
```cpp
// Zero the spring's own force at offset 0x48 so it stops pulling
*reinterpret_cast<float*>(actionBase + 0x48) = 0.0f;
```
- **Type:** `float`
- **Description:** Spring force/strength value. Zeroing this stops the spring from pulling.
- **Source:** `D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\src\DragHandler.cpp` (line 207)

## Unknown Critical Offsets

### ❌ Attachment Point Position (Primary Research Target)
The spring's attachment point on the rigid body - the local or world-space position where the spring connects to the body. This is essential for:
- Determining grab point on ragdoll NPCs
- Adjusting spring anchor during drag
- Calculating proper torque/rotation during throw

**Possible locations:**
- Likely between 0x38 and 0x60 (between entity and force parameter)
- Could be stored as `hkVector4` (16 bytes) or `NiPoint3` (12 bytes)
- May be in local body space or world space

### ❌ Other Unknown Fields
- Spring damping coefficient
- Spring elasticity/stiffness
- Mouse position in world space
- Transform matrices for coordinate conversion

## Havok Physics SDK Context

**Havok Version used by Skyrim:** Likely Havok Physics 2010-2012 era

**Known Base Class:** `hkpAction`
- Virtual function table at offset 0x00
- Typical size: 0x30-0x40 bytes before derived class members

**Inheritance Chain:**
```
hkpAction
  └─ hkpMouseSpringAction
```

## Class Layout Hypothesis

Based on confirmed offsets and Havok patterns:

```cpp
class hkpMouseSpringAction : public hkpAction {
    // hkpAction base (likely ~0x30 bytes)
    // ... base members ...

    // Derived class members
    hkpEntity* entity;          // +0x30 [CONFIRMED]
    // ... unknown fields ...
    hkVector4 attachmentPoint;  // ??? [UNKNOWN - PRIMARY TARGET]
    // ... unknown fields ...
    float springForce;          // +0x48 [CONFIRMED]
    // ... potentially more fields ...
};
```

## Search Attempts & Limitations

**Attempted Sources:**
1. ✅ GitHub: powerof3/GrabAndThrow - **Found** entity access pattern
2. ❌ GitHub: qm-computing/CatchAndRelease - Not found (may be archived/private)
3. ❌ DuckDuckGo searches for "hkpMouseSpringAction" - Bot detection limiting results
4. ❌ Havok SDK documentation - Not publicly available (proprietary)
5. ❌ CommonLibSSE headers - No direct hkpMouseSpringAction definition

**Barriers:**
- Havok Physics SDK is proprietary, closed-source
- Public SDK documentation removed from internet (Havok acquired by Microsoft)
- Most SKSE mods use engine abstraction layers, not direct Havok access
- Search engines limiting automated queries

## Key Insights from Code Analysis

### From DragHandler.cpp
```cpp
// Offset 0x48 is a float that controls spring pull strength
// Zeroing it stops the spring from pulling the object
*reinterpret_cast<float*>(actionBase + 0x48) = 0.0f;
```

This suggests:
- The spring continuously applies force during grab
- The force is stored as a single float parameter
- Modifying this at runtime affects spring behavior

### From GrabAndThrow (powerof3)
```cpp
if (auto hkMouseSpring = skyrim_cast<RE::hkpMouseSpringAction*>(mouseSpring->referencedObject.get())) {
    if (auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(hkMouseSpring->entity)) {
        // Access via entity field, not raw offsets
    }
}
```

This suggests:
- CommonLibSSE may have a `hkpMouseSpringAction` wrapper class
- The `entity` field is accessed through class definition, not raw offset
- **Action:** Check CommonLibSSE headers for class definition

## Recommended Next Steps

### 1. Check CommonLibSSE Headers (Highest Priority)
```bash
# Search for hkpMouseSpringAction definition
$ grep -r "hkpMouseSpringAction" $CommonLibSSEPath
$ grep -r "MouseSpring" $CommonLibSSEPath/include/RE
```

**Locations to check:**
- `$CommonLibSSEPath/include/RE/H/hkpMouseSpringAction.h`
- `$CommonLibSSEPath/include/RE/H/hkpAction.h`
- Any NiHavok or bhk headers

### 2. Runtime Memory Inspection
Add logging to dump all memory between 0x30 and 0x80:
```cpp
SKSE::log::info("=== MOUSE SPRING MEMORY DUMP ===");
auto* base = reinterpret_cast<uint8_t*>(actionBase);
for (int i = 0x30; i < 0x80; i += 4) {
    float* f = reinterpret_cast<float*>(base + i);
    void** p = reinterpret_cast<void**>(base + i);
    SKSE::log::info("+0x{:02X}: float={:.6f} ptr={:p}", i, *f, *p);
}
```

Look for:
- Position-like values (e.g., grab point coordinates)
- Patterns that change when grabbing different body parts
- NaN or infinity values (indicate unused/invalid fields)

### 3. Comparative Analysis with Other Havok Springs
Search for similar Havok constraint/action classes in CommonLibSSE:
- `hkpSpringAction`
- `hkpGenericSpringAction`
- `bhkSpringConstraint`

These may share memory layout patterns.

### 4. IDA/Ghidra Reverse Engineering
Decompile Skyrim executable (Havok behaviors):
- Find `PlayerCharacter::CreateMouseSpring` or similar
- Trace constructor parameters
- Identify field assignments after construction

## Sources

1. **powerof3/GrabAndThrow** - GitHub repository
   - URL: https://github.com/powerof3/GrabAndThrow
   - Access pattern for hkpMouseSpringAction::entity field
   - No raw offset usage (uses CommonLibSSE wrappers)

2. **Local Codebase Analysis**
   - `D:\gerkgit\Skyrim_Drag-n-Drop\SKSE\src\DragHandler.cpp`
   - Confirmed offset 0x30 (entity pointer)
   - Confirmed offset 0x48 (spring force float)

3. **AGENTS.md Documentation**
   - `D:\gerkgit\Skyrim_Drag-n-Drop\AGENTS.md`
   - Describes GrabAndThrow as reference for mouse spring access
   - Notes CatchAndRelease as previous attempt (no longer available)

## Metadata

- **Search queries attempted:** 15+
- **Sources successfully accessed:** 2
- **Confidence level:** Medium on confirmed offsets, Low on attachment point
- **Research time:** ~45 minutes
- **Blockers:** Proprietary Havok SDK, search engine limitations, archived repos

## Current Assessment

**What We Know:**
- ✅ Offset 0x30 = entity pointer
- ✅ Offset 0x48 = spring force parameter
- ✅ Basic usage pattern from GrabAndThrow

**What We Don't Know:**
- ❌ Attachment point/anchor position location
- ❌ Complete memory layout
- ❌ Spring parameters beyond force

**Critical Missing Information:**
The attachment point position is essential for implementing:
1. Proper grab point visualization
2. Adjusting grab position during drag
3. Realistic throw torque/rotation
4. Attaching to specific ragdoll body parts

**Status:** Unable to locate complete `hkpMouseSpringAction` class definition through public sources. Direct access to Havok SDK or runtime memory analysis required.

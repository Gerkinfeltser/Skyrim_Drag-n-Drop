#include "DragHandler.h"

#include <cstdio>
#include <vector>

namespace
{
    constexpr RE::FormID GHOST_KEYWORD{ 0xD205E };
    constexpr RE::FormID IMMUNE_PARALYSIS_KEYWORD{ 0xF23C5 };

    constexpr uint32_t KEY_G = 0x22;
    constexpr uint32_t KEY_R = 0x13;

    RE::BGSKeyword* GetKeyword(RE::FormID a_formID)
    {
        auto factory = RE::TESForm::LookupByID(a_formID);
        return factory ? factory->As<RE::BGSKeyword>() : nullptr;
    }
}

bool DragHandler::LoadSettings()
{
    SKSE::log::info("Settings loaded (defaults)");
    return true;
}

void DragHandler::OnDataLoad()
{
    SKSE::log::info("Data loaded (GMST tuning skipped — using engine defaults)");
}

bool DragHandler::IsValidTarget(RE::Actor* a_actor) const
{
    if (!a_actor || a_actor->IsPlayerRef()) return false;

    auto ghostKw = GetKeyword(GHOST_KEYWORD);
    auto paraKw = GetKeyword(IMMUNE_PARALYSIS_KEYWORD);
    if ((ghostKw && a_actor->HasKeyword(ghostKw)) || (paraKw && a_actor->HasKeyword(paraKw))) return false;

    if (a_actor->IsGhost()) return false;

    bool isDead = a_actor->IsDead();
    bool isParalyzed = a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis) > 0.0f;
    bool isFollower = a_actor->IsPlayerTeammate();

    return isDead || isParalyzed || isFollower;
}

RE::Actor* DragHandler::GetCrosshairActor() const
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nullptr;

    auto processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return nullptr;

    RE::Actor* closest = nullptr;
    float closestDist = 150.0f;
    auto playerPos = player->GetPosition();

    processLists->ForAllActors([&](RE::Actor& actor) {
        if (!IsValidTarget(&actor)) return RE::BSContainer::ForEachResult::kContinue;

        float dist = std::sqrt(playerPos.GetSquaredDistance(actor.GetPosition()));
        if (dist < closestDist) {
            closestDist = dist;
            closest = &actor;
        }
        return RE::BSContainer::ForEachResult::kContinue;
    });

    return closest;
}

void DragHandler::DrainStamina(float a_dt)
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    float currentStamina = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
    float drain = staminaDrainRate * a_dt;
    if (currentStamina - drain <= 0.0f) {
        RE::DebugNotification("Too exhausted to keep holding");
    } else {
        player->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -drain);
    }
}

float DragHandler::GetForce(float a_heldDuration) const
{
    float force = a_heldDuration * throwStrengthMult;
    if (force > throwImpulseMax) {
        force = throwImpulseMax;
    }
    return force;
}

RE::hkVector4 DragHandler::GetImpulse(float a_force, float a_mass) const
{
    RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
    float x = (matrix.entry[0][1] * a_force) * BS_TO_HK_SCALE;
    float y = (matrix.entry[1][1] * a_force) * BS_TO_HK_SCALE;
    float z = (matrix.entry[2][1] * a_force) * BS_TO_HK_SCALE;

    RE::hkVector4 velocity(x, y, z, 0);
    return velocity * a_mass;
}

void DragHandler::ZeroGrabbedVelocity(RE::PlayerCharacter* a_player)
{
    if (!a_player) return;

    auto cell = a_player->GetParentCell();
    auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
    if (!bhkWorld) return;

    RE::BSWriteLockGuard locker(bhkWorld->worldLock);

    auto& grabSpring = a_player->GetPlayerRuntimeData().grabSpring;
    for (auto& springRef : grabSpring) {
        if (!springRef) continue;

        auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
        if (!bhkObj || !bhkObj->referencedObject) continue;

        auto actionBase = reinterpret_cast<std::uintptr_t>(bhkObj->referencedObject.get());
        auto entityPtr = *reinterpret_cast<RE::hkpEntity**>(actionBase + 0x30);
        if (!entityPtr) continue;

        // Zero the spring's own force at offset 0x48 so it stops pulling
        *reinterpret_cast<float*>(actionBase + 0x48) = 0.0f;

        auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(entityPtr);
        hkpRigidBody->motion.SetLinearVelocity(RE::hkVector4());
        hkpRigidBody->motion.SetAngularVelocity(RE::hkVector4());
    }
}

void DragHandler::ThrowGrabbedObject(float a_heldDuration)
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto cell = player->GetParentCell();
    auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
    if (!bhkWorld) return;

    RE::BSWriteLockGuard locker(bhkWorld->worldLock);

    float force = GetForce(a_heldDuration);

    auto& grabSpring = player->GetPlayerRuntimeData().grabSpring;

    SKSE::log::info("ThrowGrabbedObject: grabSpring size={}", grabSpring.size());

    for (auto& springRef : grabSpring) {
        if (!springRef) {
            SKSE::log::info("  springRef is null, skipping");
            continue;
        }

        auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
        if (!bhkObj || !bhkObj->referencedObject) {
            SKSE::log::info("  bhkObj or referencedObject null");
            continue;
        }

        auto hkObj = bhkObj->referencedObject.get();

        // hkpMouseSpringAction extends hkpAction (size 0x30).
        // At offset 0x30: hkpEntity* m_entity (the grabbed rigid body)
        // At offset 0x38: hkVector4 m_positionInWorld
        // At offset 0x48: float m_force
        auto actionBase = reinterpret_cast<std::uintptr_t>(hkObj);
        auto entityPtr = *reinterpret_cast<RE::hkpEntity**>(actionBase + 0x30);

        SKSE::log::info("  hkObj ptr={}, entityPtr={}", (void*)hkObj, (void*)entityPtr);

        if (!entityPtr) {
            SKSE::log::info("  entityPtr is null");
            continue;
        }

        auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(entityPtr);
        float mass = hkpRigidBody->motion.GetMass();
        auto impulse = GetImpulse(force, mass);

        hkpRigidBody->motion.SetLinearVelocity(RE::hkVector4());
        hkpRigidBody->motion.SetAngularVelocity(RE::hkVector4());
        hkpRigidBody->motion.ApplyLinearImpulse(impulse);

        SKSE::log::info("Throw applied: force={:.1f}, mass={:.1f}, impulse=({:.1f},{:.1f},{:.1f})",
            force, mass,
            impulse.quad.m128_f32[0], impulse.quad.m128_f32[1], impulse.quad.m128_f32[2]);
    }

    player->DestroyMouseSprings();
}

void DragHandler::UpdateGrabState()
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (state == State::None && player->IsGrabbing()) {
        auto grabbedRef = player->GetGrabbedRef();
        if (grabbedRef) {
            grabbedActor = grabbedRef->As<RE::Actor>();
            if (grabbedActor) {
                state = State::Dragging;
                std::string name = grabbedActor->GetDisplayFullName();
                SKSE::log::info("Grabbed: {} ({:08X})", name, grabbedActor->GetFormID());
            }
        }
    } else if (state == State::Dragging && !player->IsGrabbing()) {
        SKSE::log::info("Grab lost (engine released)");
        grabbedActor = nullptr;
        state = State::None;
        rKeyHeld = false;
    }
}

void DragHandler::OnKeyDown(uint32_t a_key)
{
    if (a_key == KEY_R && state == State::Dragging) {
        rKeyHeld = true;
        rKeyTime = std::chrono::steady_clock::now();
        SKSE::log::info("R key down, charging throw");
    }
}

void DragHandler::OnKeyUp(uint32_t a_key)
{
    if (a_key == KEY_G && state == State::Dragging) {
        SKSE::log::info("G key up -- releasing (no throw)");
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->DestroyMouseSprings();
            player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        }
        if (grabbedActor) {
            grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
            grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
        }
        grabbedActor = nullptr;
        state = State::None;
        rKeyHeld = false;
        RE::DebugNotification("Dropped");
        return;
    }

    if (a_key == KEY_R && state == State::Dragging && rKeyHeld) {
        auto now = std::chrono::steady_clock::now();
        float heldDuration = std::chrono::duration<float>(now - rKeyTime).count();

        if (heldDuration < 0.5f) {
            SKSE::log::info("R key tap -- treating as drop ({:.2f}s)", heldDuration);
        } else {
            float force = GetForce(heldDuration);
            SKSE::log::info("R key up -- throwing (held {:.2f}s, force={:.0f})", heldDuration, force);
            ThrowGrabbedObject(heldDuration);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Threw! (%.0f force)", force);
            RE::DebugNotification(buf);
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        }
        if (grabbedActor) {
            grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
            grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
        }
        grabbedActor = nullptr;
        state = State::None;
        rKeyHeld = false;
    }
}

bool DragHandler::ReleaseNPC(bool a_throw, float a_force)
{
    if (state == State::None) return false;

    auto player = RE::PlayerCharacter::GetSingleton();

    if (a_throw && player) {
        ThrowGrabbedObject(a_force > 0.0f ? a_force : 1.0f);
    } else if (player) {
        player->DestroyMouseSprings();
    }

    if (grabbedActor) {
        grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
    }

    SKSE::log::info("Released (throw={}, force={:.1f})", a_throw, a_force);

    RE::DebugNotification(a_throw ? "Threw!" : "Released");
    grabbedActor = nullptr;
    state = State::None;
    rKeyHeld = false;

    return true;
}

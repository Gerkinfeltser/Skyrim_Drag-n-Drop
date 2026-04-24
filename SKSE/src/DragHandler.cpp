#include "DragHandler.h"
#include "Hooks.h"

#include <cstdlib>
#include <cstdio>
#include <vector>
#include <windows.h>

#include "RE/B/BSVisit.h"
#include "RE/B/bhkCollisionObject.h"
#include "RE/B/bhkRigidBody.h"
#include "RE/B/BShkbAnimationGraph.h"
#include "RE/T/TESDataHandler.h"

namespace
{
    constexpr RE::FormID GHOST_KEYWORD{ 0xD205E };
    constexpr RE::FormID IMMUNE_PARALYSIS_KEYWORD{ 0xF23C5 };

    RE::BGSKeyword* GetKeyword(RE::FormID a_formID)
    {
        auto factory = RE::TESForm::LookupByID(a_formID);
        return factory ? factory->As<RE::BGSKeyword>() : nullptr;
    }

    std::vector<RE::hkpRigidBody*> CollectAllRigidBodies(RE::Actor* a_actor)
    {
        std::vector<RE::hkpRigidBody*> bodies;
        if (!a_actor) return bodies;

        auto root = a_actor->Get3D();
        if (!root) return bodies;

        RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_colObj) {
            if (!a_colObj) return RE::BSVisit::BSVisitControl::kContinue;

            auto colObj = static_cast<RE::bhkCollisionObject*>(a_colObj);
            auto rigidBody = colObj->GetRigidBody();
            if (rigidBody && rigidBody->referencedObject) {
                auto hkpBody = reinterpret_cast<RE::hkpRigidBody*>(rigidBody->referencedObject.get());
                if (hkpBody) bodies.push_back(hkpBody);
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });

        SKSE::log::info("Collected {} rigid bodies from actor scene graph", bodies.size());
        return bodies;
    }
}

bool DragHandler::LoadSettings()
{
    grabFollowers = true;
    grabChildren = false;
    grabAnyone = false;
    enabled = true;
    grabRange = 150.0f;
    grabHoldDist = 150.0f;
    throwImpulseMax = 10.0f;
    throwDropWindow = 0.5f;
    throwTimeToMax = 4.0f;
    actionKey = 0x22;
    noSpeedPenalty = true;
    staminaDrainRate = 5.0f;
    dragSpeedMult = 3.0f;
    useShoutKeyForRelease = true;

    SKSE::log::info("Settings loaded: enabled={}, range={:.0f}, followers={}, children={}, anyone={}, actionKey={}",
        enabled, grabRange, grabFollowers, grabChildren, grabAnyone, actionKey);

    return true;
}

void DragHandler::OnDataLoad()
{
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        grabSpell = dataHandler->LookupForm<RE::SpellItem>(0x800, "DragAndDrop.esp");
        SKSE::log::info("Data loaded, grab spell: {:p}", (void*)grabSpell);
    } else {
        SKSE::log::warn("Data loaded, TESDataHandler not available");
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (player && grabSpell) {
        player->AddSpell(grabSpell);
        SKSE::log::info("Added grab spell to player via AddSpell");
    }
}

bool DragHandler::IsValidTarget(RE::Actor* a_actor) const
{
    if (!a_actor || a_actor->IsPlayerRef()) return false;

    auto ghostKw = GetKeyword(GHOST_KEYWORD);
    auto paraKw = GetKeyword(IMMUNE_PARALYSIS_KEYWORD);
    if ((ghostKw && a_actor->HasKeyword(ghostKw)) || (paraKw && a_actor->HasKeyword(paraKw))) return false;

    if (a_actor->IsGhost()) return false;
    if (!grabChildren && a_actor->IsChild()) return false;

    bool isDead = a_actor->IsDead();
    bool isParalyzed = a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis) > 0.0f;
    bool isFollower = a_actor->IsPlayerTeammate();

    if (grabAnyone) return true;
    if (isDead || isParalyzed) return true;
    if (grabFollowers && isFollower) return true;

    return false;
}

RE::Actor* DragHandler::GetCrosshairActor() const
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nullptr;

    auto processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return nullptr;

    RE::Actor* closest = nullptr;
    float closestDist = grabRange;
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

void DragHandler::ApplySpeedBoost(RE::PlayerCharacter* a_player)
{
    if (!noSpeedPenalty || !a_player) return;
    savedSpeedMult = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeedMult);
    a_player->AsActorValueOwner()->SetActorValue(RE::ActorValue::kSpeedMult, savedSpeedMult * dragSpeedMult);
    SKSE::log::info("Speed boost applied: saved={:.1f}, boosted={:.1f}", savedSpeedMult, savedSpeedMult * dragSpeedMult);
}

void DragHandler::RestoreSpeed(RE::PlayerCharacter* a_player)
{
    if (!noSpeedPenalty || !a_player) return;
    a_player->AsActorValueOwner()->SetActorValue(RE::ActorValue::kSpeedMult, savedSpeedMult);
    SKSE::log::info("Speed restored to {:.1f}", savedSpeedMult);
    savedSpeedMult = 0.0f;
}

float DragHandler::GetForce(float a_heldDuration) const
{
    float effective = a_heldDuration - throwDropWindow;
    if (effective < 0.0f) effective = 0.0f;
    float force = (effective / throwTimeToMax) * throwImpulseMax;
    if (force > throwImpulseMax) {
        force = throwImpulseMax;
    }
    return force;
}

RE::hkVector4 DragHandler::GetImpulse(float a_force, float a_mass) const
{
    RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
    float x = matrix.entry[0][1] * a_force;
    float y = matrix.entry[1][1] * a_force;
    float z = matrix.entry[2][1] * a_force;

    return RE::hkVector4(x, y, z, 0) * a_mass;
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

    float force = GetForce(a_heldDuration);

    RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
    float dirX = matrix.entry[0][1];
    float dirY = matrix.entry[1][1];
    float dirZ = matrix.entry[2][1];

    RE::hkVector4 throwDir(dirX, dirY, dirZ, 0);

    auto allBodies = CollectAllRigidBodies(grabbedActor);

    player->DestroyMouseSprings();

    SKSE::log::info("ThrowGrabbedObject: force={:.1f}, bodies={}", force, allBodies.size());

    auto capturedBodies = allBodies;
    auto capturedDir = throwDir;
    float capturedForce = force;

    SKSE::GetTaskInterface()->AddTask([capturedBodies, capturedDir, capturedForce]() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto cell = player->GetParentCell();
        auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
        if (!bhkWorld) return;
        RE::BSWriteLockGuard locker(bhkWorld->worldLock);

        for (auto* body : capturedBodies) {
            if (!body) continue;
            body->motion.SetLinearVelocity(RE::hkVector4());
            body->motion.SetAngularVelocity(RE::hkVector4());
        }

        RE::hkVector4 impulse = capturedDir * capturedForce;
        for (auto* body : capturedBodies) {
            if (!body) continue;
            float mass = body->motion.GetMass();
            if (mass > 0.001f) {
                body->motion.ApplyLinearImpulse(impulse * mass);
            }
        }

        SKSE::log::info("Throw: delayed zero + impulse on all {} bodies, force={:.1f}", capturedBodies.size(), capturedForce);
    });
}

void DragHandler::ForceRagdoll(RE::Actor* a_actor)
{
    if (!a_actor) return;

    SKSE::log::info("ForceRagdoll: {:08X} (dead={}, ragdollState={})",
        a_actor->GetFormID(), a_actor->IsDead(), a_actor->IsInRagdollState());

    a_actor->NotifyAnimationGraph("ragdoll");

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
    if (a_actor->GetAnimationGraphManager(graphManager) && graphManager) {
        for (auto& graphPtr : graphManager->graphs) {
            if (!graphPtr) continue;
            auto* driver = static_cast<RE::BSIRagdollDriver*>(graphPtr.get());
            if (driver && driver->HasRagdoll()) {
                driver->AddRagdollToWorld();
                driver->SetMotionType(RE::hkpMotion::MotionType::kDynamic);
                SKSE::log::info("  Ragdoll driver: added to world, motion=dynamic");
            }
        }
    }

    auto root = a_actor->Get3D();
    if (root) {
        root->UpdateRigidConstraints(true);

        int bodyCount = 0;
        RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_colObj) {
            if (!a_colObj) return RE::BSVisit::BSVisitControl::kContinue;
            auto colObj = static_cast<RE::bhkCollisionObject*>(a_colObj);
            auto rigidBody = colObj->GetRigidBody();
            if (rigidBody && rigidBody->referencedObject) {
                auto hkpBody = reinterpret_cast<RE::hkpRigidBody*>(rigidBody->referencedObject.get());
                if (hkpBody) {
                    hkpBody->motion.type = RE::hkpMotion::MotionType::kDynamic;
                    bodyCount++;
                }
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        SKSE::log::info("  Set {} bodies to kDynamic", bodyCount);
    }
}

void DragHandler::UpdateGrabState()
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (state == State::None && player->IsGrabbing()) {
        auto grabbedRef = player->GetGrabbedRef();
        SKSE::log::info("UpdateGrabState: IsGrabbing=true, grabbedRef={}", grabbedRef ? "yes" : "null");
        if (grabbedRef) {
            grabbedActor = grabbedRef->As<RE::Actor>();
            if (grabbedActor) {
                state = State::Dragging;
                grabStartTime = std::chrono::steady_clock::now();
                ApplySpeedBoost(player);

                auto& grabSpring = player->GetPlayerRuntimeData().grabSpring;
                for (auto& springRef : grabSpring) {
                    if (!springRef) continue;
                    auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
                    if (!bhkObj || !bhkObj->referencedObject) continue;
                    auto actionBase = reinterpret_cast<std::uintptr_t>(bhkObj->referencedObject.get());

                    auto* damping = reinterpret_cast<float*>(actionBase + 0x60);
                    auto* elasticity = reinterpret_cast<float*>(actionBase + 0x64);
                    auto* maxForce = reinterpret_cast<float*>(actionBase + 0x68);

                    *damping = 1.5f;
                    *elasticity = 0.05f;
                    *maxForce = 500.0f;
                }

                auto cell = player->GetParentCell();
                auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
                if (bhkWorld) {
                    RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                    auto allBodies = CollectAllRigidBodies(grabbedActor);
                    for (auto* body : allBodies) {
                        body->motion.SetLinearVelocity(RE::hkVector4());
                        body->motion.SetAngularVelocity(RE::hkVector4());
                    }
                }
            }
        }
    }

    if (state == State::Dragging && !player->IsGrabbing()) {
        RestoreSpeed(player);
        grabbedActor = nullptr;
        state = State::None;
        actionKeyHeld = false;
        actionNotified = false;
        spellCastDetected = false;
    }

    if (state == State::Dragging && (actionKeyHeld || spellCastDetected) && !actionNotified) {
        auto now = std::chrono::steady_clock::now();
        auto startTime = actionKeyHeld ? actionKeyTime : spellCastTime;
        float elapsed = std::chrono::duration<float>(now - startTime).count();
        if (elapsed >= throwDropWindow) {
            actionNotified = true;
            RE::DebugNotification("Ready to throw!");
        }
    }
}

void DragHandler::OnKeyDown(uint32_t a_key, const char* a_userEvent)
{
    if (state == State::Dragging && useShoutKeyForRelease && strcmp(a_userEvent, "Shout") == 0 && !spellCastDetected) {
        spellCastDetected = true;
        spellCastTime = std::chrono::steady_clock::now();
        SKSE::log::info("Power/Shout key down while dragging, charging throw");
        return;
    }

    if (a_key == actionKey && state == State::Dragging && !actionKeyHeld) {
        actionKeyHeld = true;
        actionKeyTime = std::chrono::steady_clock::now();
        SKSE::log::info("Action key down (0x{:02X}), charging throw", a_key);
    } else if (a_key == actionKey && state == State::None && bEnableGKeyGrab) {
        SKSE::log::info("G-key grab: calling TryGrabWithSpell");
        TryGrabWithSpell();
    }
}

void DragHandler::OnKeyUp(uint32_t a_key, const char* a_userEvent)
{
    bool isShoutUp = useShoutKeyForRelease && (strcmp(a_userEvent, "Shout") == 0) && spellCastDetected;
    bool isActionUp = (a_key == actionKey) && actionKeyHeld;

    if (!isShoutUp && !isActionUp) return;
    if (state != State::Dragging) return;

    auto now = std::chrono::steady_clock::now();
    float heldDuration = 0.0f;

    if (isShoutUp) {
        heldDuration = std::chrono::duration<float>(now - spellCastTime).count();
        spellCastDetected = false;
    } else {
        heldDuration = std::chrono::duration<float>(now - actionKeyTime).count();
        actionKeyHeld = false;
    }

    DoRelease(heldDuration);
}

void DragHandler::DoRelease(float a_heldDuration)
{
    auto player = RE::PlayerCharacter::GetSingleton();
    std::vector<RE::hkpRigidBody*> bodiesToZero = CollectAllRigidBodies(grabbedActor);

    if (a_heldDuration < throwDropWindow) {
        SKSE::log::info("Dropping ({:.2f}s)", a_heldDuration);

        if (player) {
            player->DestroyMouseSprings();
            player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        }
        if (grabbedActor) {
            grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
            grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
        }

        if (!bodiesToZero.empty()) {
            SKSE::GetTaskInterface()->AddTask([bodiesToZero]() {
                auto p = RE::PlayerCharacter::GetSingleton();
                if (!p) return;
                auto cell = p->GetParentCell();
                auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
                if (!bhkWorld) return;
                RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                for (auto* body : bodiesToZero) {
                    if (body) {
                        body->motion.SetLinearVelocity(RE::hkVector4());
                        body->motion.SetAngularVelocity(RE::hkVector4());
                    }
                }
                SKSE::log::info("Delayed velocity zero applied ({} bodies)", bodiesToZero.size());
            });
        }

        grabbedActor = nullptr;
        state = State::None;
        actionKeyHeld = false;
        actionNotified = false;
        spellCastDetected = false;
        RestoreSpeed(player);
        RE::DebugNotification("Dropped");
        return;
    }

    float force = GetForce(a_heldDuration);
    SKSE::log::info("Throwing (held {:.2f}s, force={:.0f})", a_heldDuration, force);
    ThrowGrabbedObject(a_heldDuration);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Threw! (%.0f force)", force);
    RE::DebugNotification(buf);

    if (player) {
        player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
    }
    if (grabbedActor) {
        grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
    }
    grabbedActor = nullptr;
    state = State::None;
    actionKeyHeld = false;
    actionNotified = false;
    spellCastDetected = false;
    RestoreSpeed(player);
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
    RestoreSpeed(player);
    grabbedActor = nullptr;
    state = State::None;
    actionKeyHeld = false;

    return true;
}

void DragHandler::TryGrabWithSpell()
{
    if (state != State::None) return;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || !grabSpell) return;

    auto target = GetCrosshairActor();
    if (!target || !IsValidTarget(target)) return;

    RE::FormID targetFormID = target->GetFormID();
    float holdDist = grabHoldDist;

    SKSE::GetTaskInterface()->AddTask([this, targetFormID, holdDist]() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto target = RE::TESForm::LookupByID(targetFormID)->As<RE::Actor>();
        if (!target) return;

        player->GetPlayerRuntimeData().grabObjectWeight = 0.0f;
        player->GetPlayerRuntimeData().grabDistance = holdDist;
        player->GetPlayerRuntimeData().grabbedObject = target->CreateRefHandle();

        auto caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
        if (!caster) return;

        caster->CastSpellImmediate(grabSpell, false, target, 1.0f, false, 0.0f, player);
    });
}

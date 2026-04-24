#include "DragHandler.h"
#include "Hooks.h"

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>

#include "RE/B/BSVisit.h"
#include "RE/B/bhkCollisionObject.h"
#include "RE/B/bhkRigidBody.h"
#include "RE/B/BShkbAnimationGraph.h"
#include "RE/T/TESDataHandler.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    std::string GetINIPath()
    {
        char buf[MAX_PATH];
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), buf, MAX_PATH);
        auto* slash = strrchr(buf, '\\');
        if (slash) slash[1] = '\0';
        strcat_s(buf, "DragAndDrop.ini");
        return std::string(buf);
    }

    std::string GetINIOption(const std::string& a_path, const char* a_section, const char* a_key, const char* a_default)
    {
        char buf[256];
        GetPrivateProfileStringA(a_section, a_key, a_default, buf, sizeof(buf), a_path.c_str());
        return std::string(buf);
    }

    bool GetINIBool(const std::string& a_path, const char* a_section, const char* a_key, bool a_default)
    {
        auto val = GetINIOption(a_path, a_section, a_key, a_default ? "true" : "false");
        return val == "true" || val == "1";
    }

    float GetINIFloat(const std::string& a_path, const char* a_section, const char* a_key, float a_default)
    {
        char defBuf[32];
        std::snprintf(defBuf, sizeof(defBuf), "%.6f", a_default);
        auto val = GetINIOption(a_path, a_section, a_key, defBuf);
        return static_cast<float>(std::atof(val.c_str()));
    }

    int GetINIInt(const std::string& a_path, const char* a_section, const char* a_key, int a_default)
    {
        char defBuf[16];
        std::snprintf(defBuf, sizeof(defBuf), "%d", a_default);
        auto val = GetINIOption(a_path, a_section, a_key, defBuf);
        return std::atoi(val.c_str());
    }

    constexpr RE::FormID GHOST_KEYWORD{ 0xD205E };
    constexpr RE::FormID IMMUNE_PARALYSIS_KEYWORD{ 0xF23C5 };

    void ApplyClampedImpulse(RE::hkpRigidBody* body, const RE::hkVector4& impulse, float maxVel)
    {
        body->motion.ApplyLinearImpulse(impulse);
        auto& vel = body->motion.linearVelocity;
        float speed = std::sqrt(vel.quad.m128_f32[0] * vel.quad.m128_f32[0] +
                                vel.quad.m128_f32[1] * vel.quad.m128_f32[1] +
                                vel.quad.m128_f32[2] * vel.quad.m128_f32[2]);
        if (speed > maxVel && speed > 0.001f) {
            float scale = maxVel / speed;
            vel.quad.m128_f32[0] *= scale;
            vel.quad.m128_f32[1] *= scale;
            vel.quad.m128_f32[2] *= scale;
        }
    }

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
    auto iniPath = GetINIPath();
    SKSE::log::info("Loading settings from: {}", iniPath);

    enabled = GetINIBool(iniPath, "General", "bEnableMod", true);
    grabRange = GetINIFloat(iniPath, "General", "fGrabRange", 150.0f);
    grabHoldDist = GetINIFloat(iniPath, "General", "fGrabHoldDist", 150.0f);
    grabFollowers = GetINIBool(iniPath, "General", "bGrabFollowers", true);
    grabChildren = GetINIBool(iniPath, "General", "bGrabChildren", false);
    grabAnyone = GetINIBool(iniPath, "General", "bGrabAnyone", false);
    grabHostile = GetINIBool(iniPath, "General", "bGrabHostile", false);
    staminaDrainRate = GetINIFloat(iniPath, "General", "fStaminaDrainRate", 5.0f);
    dragSpeedMult = GetINIFloat(iniPath, "General", "fDragSpeedMult", 3.0f);
    noSpeedPenalty = GetINIBool(iniPath, "General", "bNoSpeedPenalty", true);
    actionKey = static_cast<uint32_t>(GetINIInt(iniPath, "General", "iActionKey", 0x22));
    useShoutKeyForRelease = GetINIBool(iniPath, "General", "bUseShoutKeyForRelease", true);

    throwImpulseMax = GetINIFloat(iniPath, "Throw", "fThrowImpulseMax", 10.0f);
    throwDropWindow = GetINIFloat(iniPath, "Throw", "fThrowDropWindow", 0.5f);
    throwTimeToMax = GetINIFloat(iniPath, "Throw", "fThrowTimeToMax", 4.0f);

    impactRadius = GetINIFloat(iniPath, "Impact", "fImpactRadius", 200.0f);
    impactDuration = GetINIFloat(iniPath, "Impact", "fImpactDuration", 3.0f);
    impactMinVelocity = GetINIFloat(iniPath, "Impact", "fImpactMinVelocity", 0.5f);
    impactForce = GetINIFloat(iniPath, "Impact", "fImpactForce", 300.0f);
    impactPushForceMax = GetINIFloat(iniPath, "Impact", "fImpactPushForceMax", 5.0f);
    impactDamage = GetINIFloat(iniPath, "Impact", "fImpactDamage", 0.0f);
    impactDamageThrownMult = GetINIFloat(iniPath, "Impact", "fImpactDamageThrownMult", 1.0f);
    impactOnDrop = GetINIBool(iniPath, "Impact", "bImpactOnDrop", false);
    swingImpactCooldown = GetINIFloat(iniPath, "Impact", "fSwingImpactCooldown", 0.5f);
    swingImpactRadiusMult = GetINIFloat(iniPath, "Impact", "fSwingImpactRadiusMult", 0.5f);
    swingImpactStatics = GetINIBool(iniPath, "Impact", "bSwingImpactStatics", true);
    ragdollMaxVelocity = GetINIFloat(iniPath, "Impact", "fRagdollMaxVelocity", 20.0f);

    SKSE::log::info("Settings: enabled={}, range={:.0f}, holdDist={:.0f}, followers={}, children={}, anyone={}, hostile={}",
        enabled, grabRange, grabHoldDist, grabFollowers, grabChildren, grabAnyone, grabHostile);
    SKSE::log::info("  throw: impulseMax={:.1f}, dropWindow={:.2f}, timeToMax={:.1f}",
        throwImpulseMax, throwDropWindow, throwTimeToMax);
    SKSE::log::info("  impact: radius={:.0f}, duration={:.1f}, minVel={:.2f}, force={:.0f}, pushForce={:.1f}, damage={:.1f}, thrownDmgMult={:.1f}",
        impactRadius, impactDuration, impactMinVelocity, impactForce, impactPushForceMax, impactDamage, impactDamageThrownMult);
    SKSE::log::info("  misc: staminaDrain={:.1f}, dragSpeed={:.1f}, noSpeedPenalty={}, actionKey=0x{:02X}, shoutKey={}",
        staminaDrainRate, dragSpeedMult, noSpeedPenalty, actionKey, useShoutKeyForRelease);

    return true;
}

void DragHandler::OnDataLoad()
{
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        grabSpell = dataHandler->LookupForm<RE::SpellItem>(0x800, "DragAndDrop.esp");
        SKSE::log::info("Data loaded, grab spell: {:p}", (void*)grabSpell);

        impactCloakSpell = dataHandler->LookupForm<RE::SpellItem>(0x808, "DragAndDrop.esp");
        SKSE::log::info("Data loaded, impact spell: {:p}", (void*)impactCloakSpell);
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

    if (grabHostile) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player && !a_actor->IsHostileToActor(player)) return false;
    }

    if (grabFollowers && isFollower) return true;

    return false;
}

RE::Actor* DragHandler::GetCrosshairActor() const
{
    auto pickData = RE::CrosshairPickData::GetSingleton();
    if (!pickData) return nullptr;

    auto targetHandle = pickData->targetActor;
    if (!targetHandle) return nullptr;

    auto targetRef = targetHandle.get();
    if (!targetRef) return nullptr;

    return targetRef->As<RE::Actor>();
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
        SKSE::log::info("UpdateGrabState: IsGrabbing=true, grabbedRef={} ({:08X})",
            grabbedRef ? grabbedRef->GetDisplayFullName() : "null",
            grabbedRef ? grabbedRef->GetFormID() : 0);
        if (grabbedRef) {
            grabbedActor = grabbedRef->As<RE::Actor>();
            if (grabbedActor && IsValidTarget(grabbedActor)) {
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
            } else {
                SKSE::log::info("UpdateGrabState: engine grab rejected, releasing");
                player->GetPlayerRuntimeData().grabbedObject = RE::ActorHandle();
                player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
                grabbedActor = nullptr;
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
        swingCooldowns.clear();
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

    if (state == State::Dragging && grabbedActor) {
        auto now = std::chrono::steady_clock::now();
        float grabElapsed = std::chrono::duration<float>(now - grabStartTime).count();
        if (grabElapsed < 0.5f) return;

        auto thrown3D = grabbedActor->Get3D();
        if (!thrown3D) return;
        RE::NiPoint3 thrownPos = thrown3D->world.translate;

        auto& grabSpring = player->GetPlayerRuntimeData().grabSpring;
        float springSpeed = 0.0f;
        for (auto& springRef : grabSpring) {
            if (!springRef) continue;
            auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
            if (!bhkObj || !bhkObj->referencedObject) continue;
            auto actionBase = reinterpret_cast<std::uintptr_t>(bhkObj->referencedObject.get());
            auto entityPtr = *reinterpret_cast<RE::hkpEntity**>(actionBase + 0x30);
            if (!entityPtr) continue;
            auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(entityPtr);
            auto& lv = hkpRigidBody->motion.linearVelocity;
            float s = lv.quad.m128_f32[0] * lv.quad.m128_f32[0] +
                      lv.quad.m128_f32[1] * lv.quad.m128_f32[1] +
                      lv.quad.m128_f32[2] * lv.quad.m128_f32[2];
            springSpeed = std::sqrt(s);
            break;
        }

        if (springSpeed < impactMinVelocity) return;

        if (swingImpactRadiusMult <= 0.0f) return;
        float swingRadius = impactRadius * swingImpactRadiusMult;

        auto cell = player->GetParentCell();
        if (!cell) return;

        cell->ForEachReferenceInRange(thrownPos, swingRadius, [&](RE::TESObjectREFR& a_ref) {
            if (a_ref.GetFormID() == player->GetFormID()) return RE::BSContainer::ForEachResult::kContinue;
            if (grabbedActor && a_ref.GetFormID() == grabbedActor->GetFormID()) return RE::BSContainer::ForEachResult::kContinue;

            auto refPos = a_ref.GetPosition();
            auto ref3D = a_ref.Get3D();
            if (ref3D) {
                refPos.x = ref3D->world.translate.x;
                refPos.y = ref3D->world.translate.y;
                refPos.z = ref3D->world.translate.z;
            }
            float dx = thrownPos.x - refPos.x;
            float dy = thrownPos.y - refPos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > swingRadius) return RE::BSContainer::ForEachResult::kContinue;

            auto cdIt = swingCooldowns.find(a_ref.GetFormID());
            if (cdIt != swingCooldowns.end()) {
                float cdElapsed = std::chrono::duration<float>(now - cdIt->second).count();
                if (cdElapsed < swingImpactCooldown) return RE::BSContainer::ForEachResult::kContinue;
            }

            auto* actor = a_ref.As<RE::Actor>();
            if (actor) {
                if (actor->IsDead()) return RE::BSContainer::ForEachResult::kContinue;
                if (actor->IsPlayerTeammate()) return RE::BSContainer::ForEachResult::kContinue;

                swingCooldowns[a_ref.GetFormID()] = now;
                SKSE::log::info("Swing impact: {} (dist={:.0f}, speed={:.2f})",
                    actor->GetDisplayFullName(), dist, springSpeed);

                RE::NiPoint3 dir = refPos - thrownPos;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }

                if (actor->IsInRagdollState()) {
                    auto allBodies = CollectAllRigidBodies(actor);
                    if (!allBodies.empty()) {
                        RE::hkVector4 impulseHK(dir.x * impactForce, dir.y * impactForce, dir.z * impactForce, 0.0f);
                        auto bhkWorld = cell->GetbhkWorld();
                        if (bhkWorld) {
                            RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                            for (auto* body : allBodies) {
                                if (body) {
                                    float mass = body->motion.GetMass();
                                    if (mass > 0.001f) ApplyClampedImpulse(body, impulseHK * mass, ragdollMaxVelocity);
                                }
                            }
                        }
                    }
                } else {
                    auto caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
                    if (caster && impactCloakSpell) {
                        caster->CastSpellImmediate(impactCloakSpell, false, actor, 1.0f, false, 0.0f, nullptr);
                    }
                }

                if (impactDamage > 0.0f) {
                    float thrownDmg = impactDamage * impactDamageThrownMult;
                    actor->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -impactDamage);
                    if (grabbedActor && !grabbedActor->IsDead()) {
                        grabbedActor->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -thrownDmg);
                    }
                }
            } else if (swingImpactStatics) {
                auto root = a_ref.Get3D();
                if (!root) return RE::BSContainer::ForEachResult::kContinue;

                RE::NiPoint3 dir = refPos - thrownPos;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }

                std::vector<RE::hkpRigidBody*> bodies;
                RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_colObj) {
                    if (!a_colObj) return RE::BSVisit::BSVisitControl::kContinue;
                    auto colObj = static_cast<RE::bhkCollisionObject*>(a_colObj);
                    auto rigidBody = colObj->GetRigidBody();
                    if (rigidBody && rigidBody->referencedObject) {
                        auto hkpBody = reinterpret_cast<RE::hkpRigidBody*>(rigidBody->referencedObject.get());
                        if (hkpBody && hkpBody->motion.type == RE::hkpMotion::MotionType::kDynamic) {
                            bodies.push_back(hkpBody);
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });

                if (!bodies.empty()) {
                    swingCooldowns[a_ref.GetFormID()] = now;
                    SKSE::log::info("Swing impact: static {} ({} bodies, dist={:.0f})",
                        a_ref.GetDisplayFullName(), bodies.size(), dist);

                    RE::hkVector4 impulseHK(dir.x * impactForce, dir.y * impactForce, dir.z * impactForce, 0.0f);
                    auto bhkWorld = cell->GetbhkWorld();
                    if (bhkWorld) {
                        RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                        for (auto* body : bodies) {
                            float mass = body->motion.GetMass();
                            if (mass > 0.001f) ApplyClampedImpulse(body, impulseHK * mass, ragdollMaxVelocity);
                        }
                    }
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });
    }

    if (state == State::TrackingImpact) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - impactTrackStart).count();

        if (elapsed > impactDuration) {
            SKSE::log::info("Impact tracking: timeout after {:.2f}s", elapsed);
            impactTrackFormID = 0;
            state = State::None;
            return;
        }

        auto thrownActor = RE::TESForm::LookupByID(impactTrackFormID)->As<RE::Actor>();
        if (!thrownActor) {
            state = State::None;
            return;
        }

        auto thrownBodies = CollectAllRigidBodies(thrownActor);
        float totalSpeed = 0.0f;
        int velCount = 0;
        for (auto* body : thrownBodies) {
            if (body) {
                auto& lv = body->motion.linearVelocity;
                float s = lv.quad.m128_f32[0] * lv.quad.m128_f32[0] +
                          lv.quad.m128_f32[1] * lv.quad.m128_f32[1] +
                          lv.quad.m128_f32[2] * lv.quad.m128_f32[2];
                totalSpeed += std::sqrt(s);
                velCount++;
            }
        }
        float avgSpeed = velCount > 0 ? totalSpeed / velCount : 0.0f;

        auto thrownPos = thrownActor->GetPosition();
        auto thrown3D = thrownActor->Get3D();
        if (thrown3D) {
            auto& worldPos = thrown3D->world;
            thrownPos.x = worldPos.translate.x;
            thrownPos.y = worldPos.translate.y;
            thrownPos.z = worldPos.translate.z;
        }

        SKSE::log::info("Impact tracking: ragdollCenter=({:.0f},{:.0f},{:.0f}) actorPos=({:.0f},{:.0f},{:.0f}) avgSpeed={:.4f}",
            thrownPos.x, thrownPos.y, thrownPos.z,
            thrownActor->GetPosition().x, thrownActor->GetPosition().y, thrownActor->GetPosition().z,
            avgSpeed);

        if (avgSpeed < impactMinVelocity) {
            SKSE::log::info("Impact tracking: NPC stopped (avgSpeed={:.4f} HK)", avgSpeed);
            impactTrackFormID = 0;
            state = State::None;
            return;
        }

        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        processLists->ForAllActors([&](RE::Actor& actor) {
            if (actor.GetFormID() == impactTrackFormID) return RE::BSContainer::ForEachResult::kContinue;
            if (actor.IsPlayerRef()) return RE::BSContainer::ForEachResult::kContinue;
            if (actor.IsDead()) return RE::BSContainer::ForEachResult::kContinue;
            if (actor.IsPlayerTeammate()) return RE::BSContainer::ForEachResult::kContinue;

            auto actorPos = actor.GetPosition();
            float dx = thrownPos.x - actorPos.x;
            float dy = thrownPos.y - actorPos.y;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < impactRadius && impactHitActors.find(actor.GetFormID()) == impactHitActors.end()) {
                impactHitActors.insert(actor.GetFormID());
                SKSE::log::info("Impact: {} hit by thrown NPC (dist={:.0f}, speed={:.4f})",
                    actor.GetDisplayFullName(), dist, avgSpeed);

                if (impactDamage > 0.0f) {
                    float thrownDmg = impactDamage * impactDamageThrownMult;
                    actor.AsActorValueOwner()->RestoreActorValue(
                        RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -impactDamage);
                    if (thrownActor && !thrownActor->IsDead()) {
                        thrownActor->AsActorValueOwner()->RestoreActorValue(
                            RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -thrownDmg);
                    }
                    SKSE::log::info("  Impact damage: {:.1f} (hit), {:.1f} (thrown)", impactDamage, thrownDmg);
                }

                RE::NiPoint3 dir = actorPos - thrownPos;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 0.001f) {
                    dir.x /= len; dir.y /= len; dir.z /= len;
                }

                if (actor.IsDead() || actor.IsInRagdollState()) {
                    auto allBodies = CollectAllRigidBodies(&actor);
                    if (!allBodies.empty()) {
                        RE::hkVector4 impulseHK(
                            dir.x * impactForce,
                            dir.y * impactForce,
                            dir.z * impactForce,
                            0.0f);

                        auto cell = thrownActor->GetParentCell();
                        auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
                        if (bhkWorld) {
                            RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                            for (auto* body : allBodies) {
                                if (body) {
                                    float mass = body->motion.GetMass();
                                    if (mass > 0.001f) {
                                        ApplyClampedImpulse(body, impulseHK * mass, ragdollMaxVelocity);
                                    }
                                }
                            }
                            SKSE::log::info("  Applied ragdoll impulse to {} bodies", allBodies.size());
                        }
                    }
                } else {
                    SKSE::log::info("  Standing actor hit, casting PushActorAway spell");
                    auto player = RE::PlayerCharacter::GetSingleton();
                    if (player && impactCloakSpell) {
                        auto caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
                        if (caster) {
                            caster->CastSpellImmediate(impactCloakSpell, false, &actor, 1.0f, false, 0.0f, nullptr);
                        }
                    }
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
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

    if (a_heldDuration < throwDropWindow) {
        SKSE::log::info("Dropping ({:.2f}s)", a_heldDuration);

        RE::hkVector4 springBodyVel;
        bool hasSpringVel = false;
        if (player) {
            auto& grabSpring = player->GetPlayerRuntimeData().grabSpring;
            for (auto& springRef : grabSpring) {
                if (!springRef) continue;
                auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
                if (!bhkObj || !bhkObj->referencedObject) continue;
                auto actionBase = reinterpret_cast<std::uintptr_t>(bhkObj->referencedObject.get());
                auto entityPtr = *reinterpret_cast<RE::hkpEntity**>(actionBase + 0x30);
                if (!entityPtr) continue;
                auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(entityPtr);
                springBodyVel = hkpRigidBody->motion.linearVelocity;
                hasSpringVel = true;
                break;
            }
        }

        auto allBodies = CollectAllRigidBodies(grabbedActor);

        if (player) {
            player->DestroyMouseSprings();
            player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        }
        if (grabbedActor) {
            grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
            grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
        }

        if (hasSpringVel && !allBodies.empty()) {
            auto capturedBodies = allBodies;
            auto capturedVel = springBodyVel;
            SKSE::GetTaskInterface()->AddTask([capturedBodies, capturedVel]() {
                auto p = RE::PlayerCharacter::GetSingleton();
                if (!p) return;
                auto cell = p->GetParentCell();
                auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
                if (!bhkWorld) return;
                RE::BSWriteLockGuard locker(bhkWorld->worldLock);

                for (auto* body : capturedBodies) {
                    if (body) {
                        body->motion.SetLinearVelocity(capturedVel);
                    }
                }
            });
        }

        if (grabbedActor && impactOnDrop) {
            impactTrackFormID = grabbedActor->GetFormID();
            impactTrackStart = std::chrono::steady_clock::now();
            impactHitActors.clear();
            SKSE::log::info("  Starting impact tracking for {:08X}", impactTrackFormID);
        }
        grabbedActor = nullptr;
        if (impactOnDrop) {
            state = State::TrackingImpact;
        } else {
            state = State::None;
        }
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
        impactTrackFormID = grabbedActor->GetFormID();
        impactTrackStart = std::chrono::steady_clock::now();
        impactHitActors.clear();
        SKSE::log::info("  Starting impact tracking for {:08X} (throw)", impactTrackFormID);
    }
    grabbedActor = nullptr;
    state = State::TrackingImpact;
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
    SKSE::log::info("TryGrabWithSpell: target={} ({:08X})", target->GetDisplayFullName(), targetFormID);
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

        caster->CastSpellImmediate(grabSpell, false, player, 1.0f, false, 0.0f, player);
    });
}

#include "DragHandler.h"
#include "Hooks.h"

#include <cstdlib>
#include <cstdio>
#include <ctime>
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
        return static_cast<int>(std::strtol(val.c_str(), nullptr, 0));
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

        return bodies;
    }
    RE::hkpRigidBody* GetSpringBody(RE::PlayerCharacter* a_player)
    {
        if (!a_player) return nullptr;
        auto& grabSpring = a_player->GetPlayerRuntimeData().grabSpring;
        for (auto& springRef : grabSpring) {
            if (!springRef) continue;
            auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
            if (!bhkObj || !bhkObj->referencedObject) continue;
            auto actionBase = reinterpret_cast<std::uintptr_t>(bhkObj->referencedObject.get());
            auto entityPtr = *reinterpret_cast<RE::hkpEntity**>(actionBase + 0x30);
            if (entityPtr) return reinterpret_cast<RE::hkpRigidBody*>(entityPtr);
        }
        return nullptr;
    }

    std::uintptr_t GetSpringActionBase(RE::PlayerCharacter* a_player)
    {
        if (!a_player) return 0;
        auto& grabSpring = a_player->GetPlayerRuntimeData().grabSpring;
        for (auto& springRef : grabSpring) {
            if (!springRef) continue;
            auto bhkObj = reinterpret_cast<RE::bhkRefObject*>(springRef.get());
            if (!bhkObj || !bhkObj->referencedObject) continue;
            return reinterpret_cast<std::uintptr_t>(bhkObj->referencedObject.get());
        }
        return 0;
    }

    void PlaySoundForm(RE::FormID a_formID)
    {
        if (a_formID == 0) return;
        auto* descriptor = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(a_formID);
        if (!descriptor) {
            SKSE::log::warn("PlaySoundForm: LookupByID<SoundDescriptorForm>(0x{:08X}) returned null", a_formID);
            return;
        }
        auto* audioMgr = RE::BSAudioManager::GetSingleton();
        if (!audioMgr) {
            SKSE::log::warn("PlaySoundForm: BSAudioManager is null");
            return;
        }
        RE::BSSoundHandle handle;
        audioMgr->BuildSoundDataFromDescriptor(handle, descriptor, 0x1A);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* node3D = player->Get3D();
            if (node3D) {
                handle.SetObjectToFollow(node3D);
            } else {
                handle.SetPosition(player->GetPosition());
            }
        }
        handle.SetVolume(1.0f);
        bool played = handle.Play();
        SKSE::log::info("PlaySoundForm: 0x{:08X} played={}", a_formID, played);
    }
}

bool DragHandler::LoadSettings()
{
    auto iniPath = GetINIPath();
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    SKSE::log::info("Loading settings from: {}", iniPath);

    enabled = GetINIBool(iniPath, "General", "bEnableMod", true);
    enableLogging = GetINIBool(iniPath, "General", "bEnableLogging", false);
    showNotifications = GetINIBool(iniPath, "General", "bShowNotifications", true);
    actionKey = static_cast<uint32_t>(GetINIInt(iniPath, "General", "iActionKey", 0x22));
    bEnableGKeyGrab = GetINIBool(iniPath, "General", "bEnableGKeyGrab", true);
    useShoutKeyForRelease = GetINIBool(iniPath, "General", "bUseShoutKeyForRelease", true);

    grabRange = GetINIFloat(iniPath, "Grab", "fGrabRange", 150.0f);
    grabHoldTimeout = GetINIFloat(iniPath, "Grab", "fGrabHoldTimeout", 0.5f);
    grabAnyone = GetINIBool(iniPath, "Grab", "bGrabAnyone", false);
    grabFollowers = GetINIBool(iniPath, "Grab", "bGrabFollowers", true);
    grabChildren = GetINIBool(iniPath, "Grab", "bGrabChildren", false);
    grabHostile = GetINIBool(iniPath, "Grab", "bGrabHostile", false);
    blockTwoHanded = GetINIBool(iniPath, "Grab", "bBlockTwoHanded", true);
    blockUnsheathed = GetINIBool(iniPath, "Grab", "bBlockUnsheathed", false);

    dragSpeedMult = GetINIFloat(iniPath, "Drag", "fDragSpeedMult", 3.0f);
    noSpeedPenalty = GetINIBool(iniPath, "Drag", "bNoSpeedPenalty", true);
    noSprint = GetINIBool(iniPath, "Drag", "bNoSprintWhileDragging", true);
    staminaDrainRate = GetINIFloat(iniPath, "Drag", "fStaminaDrainRate", 5.0f);
    dragMaxVelocity = GetINIFloat(iniPath, "Drag", "fDragMaxVelocity", 5.0f);
    grabTetherDist = GetINIFloat(iniPath, "Drag", "fGrabTetherDist", 600.0f);
    dropOnHitChance = GetINIFloat(iniPath, "Drag", "fDropOnHitChance", 100.0f);
    dropOnProjectileChance = GetINIFloat(iniPath, "Drag", "fDropOnProjectileChance", 100.0f);
    chargeThrowOnHold = GetINIBool(iniPath, "Drag", "bChargeThrowOnHold", false);

    throwImpulseMax = GetINIFloat(iniPath, "Throw", "fThrowImpulseMax", 10.0f);
    throwDropWindow = GetINIFloat(iniPath, "Throw", "fThrowDropWindow", 0.5f);
    throwTimeToMax = GetINIFloat(iniPath, "Throw", "fThrowTimeToMax", 4.0f);

    springDamping = GetINIFloat(iniPath, "Physics", "fSpringDamping", 1.5f);
    springElasticity = GetINIFloat(iniPath, "Physics", "fSpringElasticity", 0.05f);
    springMaxForce = GetINIFloat(iniPath, "Physics", "fSpringMaxForce", 500.0f);

    grabFailSoundForm = static_cast<RE::FormID>(GetINIInt(iniPath, "Sound", "iGrabFailSound", 0));
    grabSoundForm = static_cast<RE::FormID>(GetINIInt(iniPath, "Sound", "iGrabSound", 0));
    dropSoundForm = static_cast<RE::FormID>(GetINIInt(iniPath, "Sound", "iDropSound", 0));
    throwSoundForm = static_cast<RE::FormID>(GetINIInt(iniPath, "Sound", "iThrowSound", 0));

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
    impactForceSpeedScale = GetINIFloat(iniPath, "Impact", "fImpactForceSpeedScale", 1.0f);
    impactDamageSpeedScale = GetINIFloat(iniPath, "Impact", "fImpactDamageSpeedScale", 1.0f);

    SKSE::log::info("Settings: enabled={}, logging={}, notifications={}, actionKey=0x{:02X}, gKeyGrab={}, shoutKey={}",
        enabled, enableLogging, showNotifications, actionKey, bEnableGKeyGrab, useShoutKeyForRelease);
    SKSE::log::info("  grab: range={:.0f}, holdTimeout={:.2f}, anyone={}, followers={}, children={}, hostile={}, twoHanded={}, unsheathed={}",
        grabRange, grabHoldTimeout, grabAnyone, grabFollowers, grabChildren, grabHostile, blockTwoHanded, blockUnsheathed);
    SKSE::log::info("  drag: speedMult={:.1f}, noSpeedPenalty={}, noSprint={}, staminaDrain={:.1f}, dragMaxVel={:.1f}, tetherDist={:.0f}",
        dragSpeedMult, noSpeedPenalty, noSprint, staminaDrainRate, dragMaxVelocity, grabTetherDist);
    SKSE::log::info("  drop: hitChance={:.1f}, projChance={:.1f}, chargeOnHold={}",
        dropOnHitChance, dropOnProjectileChance, chargeThrowOnHold);
    SKSE::log::info("  throw: impulseMax={:.1f}, dropWindow={:.2f}, timeToMax={:.1f}",
        throwImpulseMax, throwDropWindow, throwTimeToMax);
    SKSE::log::info("  physics: damping={:.2f}, elasticity={:.3f}, maxForce={:.0f}",
        springDamping, springElasticity, springMaxForce);
    SKSE::log::info("  impact: radius={:.0f}, duration={:.1f}, minVel={:.2f}, force={:.0f}",
        impactRadius, impactDuration, impactMinVelocity, impactForce);
    SKSE::log::info("  sounds: grabFail=0x{:08X}, grab=0x{:08X}, drop=0x{:08X}, throw=0x{:08X}",
        grabFailSoundForm, grabSoundForm, dropSoundForm, throwSoundForm);

    return true;
}

void DragHandler::OnDataLoad()
{
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        grabSpell = dataHandler->LookupForm<RE::SpellItem>(0x800, "DragAndDrop.esp");
        SKSE::log::info("Data loaded, grab spell: {:p}", (void*)grabSpell);

        impactPushSpell = dataHandler->LookupForm<RE::SpellItem>(0x808, "DragAndDrop.esp");
        SKSE::log::info("Data loaded, impact spell: {:p}", (void*)impactPushSpell);
    } else {
        SKSE::log::warn("Data loaded, TESDataHandler not available");
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (player && grabSpell) {
        player->AddSpell(grabSpell);
        SKSE::log::info("Added grab spell to player via AddSpell");
    }

    auto holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (holder) {
        holder->AddEventSink<RE::TESHitEvent>(this);
        SKSE::log::info("Registered hit event sink");
    }
}

bool DragHandler::IsValidTarget(RE::Actor* a_actor) const
{
    if (!a_actor || a_actor->IsPlayerRef()) return false;

    auto player = RE::PlayerCharacter::GetSingleton();

    if (blockUnsheathed && player && player->AsActorState()->IsWeaponDrawn()) return false;

    if (blockTwoHanded) {
        if (player && player->AsActorState()->IsWeaponDrawn()) {
            for (bool left : {false, true}) {
                auto* obj = player->GetEquippedObject(left);
                if (obj) {
                    auto* weap = obj->As<RE::TESObjectWEAP>();
                    if (weap) {
                        auto type = weap->GetWeaponType();
                        if (type == RE::WEAPON_TYPE::kTwoHandSword ||
                            type == RE::WEAPON_TYPE::kTwoHandAxe ||
                            type == RE::WEAPON_TYPE::kBow ||
                            type == RE::WEAPON_TYPE::kCrossbow) {
                            return false;
                        }
                    }
                }
            }
        }
    }

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

    bool isHostile = player && a_actor->IsHostileToActor(player);

    SKSE::log::info("IsValidTarget: {} dead={} para={} hostile={} follower={} grabHostile={} grabFollowers={}",
        a_actor->GetDisplayFullName(), isDead, isParalyzed, isHostile, isFollower, grabHostile, grabFollowers);

    if (grabHostile && isHostile) return true;
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
        if (showNotifications) RE::DebugNotification("Too exhausted to keep holding");
        DoRelease(0.0f);
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

RE::BSEventNotifyControl DragHandler::ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*)
{
    if (!a_event || !a_event->target) return RE::BSEventNotifyControl::kContinue;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event->target.get()->GetFormID() != player->GetFormID()) return RE::BSEventNotifyControl::kContinue;

    if (state == State::Dragging && grabbedActor) {
        bool isProjectile = a_event->projectile != 0;
        if (!isProjectile && a_event->source != 0) {
            auto* sourceForm = RE::TESForm::LookupByID(a_event->source);
            if (sourceForm) {
                auto* weapon = sourceForm->As<RE::TESObjectWEAP>();
                if (weapon) {
                    auto wpnType = weapon->GetWeaponType();
                    isProjectile = (wpnType == RE::WEAPON_TYPE::kBow || wpnType == RE::WEAPON_TYPE::kCrossbow);
                } else {
                    auto* ammo = sourceForm->As<RE::TESAmmo>();
                    if (ammo) isProjectile = true;
                }
            }
        }
        SKSE::log::info("Player hit: source=0x{:08X} projectile=0x{:08X} isProjectile={} flags={}", a_event->source, a_event->projectile, isProjectile, static_cast<uint32_t>(a_event->flags.get()));
        float chance = isProjectile ? dropOnProjectileChance : dropOnHitChance;
        float roll = static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX) / 100.0f);
        if (roll >= chance) {
            SKSE::log::info("Player hit while dragging, chance roll {:.1f} >= {:.1f} ({}) — no drop", roll, chance, isProjectile ? "projectile" : "melee");
            return RE::BSEventNotifyControl::kContinue;
        }
        SKSE::log::info("Player hit while dragging, chance roll {:.1f} < {:.1f} ({}) — dropping", roll, chance, isProjectile ? "projectile" : "melee");

        RE::hkVector4 springBodyVel;
        bool hasSpringVel = false;
        auto* springBody = GetSpringBody(player);
        if (springBody) {
            springBodyVel = springBody->motion.linearVelocity;
            hasSpringVel = true;
        }

        auto allBodies = CollectAllRigidBodies(grabbedActor);
        auto formID = grabbedActor->GetFormID();

        actionKeyHeld = false;
        actionNotified = false;
        spellCastDetected = false;
        grabKeyHeld = false;

        auto capturedBodies = allBodies;
        auto capturedVel = springBodyVel;
        bool capturedHasVel = hasSpringVel;

        SKSE::GetTaskInterface()->AddTask([this, formID, capturedBodies, capturedVel, capturedHasVel]() {
            if (state != State::Dragging) return;
            auto actor = RE::TESForm::LookupByID(formID)->As<RE::Actor>();
            if (!actor || !grabbedActor || grabbedActor->GetFormID() != formID) return;

            auto p = RE::PlayerCharacter::GetSingleton();
            if (!p) return;

            p->DestroyMouseSprings();
            p->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
            grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
            grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);

            if (capturedHasVel && !capturedBodies.empty()) {
                auto cell = p->GetParentCell();
                auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
                if (bhkWorld) {
                    RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                    for (auto* body : capturedBodies) {
                        if (body) body->motion.SetLinearVelocity(capturedVel);
                    }
                }
            }

            RestoreSpeed(p);
            grabbedActor = nullptr;
            state = State::None;
            SKSE::log::info("Player hit drop complete (deferred)");
        });
    }

    return RE::BSEventNotifyControl::kContinue;
}

void DragHandler::UpdateGrabState()
{
    if (!enabled) return;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    if (state == State::None) HandleNewGrab(player);
    if (state == State::Dragging) HandleDragFrame(player);
    if (state == State::Dragging) HandleSwingImpact(player);
    if (state == State::TrackingImpact) HandleImpactTracking();
}

void DragHandler::HandleNewGrab(RE::PlayerCharacter* a_player)
{
    if (!a_player->IsGrabbing()) return;

    auto grabbedRef = a_player->GetGrabbedRef();
    SKSE::log::info("HandleNewGrab: IsGrabbing=true, grabbedRef={} ({:08X})",
        grabbedRef ? grabbedRef->GetDisplayFullName() : "null",
        grabbedRef ? grabbedRef->GetFormID() : 0);
    if (!grabbedRef) return;

    grabbedActor = grabbedRef->As<RE::Actor>();
    if (grabbedActor && IsValidTarget(grabbedActor)) {
        state = State::Dragging;
        grabStartTime = std::chrono::steady_clock::now();
        ApplySpeedBoost(a_player);

        auto actionBase = GetSpringActionBase(a_player);
        if (actionBase) {
            *reinterpret_cast<float*>(actionBase + 0x60) = springDamping;
            *reinterpret_cast<float*>(actionBase + 0x64) = springElasticity;
            *reinterpret_cast<float*>(actionBase + 0x68) = springMaxForce;
        }

        auto cell = a_player->GetParentCell();
        auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
        if (bhkWorld) {
            RE::BSWriteLockGuard locker(bhkWorld->worldLock);
            auto allBodies = CollectAllRigidBodies(grabbedActor);
            for (auto* body : allBodies) {
                body->motion.SetLinearVelocity(RE::hkVector4());
                body->motion.SetAngularVelocity(RE::hkVector4());
            }
        }
    } else if (grabbedActor) {
        SKSE::log::info("HandleNewGrab: engine grab rejected, releasing");
        auto allBodies = CollectAllRigidBodies(grabbedActor);
        a_player->DestroyMouseSprings();
        if (!allBodies.empty()) {
            auto cell = a_player->GetParentCell();
            auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
            if (bhkWorld) {
                RE::BSWriteLockGuard locker(bhkWorld->worldLock);
                for (auto* body : allBodies) {
                    if (body) {
                        body->motion.SetLinearVelocity(RE::hkVector4());
                        body->motion.SetAngularVelocity(RE::hkVector4());
                    }
                }
            }
        }
        a_player->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        a_player->GetPlayerRuntimeData().grabbedObject = RE::ActorHandle();
        grabbedActor = nullptr;
    }
}

void DragHandler::HandleDragFrame(RE::PlayerCharacter* a_player)
{
    if (noSprint) {
        float stamina = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
        if (stamina > 0.0f) {
            a_player->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -stamina);
        }
    }

    if (staminaDrainRate > 0.0f) {
        auto now = std::chrono::steady_clock::now();
        if (lastFrameTime.time_since_epoch().count() > 0) {
            float dt = std::chrono::duration<float>(now - lastFrameTime).count();
            DrainStamina(dt);
        }
        lastFrameTime = now;
    } else {
        lastFrameTime = std::chrono::steady_clock::time_point{};
    }

    if (!a_player->IsGrabbing()) {
        RestoreSpeed(a_player);
        grabbedActor = nullptr;
        state = State::None;
        actionKeyHeld = false;
        actionNotified = false;
        spellCastDetected = false;
        swingCooldowns.clear();
        return;
    }

    bool isCharging = actionKeyHeld || spellCastDetected || (grabKeyHeld && chargeThrowOnHold);
    if (isCharging && !actionNotified) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point startTime;
        if (grabKeyHeld && chargeThrowOnHold) startTime = grabKeyTime;
        else if (actionKeyHeld) startTime = actionKeyTime;
        else startTime = spellCastTime;
        float elapsed = std::chrono::duration<float>(now - startTime).count();
        float threshold = (grabKeyHeld && chargeThrowOnHold) ? (grabHoldTimeout + throwDropWindow) : throwDropWindow;
        if (elapsed >= threshold) {
            actionNotified = true;
            if (showNotifications) RE::DebugNotification("Ready to throw!");
        }
    }

    if (!grabbedActor) return;

    auto grabbed3D = grabbedActor->Get3D();
    if (grabbed3D) {
        auto npcPos = grabbed3D->world.translate;
        auto playerPos = a_player->Get3D() ? a_player->Get3D()->world.translate : a_player->GetPosition();
        float dx = npcPos.x - playerPos.x;
        float dy = npcPos.y - playerPos.y;
        float dz = npcPos.z - playerPos.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist > grabTetherDist) {
            SKSE::log::info("NPC exceeded tether distance ({:.0f} > {:.0f}), dropping", dist, grabTetherDist);
            ReleaseNPC(false, 0.0f);
            return;
        }
    }

    auto cell = a_player->GetParentCell();
    auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
    if (bhkWorld && dragMaxVelocity > 0.0f) {
        RE::hkpRigidBody* springBody = GetSpringBody(a_player);

        RE::BSWriteLockGuard locker(bhkWorld->worldLock);
        auto allBodies = CollectAllRigidBodies(grabbedActor);
        for (auto* body : allBodies) {
            if (!body || body == springBody) continue;
            auto& vel = body->motion.linearVelocity;
            float speed = std::sqrt(vel.quad.m128_f32[0] * vel.quad.m128_f32[0] +
                                    vel.quad.m128_f32[1] * vel.quad.m128_f32[1] +
                                    vel.quad.m128_f32[2] * vel.quad.m128_f32[2]);
            if (speed > dragMaxVelocity && speed > 0.001f) {
                float scale = dragMaxVelocity / speed;
                vel.quad.m128_f32[0] *= scale;
                vel.quad.m128_f32[1] *= scale;
                vel.quad.m128_f32[2] *= scale;
            }
        }
    }
}

void DragHandler::HandleSwingImpact(RE::PlayerCharacter* a_player)
{
    if (!grabbedActor) return;

    auto now = std::chrono::steady_clock::now();
    float grabElapsed = std::chrono::duration<float>(now - grabStartTime).count();
    if (grabElapsed < 0.5f) return;

    auto thrown3D = grabbedActor->Get3D();
    if (!thrown3D) return;
    RE::NiPoint3 thrownPos = thrown3D->world.translate;

    float springSpeed = 0.0f;
    auto* springBody = GetSpringBody(a_player);
    if (springBody) {
        auto& lv = springBody->motion.linearVelocity;
        springSpeed = std::sqrt(lv.quad.m128_f32[0] * lv.quad.m128_f32[0] +
                                lv.quad.m128_f32[1] * lv.quad.m128_f32[1] +
                                lv.quad.m128_f32[2] * lv.quad.m128_f32[2]);
    }

    if (springSpeed < impactMinVelocity) return;

    if (swingImpactRadiusMult <= 0.0f) return;
    float swingRadius = impactRadius * swingImpactRadiusMult;

    auto cell = a_player->GetParentCell();
    if (!cell) return;

    cell->ForEachReferenceInRange(thrownPos, swingRadius, [&](RE::TESObjectREFR& a_ref) {
        if (a_ref.GetFormID() == a_player->GetFormID()) return RE::BSContainer::ForEachResult::kContinue;
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

            float swingSpeedMult = springSpeed * impactForceSpeedScale;

            RE::NiPoint3 dir = refPos - thrownPos;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }

            if (actor->IsInRagdollState()) {
                auto allBodies = CollectAllRigidBodies(actor);
                if (!allBodies.empty()) {
                    float scaledForce = impactForce * swingSpeedMult;
                    RE::hkVector4 impulseHK(dir.x * scaledForce, dir.y * scaledForce, dir.z * scaledForce, 0.0f);
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
                auto caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
                if (caster && impactPushSpell) {
                    caster->CastSpellImmediate(impactPushSpell, false, actor, 1.0f, false, 0.0f, nullptr);
                }
            }

            if (impactDamage > 0.0f) {
                float dmgScale = 1.0f + (springSpeed * impactDamageSpeedScale);
                float hitDmg = impactDamage * dmgScale;
                float thrownDmg = impactDamage * impactDamageThrownMult * dmgScale;
                actor->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -hitDmg);
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

void DragHandler::HandleImpactTracking()
{
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

            float speedMult = avgSpeed * impactForceSpeedScale;

            if (impactDamage > 0.0f) {
                float dmgScale = 1.0f + (avgSpeed * impactDamageSpeedScale);
                float hitDmg = impactDamage * dmgScale;
                float thrownDmg = impactDamage * impactDamageThrownMult * dmgScale;
                actor.AsActorValueOwner()->RestoreActorValue(
                    RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -hitDmg);
                if (thrownActor && !thrownActor->IsDead()) {
                    thrownActor->AsActorValueOwner()->RestoreActorValue(
                        RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -thrownDmg);
                }
                SKSE::log::info("  Impact damage: {:.1f} (hit), {:.1f} (thrown), speedScale={:.2f}",
                    hitDmg, thrownDmg, dmgScale);
            }

            RE::NiPoint3 dir = actorPos - thrownPos;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 0.001f) {
                dir.x /= len; dir.y /= len; dir.z /= len;
            }

            if (actor.IsDead() || actor.IsInRagdollState()) {
                auto allBodies = CollectAllRigidBodies(&actor);
                if (!allBodies.empty()) {
                    float scaledForce = impactForce * speedMult;
                    RE::hkVector4 impulseHK(
                        dir.x * scaledForce,
                        dir.y * scaledForce,
                        dir.z * scaledForce,
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
                if (player && impactPushSpell) {
                    auto caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
                    if (caster) {
                        caster->CastSpellImmediate(impactPushSpell, false, &actor, 1.0f, false, 0.0f, nullptr);
                    }
                }
            }
        }
        return RE::BSContainer::ForEachResult::kContinue;
    });
}

void DragHandler::OnKeyDown(uint32_t a_key, const char* a_userEvent)
{
    if (!enabled) return;

    if (state == State::Dragging && useShoutKeyForRelease && strcmp(a_userEvent, "Shout") == 0 && !spellCastDetected) {
        spellCastDetected = true;
        spellCastTime = std::chrono::steady_clock::now();
        SKSE::log::info("Power/Shout key down while dragging, charging throw");
        return;
    }

    if (a_key != actionKey) return;

    if (state == State::None && bEnableGKeyGrab) {
        grabKeyHeld = true;
        grabKeyTime = std::chrono::steady_clock::now();
        SKSE::log::info("G-key down: calling TryGrabWithSpell");
        TryGrabWithSpell();
    } else if (state == State::Dragging && !actionKeyHeld) {
        actionKeyHeld = true;
        actionKeyTime = std::chrono::steady_clock::now();
        SKSE::log::info("Action key down (0x{:02X}), charging throw", a_key);
    }
}

void DragHandler::OnKeyUp(uint32_t a_key, const char* a_userEvent)
{
    bool isShoutUp = useShoutKeyForRelease && (strcmp(a_userEvent, "Shout") == 0) && spellCastDetected;
    bool isActionUp = (a_key == actionKey);

    if (!isShoutUp && !isActionUp) return;

    if (isActionUp && grabKeyHeld && state == State::Dragging) {
        grabKeyHeld = false;
        auto now = std::chrono::steady_clock::now();
        float heldDuration = std::chrono::duration<float>(now - grabKeyTime).count();
        if (heldDuration >= grabHoldTimeout) {
            if (chargeThrowOnHold) {
                SKSE::log::info("G-key held {:.2f}s, throw on release", heldDuration);
                DoRelease(heldDuration);
            } else {
                SKSE::log::info("G-key held {:.2f}s (>= {:.2f}s timeout), dropping", heldDuration, grabHoldTimeout);
                DoRelease(0.0f);
            }
        }
        return;
    }

    if (isActionUp) grabKeyHeld = false;

    if (!isShoutUp && !(isActionUp && actionKeyHeld)) return;
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
            auto* springBody = GetSpringBody(player);
            if (springBody) {
                springBodyVel = springBody->motion.linearVelocity;
                hasSpringVel = true;
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
        if (showNotifications) RE::DebugNotification("Dropped");
        PlaySoundForm(dropSoundForm);
        return;
    }

    float force = GetForce(a_heldDuration);
    SKSE::log::info("Throwing (held {:.2f}s, force={:.0f})", a_heldDuration, force);
    ThrowGrabbedObject(a_heldDuration);
    PlaySoundForm(throwSoundForm);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Threw! (%.0f force)", force);
    if (showNotifications) RE::DebugNotification(buf);

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

    RE::hkVector4 springBodyVel;
    bool hasSpringVel = false;
    if (player && !a_throw) {
        auto* springBody = GetSpringBody(player);
        if (springBody) {
            springBodyVel = springBody->motion.linearVelocity;
            hasSpringVel = true;
        }
    }

    if (a_throw && player) {
        ThrowGrabbedObject(a_force > 0.0f ? a_force : 1.0f);
        PlaySoundForm(throwSoundForm);
    } else if (player) {
        player->DestroyMouseSprings();
    }

    if (grabbedActor) {
        grabbedActor->AsMagicTarget()->DispelEffectsWithArchetype(RE::EffectArchetype::kGrabActor, true);
        grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
    }

    if (hasSpringVel && grabbedActor) {
        auto allBodies = CollectAllRigidBodies(grabbedActor);
        if (!allBodies.empty()) {
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
                    if (body) body->motion.SetLinearVelocity(capturedVel);
                }
            });
        }
    }

    SKSE::log::info("Released (throw={}, force={:.1f})", a_throw, a_force);

    if (showNotifications) RE::DebugNotification(a_throw ? "Threw!" : "Released");
    if (!a_throw) PlaySoundForm(dropSoundForm);
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
    if (!target) return;
    if (!IsValidTarget(target)) {
        PlaySoundForm(grabFailSoundForm);
        return;
    }

    auto playerPos = player->GetPosition();
    auto targetPos = target->GetPosition();
    float dx = playerPos.x - targetPos.x;
    float dy = playerPos.y - targetPos.y;
    float dz = playerPos.z - targetPos.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > grabRange) {
        SKSE::log::info("TryGrabWithSpell: target too far ({:.0f} > {:.0f})", dist, grabRange);
        return;
    }

    RE::FormID targetFormID = target->GetFormID();
    SKSE::log::info("TryGrabWithSpell: target={} ({:08X}) dist={:.0f}", target->GetDisplayFullName(), targetFormID, dist);

    SKSE::GetTaskInterface()->AddTask([this, targetFormID]() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto target = RE::TESForm::LookupByID(targetFormID)->As<RE::Actor>();
        if (!target) return;

        player->GetPlayerRuntimeData().grabObjectWeight = 0.0f;
        player->GetPlayerRuntimeData().grabbedObject = target->CreateRefHandle();

        auto caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
        if (!caster) return;

        caster->CastSpellImmediate(grabSpell, false, player, 1.0f, false, 0.0f, player);
        PlaySoundForm(grabSoundForm);
    });
}

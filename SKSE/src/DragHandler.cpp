#include "DragHandler.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/A/Actor.h>
#include <RE/A/ActorValue.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiNode.h>
#include <RE/P/ProcessLists.h>
#include <RE/H/hkpRigidBody.h>
#include <RE/B/bhkRigidBody.h>
#include <RE/M/MouseSpring.h>
#include <RE/C/Color.h>
#include <RE/I/InputEvent.h>
#include <RE/G/GameSettingCollection.h>

namespace
{
    constexpr RE::FormID GHOST_KEYWORD{ 0xD205E };
    constexpr RE::FormID IMMUNE_PARALYSIS_KEYWORD{ 0xF23C5 };
}

bool DragHandler::LoadSettings()
{
    const auto path = "Data/SKSE/Plugins/DragAndDrop.ini";
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(path);

    enabled = ini.GetBoolValue("General", "bEnableMod", true);
    grabRange = static_cast<float>(ini.GetDoubleValue("General", "fGrabRange", 150.0));
    staminaDrainRate = static_cast<float>(ini.GetDoubleValue("General", "fStaminaDrainRate", 5.0));

    fZKeyMaxForce = static_cast<float>(ini.GetDoubleValue("Physics", "fMaxForce", 200.0));
    fZKeySpringDamping = static_cast<float>(ini.GetDoubleValue("Physics", "fSpringDamping", 0.5));
    fZKeySpringElasticity = static_cast<float>(ini.GetDoubleValue("Physics", "fSpringElasticity", 0.2));
    fZKeyObjectDamping = static_cast<float>(ini.GetDoubleValue("Physics", "fObjectDamping", 0.75));
    fZKeyMaxContactDistance = static_cast<float>(ini.GetDoubleValue("Physics", "fMaxContactDistance", 30.0));

    throwImpulseBase = static_cast<float>(ini.GetDoubleValue("Throw", "fBaseThrowForce", 250.0));
    throwImpulseMax = static_cast<float>(ini.GetDoubleValue("Throw", "fMaxThrowForce", 1000.0));
    throwStrengthMult = static_cast<float>(ini.GetDoubleValue("Throw", "fThrowStrengthMult", 500.0));

    ini.SaveFile(path);
    SKSE::log::info("Settings loaded"sv);
    return true;
}

void DragHandler::OnDataLoad()
{
    auto gmst = RE::GameSettingCollection::GetSingleton();
    if (!gmst) return;

    auto set_gmst = [&](const char* name, float value) {
        auto setting = gmst->GetSetting(name);
        if (setting) {
            SKSE::log::info("{}: {} -> {}"sv, name, setting->GetFloat(), value);
            setting->data.f = value;
        }
    };

    set_gmst("fZKeyMaxForce", fZKeyMaxForce);
    set_gmst("fZKeySpringDamping", fZKeySpringDamping);
    set_gmst("fZKeySpringElasticity", fZKeySpringElasticity);
    set_gmst("fZKeyObjectDamping", fZKeyObjectDamping);
    set_gmst("fZKeyMaxContactDistance", fZKeyMaxContactDistance);
}

bool DragHandler::IsValidTarget(RE::Actor* a_actor) const
{
    if (!a_actor || a_actor->IsPlayerRef()) return false;

    if (a_actor->HasKeyword(GHOST_KEYWORD) || a_actor->HasKeyword(IMMUNE_PARALYSIS_KEYWORD)) return false;

    if (a_actor->IsInMidair() || a_actor->IsGhost()) return false;

    bool isDead = a_actor->IsDead();
    bool isParalyzed = a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis) > 0.0f;
    bool isFollower = a_actor->IsPlayerTeammate();

    return isDead || isParalyzed || isFollower;
}

RE::Actor* DragHandler::GetCrosshairActor() const
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nullptr;

    auto crosshairRef = RE::ConsoleLog::GetSingleton();
    auto processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return nullptr;

    RE::Actor* closest = nullptr;
    float closestDist = grabRange;

    auto playerPos = player->GetPosition();

    processLists->ForAllActors([&](RE::Actor& actor) {
        if (!IsValidTarget(&actor)) return RE::BSContainer::ForEachResult::kContinue;

        float dist = playerPos.GetSquaredDistance(actor.GetPosition());
        float sqrtDist = std::sqrt(dist);
        if (sqrtDist < closestDist) {
            if (actor.IsDead() ||
                actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis) > 0.0f ||
                actor.IsPlayerTeammate()) {
                closestDist = sqrtDist;
                closest = &actor;
            }
        }
        return RE::BSContainer::ForEachResult::kContinue;
    });

    return closest;
}

void DragHandler::ApplyRagdoll(RE::Actor* a_actor)
{
    if (!a_actor->IsDead() && a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis) <= 0.0f) {
        a_actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 100.0f);
    }
}

void DragHandler::DrainStamina(float a_dt)
{
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    float currentStamina = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
    float drain = staminaDrainRate * a_dt;
    if (currentStamina - drain <= 0.0f) {
        ReleaseNPC(false, 0.0f);
        RE::DebugNotification("Too exhausted to keep holding");
    } else {
        player->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -drain);
    }
}

bool DragHandler::GrabNPC(RE::Actor* a_target)
{
    if (!enabled || state != State::None) return false;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    if (!IsValidTarget(a_target)) {
        SKSE::log::info("GrabNPC: invalid target"sv);
        return false;
    }

    if (player->IsGrabbing()) {
        SKSE::log::info("GrabNPC: player already grabbing"sv);
        return false;
    }

    ApplyRagdoll(a_target);

    player->StartGrabObject();

    if (!player->IsGrabbing()) {
        SKSE::log::warn("GrabNPC: StartGrabObject failed"sv);
        return false;
    }

    grabbedActor = RE::NiPointer<RE::Actor>(a_target);
    state = State::Dragging;

    std::string name = a_target->GetDisplayFullName();
    RE::DebugNotification(std::format("Dragging {}", name).c_str());

    SKSE::log::info("Grabbed NPC: {}"sv, name);
    return true;
}

bool DragHandler::ReleaseNPC(bool a_throw, float a_force)
{
    if (state == State::None) return false;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    if (a_throw && player->IsGrabbing()) {
        auto cell = player->GetParentCell();
        auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
        if (bhkWorld) {
            RE::BSWriteLockGuard locker(bhkWorld->worldLock);

            float force = (a_force > 0.0f) ? a_force : throwImpulseBase;

            for (const auto& mouseSpring : player->grabSpring) {
                if (mouseSpring && mouseSpring->referencedObject) {
                    auto hkMouseSpring = skyrim_cast<RE::hkpMouseSpringAction*>(mouseSpring->referencedObject.get());
                    if (hkMouseSpring) {
                        auto hkpRigidBody = reinterpret_cast<RE::hkpRigidBody*>(hkMouseSpring->entity);
                        if (hkpRigidBody) {
                            auto mass = hkpRigidBody->motion.GetMass();
                            RE::NiMatrix3 matrix = RE::PlayerCamera::GetSingleton()->cameraRoot->world.rotate;
                            float x = (matrix.entry[0][1] * force) * BS_TO_HK_SCALE;
                            float y = (matrix.entry[1][1] * force) * BS_TO_HK_SCALE;
                            float z = (matrix.entry[2][1] * force) * BS_TO_HK_SCALE;
                            RE::hkVector4 velocity(x, y, z, 0);
                            RE::hkVector4 impulse = velocity * mass;
                            hkpRigidBody->SetLinearVelocity(RE::hkVector4());
                            hkpRigidBody->SetAngularVelocity(RE::hkVector4());
                            hkpRigidBody->ApplyLinearImpulse(impulse);
                        }
                    }
                }
            }
        }
    }

    player->DestroyMouseSprings();

    std::string name = grabbedActor ? grabbedActor->GetDisplayFullName() : "NPC";
    if (a_throw) {
        RE::DebugNotification(std::format("Threw {}", name).c_str());
    } else {
        RE::DebugNotification(std::format("Released {}", name).c_str());
    }

    grabbedActor.reset();
    state = State::None;
    throwHoldTime = 0.0f;

    SKSE::log::info("Released NPC (throw={})"sv, a_throw);
    return true;
}

void DragHandler::OnGrabKeyHeld(float a_heldDuration)
{
    if (state == State::Dragging) {
        DrainStamina(0.016f);
    }
}

void DragHandler::OnGrabKeyReleased()
{
    if (state == State::Dragging || state == State::ReadyToThrow) {
        ReleaseNPC(false, 0.0f);
    }
}

void DragHandler::OnThrowKeyReleased(float a_heldDuration)
{
    if (state != State::Dragging && state != State::ReadyToThrow) return;

    float force = throwImpulseBase + (a_heldDuration * throwStrengthMult);
    if (force > throwImpulseMax) force = throwImpulseMax;

    ReleaseNPC(true, force);
}

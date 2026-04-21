#include "DragHandler.h"

namespace
{
    constexpr RE::FormID GHOST_KEYWORD{ 0xD205E };
    constexpr RE::FormID IMMUNE_PARALYSIS_KEYWORD{ 0xF23C5 };

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
    auto gmst = RE::GameSettingCollection::GetSingleton();
    if (!gmst) return;

    auto set_gmst = [&](const char* name, float value) {
        auto setting = gmst->GetSetting(name);
        if (setting) {
            SKSE::log::info("{}: {} -> {}", name, setting->GetFloat(), value);
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
        SKSE::log::info("GrabNPC: invalid target");
        return false;
    }

    if (player->IsGrabbing()) {
        SKSE::log::info("GrabNPC: player already grabbing");
        return false;
    }

    ApplyRagdoll(a_target);

    player->StartGrabObject();

    if (!player->IsGrabbing()) {
        SKSE::log::warn("GrabNPC: StartGrabObject failed");
        return false;
    }

    grabbedActor = a_target;
    state = State::Dragging;

    std::string name = a_target->GetDisplayFullName();
    RE::DebugNotification(std::format("Dragging {}", name).c_str());

    SKSE::log::info("Grabbed NPC: {}", name);
    return true;
}

bool DragHandler::ReleaseNPC(bool a_throw, float a_force)
{
    if (state == State::None) return false;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    player->DestroyMouseSprings();

    if (a_throw && grabbedActor) {
        auto camera = RE::PlayerCamera::GetSingleton();
        if (camera && camera->cameraRoot) {
            auto dir = camera->cameraRoot->world.rotate;
            float force = (a_force > 0.0f) ? a_force : throwImpulseBase;
            RE::NiPoint3 playerPos = player->GetPosition();
            RE::NiPoint3 throwTarget(
                playerPos.x + dir.entry[0][1] * force,
                playerPos.y + dir.entry[1][1] * force,
                playerPos.z + dir.entry[2][1] * force
            );
            grabbedActor->SetPosition(throwTarget, true);
        }
    }

    std::string name = grabbedActor ? grabbedActor->GetDisplayFullName() : "NPC";
    RE::DebugNotification(std::format("{}", a_throw ? "Threw " + name : "Released " + name).c_str());

    grabbedActor = nullptr;
    state = State::None;
    throwHoldTime = 0.0f;

    SKSE::log::info("Released NPC (throw={})", a_throw);
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

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
    RE::DebugNotification("Drag & Drop v0.5 loaded");
    SKSE::log::info("Data loaded");
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
                SKSE::log::info("Detected grab: {}", name);
            }
        }
    } else if (state == State::Dragging && !player->IsGrabbing()) {
        std::string name = grabbedActor ? grabbedActor->GetDisplayFullName() : "NPC";
        SKSE::log::info("Released: {}", name);
        grabbedActor = nullptr;
        state = State::None;
    }
}

bool DragHandler::ReleaseNPC(bool a_throw, float a_force)
{
    if (state == State::None) return false;
    state = State::None;
    grabbedActor = nullptr;
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
}

void DragHandler::OnThrowKeyReleased(float a_heldDuration)
{
}

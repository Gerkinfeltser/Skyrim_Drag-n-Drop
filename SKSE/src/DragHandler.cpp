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
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    auto& spells = dataHandler->GetFormArray(RE::FormType::Spell);
    RE::SpellItem* grabSpell = nullptr;
    for (auto& form : spells) {
        if (!form) continue;
        auto spell = form->As<RE::SpellItem>();
        if (!spell) continue;
        auto id = spell->GetFormEditorID();
        if (id && strcmp(id, "DragDropGrabSpell") == 0) {
            grabSpell = spell;
            break;
        }
    }

    if (grabSpell) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player && !player->HasSpell(grabSpell)) {
            player->AddSpell(grabSpell);
            SKSE::log::info("Added DragDropGrabSpell to player");
        }
    } else {
        SKSE::log::warn("DragDropGrabSpell not found");
    }

    auto gmst = RE::GameSettingCollection::GetSingleton();
    if (!gmst) return;

    auto set_gmst = [&](const char* name, float value) {
        auto setting = gmst->GetSetting(name);
        if (setting) {
            SKSE::log::info("{}: {} -> {}", name, setting->GetFloat(), value);
            setting->data.f = value;
        }
    };

    set_gmst("fZKeyMaxForce", 500.0f);
    set_gmst("fZKeySpringDamping", 5.0f);
    set_gmst("fZKeySpringElasticity", 0.05f);
    set_gmst("fZKeyObjectDamping", 0.95f);
    set_gmst("fZKeyMaxContactDistance", 300.0f);

    SKSE::log::info("Game settings applied for NPC drag");
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
        ReleaseNPC(false, 0.0f);
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
                SKSE::log::info("Detected grab via spell: {}", name);
            }
        }
    } else if (state == State::Dragging && !player->IsGrabbing()) {
        grabbedActor = nullptr;
        state = State::None;
        throwHoldTime = 0.0f;
    }
}

bool DragHandler::ReleaseNPC(bool a_throw, float a_force)
{
    if (state == State::None) return false;

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    player->DestroyMouseSprings();

    if (grabbedActor) {
        auto pos = grabbedActor->GetPosition();

        grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);

        grabbedActor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 100.0f);

        grabbedActor->SetPosition(pos, true);
    }

    std::string name = grabbedActor ? grabbedActor->GetDisplayFullName() : "NPC";
    RE::DebugNotification(std::format("Released {}", name).c_str());

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

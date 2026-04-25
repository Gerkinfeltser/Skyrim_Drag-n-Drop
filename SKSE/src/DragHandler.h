#pragma once

#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

class DragHandler : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
    static DragHandler* GetSingleton()
    {
        static DragHandler instance;
        return &instance;
    }

    bool LoadSettings();
    void OnDataLoad();

    void UpdateGrabState();
    bool ReleaseNPC(bool a_throw, float a_force);
    RE::Actor* GetGrabbedActor() const { return grabbedActor; }
    bool IsDragging() const { return state != State::None; }
    bool IsLoggingEnabled() const { return enableLogging; }

    void OnKeyDown(uint32_t a_key, const char* a_userEvent = "");
    void OnKeyUp(uint32_t a_key, const char* a_userEvent = "");
    bool IsValidTarget(RE::Actor* a_actor) const;
    RE::Actor* GetCrosshairActor() const;
    void TryGrabWithSpell();

    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override;

    enum class State
    {
        None,
        Dragging,
        TrackingImpact
    };

private:
    void DrainStamina(float a_dt);
    void HandleNewGrab(RE::PlayerCharacter* a_player);
    void HandleDragFrame(RE::PlayerCharacter* a_player);
    void HandleSwingImpact(RE::PlayerCharacter* a_player);
    void HandleImpactTracking();
    void ThrowGrabbedObject(float a_heldDuration);
    RE::hkVector4 GetImpulse(float a_force, float a_mass) const;
    float GetForce(float a_heldDuration) const;
    void ApplySpeedBoost(RE::PlayerCharacter* a_player);
    void RestoreSpeed(RE::PlayerCharacter* a_player);
    void DoRelease(float a_heldDuration);

    static constexpr float BS_TO_HK_SCALE{ 0.0142875f };
    static constexpr float HK_TO_BS_SCALE{ 69.991251f };

    RE::Actor* grabbedActor{ nullptr };
    State state{ State::None };

    bool actionKeyHeld{ false };
    bool actionNotified{ false };
    bool grabKeyHeld{ false };
    std::chrono::steady_clock::time_point actionKeyTime;
    std::chrono::steady_clock::time_point grabKeyTime;

    uint32_t actionKey{ 0x22 };
    bool enabled{ true };
    float grabRange{ 150.0f };
    float staminaDrainRate{ 5.0f };
    bool grabFollowers{ true };
    bool grabChildren{ false };
    bool grabAnyone{ false };
    bool grabHostile{ false };
    bool noSpeedPenalty{ true };
    float dragSpeedMult{ 3.0f };
    float savedSpeedMult{ 0.0f };
    bool useShoutKeyForRelease{ true };
    bool bEnableGKeyGrab{ true };
    float grabHoldTimeout{ 0.5f };
    bool blockTwoHanded{ true };
    bool blockUnsheathed{ false };
    bool chargeThrowOnHold{ false };
    RE::FormID grabFailSoundForm{ 0 };
    RE::FormID grabSoundForm{ 0 };
    RE::FormID throwSoundForm{ 0 };
    RE::FormID dropSoundForm{ 0 };
    bool enableLogging{ false };

    float throwImpulseMax{ 20.0f };
    float throwDropWindow{ 0.2f };
    float throwTimeToMax{ 3.0f };

    RE::SpellItem* grabSpell{ nullptr };
    RE::SpellItem* impactPushSpell{ nullptr };
    bool spellCastDetected{ false };
    std::chrono::steady_clock::time_point spellCastTime;

    std::chrono::steady_clock::time_point grabStartTime;
    std::chrono::steady_clock::time_point lastFrameTime;

    RE::FormID impactTrackFormID{ 0 };
    std::chrono::steady_clock::time_point impactTrackStart;
    float impactRadius{ 150.0f };
    float impactDuration{ 1.5f };
    float impactMinVelocity{ 0.01f };
    float impactForce{ 300.0f };
    float impactPushForceMax{ 5.0f };
    float impactDamage{ 0.0f };
    float impactDamageThrownMult{ 1.0f };
    bool impactOnDrop{ false };
    std::unordered_set<RE::FormID> impactHitActors;
    std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> swingCooldowns;
    float swingImpactCooldown{ 1.0f };
    float swingImpactRadiusMult{ 0.5f };
    bool swingImpactStatics{ true };
    float ragdollMaxVelocity{ 20.0f };
    float dragMaxVelocity{ 5.0f };
    float grabTetherDist{ 600.0f };
    float impactForceSpeedScale{ 1.0f };
    float impactDamageSpeedScale{ 1.0f };
    float dropOnHitChance{ 100.0f };
    float dropOnProjectileChance{ 100.0f };
    bool noSprint{ true };
    bool showNotifications{ true };
    float springDamping{ 1.5f };
    float springElasticity{ 0.05f };
    float springMaxForce{ 1000.0f };
};

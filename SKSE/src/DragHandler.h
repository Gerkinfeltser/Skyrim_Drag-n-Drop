#pragma once

#include <chrono>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

class DragHandler
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

    void OnKeyDown(uint32_t a_key, const char* a_userEvent = "");
    void OnKeyUp(uint32_t a_key, const char* a_userEvent = "");
    bool IsValidTarget(RE::Actor* a_actor) const;
    RE::Actor* GetCrosshairActor() const;

    enum class State
    {
        None,
        Dragging
    };

private:
    void DrainStamina(float a_dt);
    void ZeroGrabbedVelocity(RE::PlayerCharacter* a_player);
    void ThrowGrabbedObject(float a_heldDuration);
    void ForceRagdoll(RE::Actor* a_actor);
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
    std::chrono::steady_clock::time_point actionKeyTime;

    uint32_t actionKey{ 0x22 };
    bool enabled{ true };
    float grabRange{ 150.0f };
    float staminaDrainRate{ 5.0f };
    bool grabFollowers{ true };
    bool grabChildren{ false };
    bool grabAnyone{ false };
    bool noSpeedPenalty{ true };
    float dragSpeedMult{ 3.0f };
    float savedSpeedMult{ 0.0f };

    float throwImpulseMax{ 10.0f };
    float throwDropWindow{ 0.5f };
    float throwTimeToMax{ 4.0f };

    RE::SpellItem* grabSpell{ nullptr };
    bool spellCastDetected{ false };
    std::chrono::steady_clock::time_point spellCastTime;
};

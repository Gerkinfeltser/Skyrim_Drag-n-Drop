#pragma once

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

    void OnKeyDown(uint32_t a_key);
    void OnKeyUp(uint32_t a_key);
    bool IsValidTarget(RE::Actor* a_actor) const;
    RE::Actor* GetCrosshairActor() const;

    enum class State
    {
        None,
        Dragging
    };

private:
    void DrainStamina(float a_dt);
    void ThrowGrabbedObject(float a_heldDuration);
    RE::hkVector4 GetImpulse(float a_force, float a_mass) const;
    float GetForce(float a_heldDuration) const;

    static constexpr float BS_TO_HK_SCALE{ 0.0142875f };
    static constexpr float HK_TO_BS_SCALE{ 69.991251f };

    RE::Actor* grabbedActor{ nullptr };
    State state{ State::None };

    bool rKeyHeld{ false };
    float rKeyHoldTime{ 0.0f };

    bool enabled{ true };
    float staminaDrainRate{ 5.0f };

    float throwImpulseBase{ 250.0f };
    float throwImpulseMax{ 1000.0f };
    float throwStrengthMult{ 500.0f };
    float fZKeyMaxContactDistance{ 30.0f };
    float fZKeyMaxForce{ 175.0f };
    float fZKeyObjectDamping{ 0.75f };
    float fZKeySpringDamping{ 0.5f };
    float fZKeySpringElasticity{ 0.2f };
    float fZKeyHeavyWeight{ 100.0f };
};

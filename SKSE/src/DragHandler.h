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

    bool GrabNPC(RE::Actor* a_target);
    bool ReleaseNPC(bool a_throw, float a_force);
    RE::Actor* GetGrabbedActor() const { return grabbedActor; }
    bool IsDragging() const { return state != State::None; }

    void OnGrabKeyHeld(float a_heldDuration);
    void OnGrabKeyReleased();
    void OnThrowKeyReleased(float a_heldDuration);

    bool IsValidTarget(RE::Actor* a_actor) const;
    RE::Actor* GetCrosshairActor() const;

    enum class State
    {
        None,
        Dragging,
        ReadyToThrow
    };

private:
    void ApplyRagdoll(RE::Actor* a_actor);
    void DrainStamina(float a_dt);

    RE::Actor* grabbedActor{ nullptr };
    State state{ State::None };
    float throwHoldTime{ 0.0f };

    bool enabled{ true };
    float grabRange{ 150.0f };
    float staminaDrainRate{ 5.0f };

    float fZKeyMaxForce{ 200.0f };
    float fZKeySpringDamping{ 0.5f };
    float fZKeySpringElasticity{ 0.2f };
    float fZKeyObjectDamping{ 0.75f };
    float fZKeyMaxContactDistance{ 30.0f };

    float throwImpulseBase{ 250.0f };
    float throwImpulseMax{ 1000.0f };
    float throwStrengthMult{ 500.0f };

    static constexpr float BS_TO_HK_SCALE{ 0.0142875f };
};

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
    void DrainStamina(float a_dt);

    RE::Actor* grabbedActor{ nullptr };
    State state{ State::None };
    float throwHoldTime{ 0.0f };

    bool enabled{ true };
    float staminaDrainRate{ 5.0f };

    float throwImpulseBase{ 250.0f };
    float throwImpulseMax{ 1000.0f };
    float throwStrengthMult{ 500.0f };
};

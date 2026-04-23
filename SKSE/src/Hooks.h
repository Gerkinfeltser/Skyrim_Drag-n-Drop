#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace Hooks
{
    void Install();
    void InstallGrabActorEffectHook();
}

using GrabActorUpdateFn = void(*)(RE::GrabActorEffect*, float);
extern GrabActorUpdateFn g_originalGrabActorUpdate;

void GrabActorEffectUpdate_Hook(RE::GrabActorEffect* a_effect, float a_delta);
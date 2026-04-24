#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace Hooks
{
    void Install();
    void InstallCastSpellHook();
}

using CastSpellImmediateFn = void(*)(RE::MagicCaster* a_this, RE::MagicItem* a_spell, bool a_noHitEffectArt, RE::TESObjectREFR* a_target, float a_effectiveness, bool a_hostileEffectogenicityOnly, float a_magnitudeOverride, RE::Actor* a_blameActor);
extern CastSpellImmediateFn g_originalCastSpellImmediate;

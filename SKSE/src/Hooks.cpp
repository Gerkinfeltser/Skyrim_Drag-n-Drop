#include "Hooks.h"
#include "DragHandler.h"
#include <windows.h>

CastSpellImmediateFn g_originalCastSpellImmediate = nullptr;

void CastSpellImmediate_Hook(RE::MagicCaster* a_this, RE::MagicItem* a_spell, bool a_noHitEffectArt, RE::TESObjectREFR* a_target, float a_effectiveness, bool a_hostileEffectogenicityOnly, float a_magnitudeOverride, RE::Actor* a_blameActor)
{
    constexpr RE::FormID GRAB_SPELL_ID = 0x800;

    if (a_spell) {
        RE::FormID spellID = a_spell->GetFormID();
        SKSE::log::info("CastSpellImmediate hook: spellID={:08X} target={:p}", spellID, (void*)a_target);
    } else {
        SKSE::log::info("CastSpellImmediate hook: no spell");
    }

    if (a_spell && a_spell->GetFormID() == GRAB_SPELL_ID) {
        if (a_target) {
            auto targetActor = a_target->As<RE::Actor>();
            if (targetActor) {
                auto handler = DragHandler::GetSingleton();
                bool valid = handler->IsValidTarget(targetActor);
                SKSE::log::info("CastSpellImmediate hook: grab spell target {:08X}, IsValidTarget={}", targetActor->GetFormID(), valid);
                if (!valid) {
                    return;
                }
            }
        }
    }

    if (g_originalCastSpellImmediate) {
        g_originalCastSpellImmediate(a_this, a_spell, a_noHitEffectArt, a_target, a_effectiveness, a_hostileEffectogenicityOnly, a_magnitudeOverride, a_blameActor);
    }
}

namespace Hooks
{
    struct InputEventSink : public RE::BSTEventSink<RE::InputEvent*>
    {
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_source) override
        {
            if (!a_event || !*a_event) return RE::BSEventNotifyControl::kContinue;

            auto handler = DragHandler::GetSingleton();

            for (auto ev = *a_event; ev; ev = ev->next) {
                if (ev->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;

                auto btn = static_cast<RE::ButtonEvent*>(ev);
                if (btn->IsDown()) {
                    handler->OnKeyDown(btn->idCode, btn->userEvent.c_str());
                } else if (btn->IsUp()) {
                    handler->OnKeyUp(btn->idCode, btn->userEvent.c_str());
                }
            }

            handler->UpdateGrabState();
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    static InputEventSink g_inputSink;

    void Install()
    {
        SKSE::log::info("Installing input event sink");

        auto inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (inputMgr) {
            inputMgr->AddEventSink(&g_inputSink);
            SKSE::log::info("Input event sink installed");
        } else {
            SKSE::log::error("BSInputDeviceManager not found");
        }
    }

    void InstallCastSpellHook()
    {
        auto vtableAddr = RE::VTABLE_MagicCaster[0].address();
        auto vfuncAddr = vtableAddr + 0x08;
        SKSE::log::info("CastSpell hook: VTABLE={:p}, vfunc +0x08={:p}", (void*)vtableAddr, (void*)vfuncAddr);

        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(vfuncAddr), 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            SKSE::log::error("CastSpell hook: VirtualProtect failed");
            return;
        }

        CastSpellImmediateFn originalFn = nullptr;
        SIZE_T bytesWritten = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(vfuncAddr), &originalFn, 8, &bytesWritten)) {
            SKSE::log::error("CastSpell hook: failed to read original");
            VirtualProtect(reinterpret_cast<void*>(vfuncAddr), 8, oldProtect, &oldProtect);
            return;
        }
        if (bytesWritten != 8) {
            SKSE::log::error("CastSpell hook: failed to read original - read {} bytes", bytesWritten);
            VirtualProtect(reinterpret_cast<void*>(vfuncAddr), 8, oldProtect, &oldProtect);
            return;
        }
        g_originalCastSpellImmediate = originalFn;

        auto hookFn = reinterpret_cast<void*>(&CastSpellImmediate_Hook);
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(vfuncAddr), &hookFn, 8, &bytesWritten)) {
            SKSE::log::error("CastSpell hook: WriteProcessMemory failed");
            VirtualProtect(reinterpret_cast<void*>(vfuncAddr), 8, oldProtect, &oldProtect);
            return;
        }

        if (bytesWritten != 8) {
            SKSE::log::error("CastSpell hook: only wrote {} bytes", bytesWritten);
        }

        VirtualProtect(reinterpret_cast<void*>(vfuncAddr), 8, oldProtect, &oldProtect);

        SKSE::log::info("CastSpell hook installed via WriteProcessMemory");
    }
}

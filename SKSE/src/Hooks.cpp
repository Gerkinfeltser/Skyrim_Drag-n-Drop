#include "Hooks.h"
#include "DragHandler.h"
#include <windows.h>

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

    void InstallGrabActorEffectHook()
    {
        auto vtableAddr = RE::VTABLE_GrabActorEffect[0].address();
        auto updateFnAddr = vtableAddr + 0x28;
        SKSE::log::info("Hook: VTABLE={:p}, Update fn offset +0x28, target={:p}", (void*)vtableAddr, (void*)updateFnAddr);

        void* hookTarget = reinterpret_cast<void*>(&GrabActorEffectUpdate_Hook);

        DWORD oldProtect = 0;
        auto result = VirtualProtect(reinterpret_cast<void*>(updateFnAddr), 8, PAGE_EXECUTE_READWRITE, &oldProtect);
        SKSE::log::info("VirtualProtect result={}", result);

        if (result) {
            SIZE_T bytesWritten = 0;
            WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(updateFnAddr),
                &hookTarget, sizeof(hookTarget), &bytesWritten);
            SKSE::log::info("Hook installed via WriteProcessMemory, bytesWritten={}", bytesWritten);

            DWORD temp = 0;
            VirtualProtect(reinterpret_cast<void*>(updateFnAddr), 8, oldProtect, &temp);
        } else {
            SKSE::log::error("VirtualProtect failed on vtable slot, error={}", GetLastError());
        }
    }
}

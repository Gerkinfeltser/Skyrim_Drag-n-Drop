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
}
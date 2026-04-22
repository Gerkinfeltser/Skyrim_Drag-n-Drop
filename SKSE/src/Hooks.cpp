#include "Hooks.h"
#include "DragHandler.h"

namespace Hooks
{
    namespace GrabRelease
    {
        struct ActivateButton
        {
            static void thunk(RE::ActivateHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
            {
                DragHandler::GetSingleton()->UpdateGrabState();
                return func(a_this, a_event, a_data);
            }
            static inline REL::Relocation<decltype(thunk)> func;
            static inline constexpr std::size_t idx = 0x4;
        };

        void Install()
        {
            SKSE::log::info("Installing grab hooks");

            REL::Relocation<std::uintptr_t> activateVtbl{ RE::VTABLE_ActivateHandler[0] };
            ActivateButton::func = activateVtbl.write_vfunc(ActivateButton::idx, ActivateButton::thunk);

            SKSE::log::info("Grab hooks installed");
        }
    }

    void Install()
    {
        SKSE::AllocTrampoline(14);
        GrabRelease::Install();
    }
}

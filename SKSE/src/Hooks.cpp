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
                auto handler = DragHandler::GetSingleton();

                if (handler->IsDragging()) {
                    if (a_event->IsUp()) {
                        handler->OnGrabKeyReleased();
                        return;
                    }
                    return;
                }

                if (a_event->IsDown() && !handler->IsDragging()) {
                    auto target = handler->GetCrosshairActor();
                    if (target) {
                        handler->GrabNPC(target);
                        return;
                    }
                }

                return func(a_this, a_event, a_data);
            }
            static inline REL::Relocation<decltype(thunk)> func;
            static inline constexpr std::size_t idx = 0x4;
        };

        struct ReadyWeaponButton
        {
            static void thunk(RE::ReadyWeaponHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
            {
                auto handler = DragHandler::GetSingleton();

                if (handler->IsDragging()) {
                    if (a_event->IsUp()) {
                        handler->OnThrowKeyReleased(a_event->HeldDuration());
                        return;
                    }
                    return;
                }

                return func(a_this, a_event, a_data);
            }
            static inline REL::Relocation<decltype(thunk)> func;
            static inline constexpr std::size_t idx = 0x4;
        };

        struct BlockAttackInput
        {
            static bool thunk(RE::AttackBlockHandler* a_this, RE::InputEvent* a_event)
            {
                if (DragHandler::GetSingleton()->IsDragging()) {
                    return false;
                }
                return func(a_this, a_event);
            }
            static inline REL::Relocation<decltype(thunk)> func;
            static inline constexpr std::size_t idx = 0x1;
        };

        void Install()
        {
            SKSE::log::info("Installing grab/release hooks");

            REL::Relocation<std::uintptr_t> activateVtbl{ RE::VTABLE_ActivateHandler[0] };
            REL::Relocation<std::uintptr_t> readyVtbl{ RE::VTABLE_ReadyWeaponHandler[0] };
            REL::Relocation<std::uintptr_t> attackVtbl{ RE::VTABLE_AttackBlockHandler[0] };

            ActivateButton::func = activateVtbl.write_vfunc(ActivateButton::idx, ActivateButton::thunk);
            ReadyWeaponButton::func = readyVtbl.write_vfunc(ReadyWeaponButton::idx, ReadyWeaponButton::thunk);
            BlockAttackInput::func = attackVtbl.write_vfunc(BlockAttackInput::idx, BlockAttackInput::thunk);

            SKSE::log::info("Grab/release hooks installed");
        }
    }

    void Install()
    {
        SKSE::AllocTrampoline(14 * 3);
        GrabRelease::Install();
    }
}

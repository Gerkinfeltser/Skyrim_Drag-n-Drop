#include "Hooks.h"
#include "DragHandler.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/I/InputEvent.h>
#include <RE/H/Handlers.h>

namespace Hooks
{
    namespace GrabRelease
    {
        struct ActivateButton
        {
            static void thunk(RE::ActivateHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
            {
                auto player = RE::PlayerCharacter::GetSingleton();
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
            SKSE::log::info("Installing grab/release hooks"sv);
            SKSE::stl::write_vfunc<RE::ActivateHandler, ActivateButton>();
            SKSE::stl::write_vfunc<RE::ReadyWeaponHandler, ReadyWeaponButton>();
            SKSE::stl::write_vfunc<RE::AttackBlockHandler, BlockAttackInput>();
            SKSE::log::info("Grab/release hooks installed"sv);
        }
    }

    void Install()
    {
        SKSE::AllocTrampoline(14 * 3);
        GrabRelease::Install();
    }
}

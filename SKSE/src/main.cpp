#include "DragHandler.h"
#include "Hooks.h"

namespace
{
    constexpr auto VERSION = "0.5.0-alpha";
    constexpr auto BUILD = __DATE__ " " __TIME__;

    void InitializeLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Failed to find standard logging directory");
        }
        *path /= "DragAndDrop.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v");
        SKSE::log::info("DragAndDrop v{} [{}]", VERSION, BUILD);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            DragHandler::GetSingleton()->LoadSettings();
            Hooks::Install();
            SKSE::log::info("Hooks installed");
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            DragHandler::GetSingleton()->OnDataLoad();
            SKSE::log::info("Data loaded, game settings applied");
            break;
        default:
            break;
        }
    }

    bool ReleaseGrabbedActor(RE::StaticFunctionTag*)
    {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return false;

        auto playerPos = player->GetPosition();

        processLists->ForAllActors([&](RE::Actor& actor) {
            if (&actor == player) return RE::BSContainer::ForEachResult::kContinue;

            float paralysis = actor.AsActorValueOwner()->GetActorValue(RE::ActorValue::kParalysis);
            if (paralysis <= 0) return RE::BSContainer::ForEachResult::kContinue;

            float distance = playerPos.GetSquaredDistance(actor.GetPosition());
            if (distance > 22500.0f) return RE::BSContainer::ForEachResult::kContinue;

            auto actorPos = actor.GetPosition();
            auto dir = actorPos - playerPos;
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len < 0.01f) {
                dir.x = 1.0f; dir.y = 0.0f; dir.z = 0.0f;
            } else {
                dir.x /= len; dir.y /= len; dir.z /= len;
            }

            RE::NiPoint3 farPos = actorPos;
            farPos.x += dir.x * 500.0f;
            farPos.y += dir.y * 500.0f;
            farPos.z += dir.z * 500.0f;

            actor.SetPosition(farPos, true);
            actor.AsActorValueOwner()->SetActorValue(RE::ActorValue::kParalysis, 0.0f);
            actor.SetPosition(actorPos, true);

            SKSE::log::info("Released actor via teleport");
            return RE::BSContainer::ForEachResult::kStop;
        });

        player->DestroyMouseSprings();
        return true;
    }

    bool ReleaseNPC(RE::StaticFunctionTag*)
    {
        return DragHandler::GetSingleton()->ReleaseNPC(false, 0.0f);
    }

    bool ThrowNPC(RE::StaticFunctionTag*, float a_force)
    {
        return DragHandler::GetSingleton()->ReleaseNPC(true, a_force);
    }

    RE::Actor* GetGrabbedNPC(RE::StaticFunctionTag*)
    {
        return DragHandler::GetSingleton()->GetGrabbedActor();
    }

    bool IsDragging(RE::StaticFunctionTag*)
    {
        return DragHandler::GetSingleton()->IsDragging();
    }

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* a_vm)
    {
        a_vm->RegisterFunction("ReleaseGrabbedActor", "DragDrop", ReleaseGrabbedActor);
        a_vm->RegisterFunction("ReleaseNPC", "DragDrop", ReleaseNPC);
        a_vm->RegisterFunction("ThrowNPC", "DragDrop", ThrowNPC);
        a_vm->RegisterFunction("GetGrabbedNPC", "DragDrop", GetGrabbedNPC);
        a_vm->RegisterFunction("IsDragging", "DragDrop", IsDragging);
        SKSE::log::info("Papyrus functions registered");
        return true;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    InitializeLog();
    SKSE::log::info("Game version: {} | Plugin: v{} [{}]", a_skse->RuntimeVersion().string(), VERSION, BUILD);

    const auto messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener(MessageHandler);

    SKSE::GetPapyrusInterface()->Register(RegisterPapyrusFunctions);

    return true;
}

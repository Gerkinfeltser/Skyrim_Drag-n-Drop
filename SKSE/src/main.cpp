#include "DragHandler.h"
#include "Hooks.h"

namespace
{
    constexpr auto VERSION = "0.1.98-alpha";
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
            spdlog::set_level(DragHandler::GetSingleton()->IsLoggingEnabled() ? spdlog::level::info : spdlog::level::warn);
            break;
        case SKSE::MessagingInterface::kInputLoaded:
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

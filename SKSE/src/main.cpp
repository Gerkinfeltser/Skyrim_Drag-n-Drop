#include "DragHandler.h"
#include "Hooks.h"

namespace
{
    void InitializeLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
        }
        *path /= "DragAndDrop.log"sv;
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v"s);
        SKSE::log::info("DragAndDrop v1.0.0"sv);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            DragHandler::GetSingleton()->LoadSettings();
            Hooks::Install();
            SKSE::log::info("Hooks installed"sv);
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            DragHandler::GetSingleton()->OnDataLoad();
            SKSE::log::info("Data loaded, game settings applied"sv);
            break;
        default:
            break;
        }
    }

    bool GrabNPC(RE::StaticFunctionTag*, RE::Actor* a_target)
    {
        if (!a_target) {
            SKSE::log::warn("GrabNPC: null target"sv);
            return false;
        }
        return DragHandler::GetSingleton()->GrabNPC(a_target);
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

    bool RegisterPapyrusFunctions(RE::BSScript::Internal::VirtualMachine* a_vm)
    {
        a_vm->RegisterFunction("GrabNPC"sv, "DragDrop"sv, GrabNPC);
        a_vm->RegisterFunction("ReleaseNPC"sv, "DragDrop"sv, ReleaseNPC);
        a_vm->RegisterFunction("ThrowNPC"sv, "DragDrop"sv, ThrowNPC);
        a_vm->RegisterFunction("GetGrabbedNPC"sv, "DragDrop"sv, GetGrabbedNPC);
        a_vm->RegisterFunction("IsDragging"sv, "DragDrop"sv, IsDragging);
        SKSE::log::info("Papyrus functions registered"sv);
        return true;
    }
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion({ 1, 0, 0 });
    v.PluginName("DragAndDrop"sv);
    v.AuthorName("Gerkinfeltser"sv);
    v.UsesAddressLibrary();
    v.UsesUpdatedStructs();
    v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
    return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse, false);
    InitializeLog();
    SKSE::log::info("Game version: {}"sv, a_skse->RuntimeVersion().string());

    const auto messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener(MessageHandler);

    const auto papyrus = SKSE::GetPapyrusInterface();
    papyrus->Register(RegisterPapyrusFunctions);

    return true;
}

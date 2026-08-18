#include "SmokeSample.h"

#include <owl/foundation/BuildInfo.h>
#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace owl::sandbox
{
namespace
{
void LogBuildInformation()
{
    using owl::foundation::LogLevel;
    using owl::foundation::LogMessage;

    const owl::foundation::BuildInfo info = owl::foundation::GetBuildInfo();
    LogMessage(LogLevel::Info, "Build", "Version: " + std::string(info.projectVersion));
    LogMessage(LogLevel::Info, "Build", "Revision: " + std::string(info.gitRevision));
    LogMessage(LogLevel::Info, "Build", "Configuration: " + std::string(info.buildConfiguration));
    LogMessage(LogLevel::Info, "Build", "Compiler: " + std::string(info.compiler));
    LogMessage(LogLevel::Info, "Build", "OS: " + std::string(info.operatingSystem));
    LogMessage(LogLevel::Info, "Build", "Architecture: " + std::string(info.architecture));
}
} // namespace

int RunSmokeSample()
{
    using owl::foundation::LogLevel;
    using owl::foundation::LogMessage;

    owl::foundation::InitializeLogging();
    LogBuildInformation();

    std::string error;
    std::optional<owl::platform::Platform> platform = owl::platform::Platform::Create(error);
    if (!platform)
    {
        LogMessage(LogLevel::Error, "Platform", error);
        owl::foundation::ShutdownLogging();
        return 3;
    }

    LogMessage(LogLevel::Info, "Platform", "SDL: " + platform->SdlVersion());
    LogMessage(LogLevel::Info, "Platform", "Video driver: " + platform->VideoDriver());

    const owl::platform::WindowDesc windowDesc{
        .title = "OwlEngine - M0 Smoke",
        .width = 1280,
        .height = 720,
        .resizable = true,
    };

    std::optional<owl::platform::Window> window = platform->CreateWindow(windowDesc, error);
    if (!window)
    {
        LogMessage(LogLevel::Error, "Platform", error);
        platform.reset();
        owl::foundation::ShutdownLogging();
        return 3;
    }

    LogMessage(LogLevel::Info, "Smoke", "Window created; press Escape or close the window.");

    while (platform->PumpEvents() == owl::platform::EventPumpResult::Continue)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    window.reset();
    platform.reset();
    LogMessage(LogLevel::Info, "Smoke", "Shutdown complete.");
    owl::foundation::ShutdownLogging();
    return 0;
}
} // namespace owl::sandbox

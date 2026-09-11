#include "ClearSample.h"

#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <chrono>
#include <string>
#include <thread>

namespace owl::sandbox
{
    namespace
    {
        int RunClearWindow()
        {
            using owl::foundation::LogLevel;
            using owl::foundation::LogMessage;

            std::string error;
            auto platform = owl::platform::Platform::Create(error);
            if (!platform)
            {
                LogMessage(LogLevel::Error, "Platform", error);
                return 3;
            }

            const owl::platform::WindowDesc windowDesc{
                .title = "OwlEngine - M1 Vulkan Clear",
                .width = 1280,
                .height = 720,
                .resizable = true,
                .surfaceApi = owl::platform::WindowSurfaceApi::Vulkan,
            };
            auto window = platform->CreateWindow(windowDesc, error);
            if (!window)
            {
                LogMessage(LogLevel::Error, "Platform", error);
                return 3;
            }

            // Reverse local destruction keeps the borrowed window alive through Vulkan teardown.
            auto renderer = owl::vulkan::VulkanTriangle::Create(*window, error);
            if (!renderer)
            {
                LogMessage(LogLevel::Error, "Clear", error);
                return 4;
            }

            LogMessage(LogLevel::Info, "Clear",
                       "Window created; press Escape or close the window.");
            while (platform->PumpEvents() == owl::platform::EventPumpResult::Continue)
            {
                const auto frameStart = std::chrono::steady_clock::now();
                if (renderer->RenderFrame(error) == owl::vulkan::FrameResult::Failed)
                {
                    LogMessage(LogLevel::Error, "Clear", error);
                    return 4;
                }

                // Sample pacing also prevents a busy loop while the drawable is unavailable.
                std::this_thread::sleep_until(frameStart + std::chrono::milliseconds{16});
            }

            if (!renderer->WaitIdle(error))
            {
                LogMessage(LogLevel::Error, "Clear", error);
                return 4;
            }

            const auto stats = renderer->Stats();
            LogMessage(LogLevel::Info, "Clear",
                       "Submitted: " + std::to_string(stats.submittedFrames) +
                           ", presented: " + std::to_string(stats.presentedFrames) +
                           ", swapchain generation: " + std::to_string(stats.swapchainGeneration));
            return 0;
        }
    } // namespace

    int RunClearSample()
    {
        owl::foundation::InitializeLogging();
        const int result = RunClearWindow();
        if (result == 0)
        {
            owl::foundation::LogMessage(owl::foundation::LogLevel::Info, "Clear",
                                        "Shutdown complete.");
        }
        owl::foundation::ShutdownLogging();
        return result;
    }
} // namespace owl::sandbox

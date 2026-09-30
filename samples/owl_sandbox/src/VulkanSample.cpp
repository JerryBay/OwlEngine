#include "VulkanSample.h"

#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace owl::sandbox
{
    namespace
    {
        int RunVulkanWindow(const std::string_view name,
                            owl::vulkan::VulkanTriangleOptions options)
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

            if (options.triangleShaders || options.textureShaders)
            {
                const auto executableDirectory =
                    owl::platform::Platform::ExecutableDirectory(error);
                if (!executableDirectory)
                {
                    LogMessage(LogLevel::Error, "Platform", error);
                    return 3;
                }

                const auto resolve = [&](owl::vulkan::TriangleShaderPaths& shaders)
                {
                    shaders.vertex = *executableDirectory / shaders.vertex;
                    shaders.fragment = *executableDirectory / shaders.fragment;
                };
                if (options.triangleShaders)
                    resolve(*options.triangleShaders);
                if (options.textureShaders)
                    resolve(*options.textureShaders);
            }

            const owl::platform::WindowDesc windowDesc{
                .title = "OwlEngine - Vulkan " + std::string{name},
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
            auto renderer = owl::vulkan::VulkanTriangle::Create(*window, error, std::move(options));
            if (!renderer)
            {
                LogMessage(LogLevel::Error, name, error);
                return 4;
            }

            LogMessage(LogLevel::Info, name, "Window created; press Escape or close the window.");
            while (platform->PumpEvents() == owl::platform::EventPumpResult::Continue)
            {
                const auto frameStart = std::chrono::steady_clock::now();
                if (renderer->RenderFrame(error) == owl::vulkan::FrameResult::Failed)
                {
                    LogMessage(LogLevel::Error, name, error);
                    return 4;
                }

                // Sample pacing also prevents a busy loop while the drawable is unavailable.
                std::this_thread::sleep_until(frameStart + std::chrono::milliseconds{16});
            }

            if (!renderer->WaitIdle(error))
            {
                LogMessage(LogLevel::Error, name, error);
                return 4;
            }

            const auto stats = renderer->Stats();
            LogMessage(LogLevel::Info, name,
                       "Submitted: " + std::to_string(stats.submittedFrames) +
                           ", presented: " + std::to_string(stats.presentedFrames) +
                           ", indexed draws: " + std::to_string(stats.indexedDraws) +
                           ", swapchain generation: " + std::to_string(stats.swapchainGeneration));
            return 0;
        }
    } // namespace

    int RunVulkanSample(const std::string_view name,
                        owl::vulkan::VulkanTriangleOptions options)
    {
        owl::foundation::InitializeLogging();
        const int result = RunVulkanWindow(name, std::move(options));
        if (result == 0)
        {
            owl::foundation::LogMessage(owl::foundation::LogLevel::Info, name,
                                        "Shutdown complete.");
        }
        owl::foundation::ShutdownLogging();
        return result;
    }
} // namespace owl::sandbox

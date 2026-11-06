#include "VulkanSample.h"

#include <owl/foundation/Log.h>
#include <owl/platform/Platform.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace owl::sandbox
{
    namespace
    {
        void LogMemoryStats(const owl::vulkan::VulkanTriangle& renderer,
                            const std::string_view name, const std::string_view reason,
                            const owl::vulkan::VulkanTriangleStats& stats)
        {
            const auto memory = renderer.QueryMemoryStats();
            const auto resources = renderer.QueryResourceStats();
            std::ostringstream message;
            message << "VMA memory [" << reason << "]: ";
            if (memory)
            {
                message << "allocations=" << memory->allocationCount
                        << ", allocation bytes=" << memory->allocationBytes
                        << ", blocks=" << memory->blockCount
                        << ", reserved block bytes=" << memory->blockBytes
                        << "; texture replacements=" << stats.textureReplacements
                        << ", pending upload=" << (stats.textureReplacementPending ? "yes" : "no")
                        << ", pending retirements=" << stats.pendingTextureRetirements;
                if (resources)
                {
                    message << "; buffers=" << resources->buffers.count
                            << ", buffer requested bytes=" << resources->buffers.requestedBytes
                            << ", buffer allocation bytes=" << resources->buffers.allocationBytes
                            << "; images=" << resources->images.count
                            << ", image requested bytes=" << resources->images.requestedBytes
                            << ", image allocation bytes=" << resources->images.allocationBytes
                            << ", retired image allocation bytes (included)="
                            << resources->retiredImageAllocationBytes;
                }
            }
            else
            {
                message << "unavailable (no active VMA allocator)";
            }
            owl::foundation::LogMessage(owl::foundation::LogLevel::Info, name, message.str());
        }

        // Sample-only controls. The renderer receives draw data and owns no input or clock.
        class MipSampleControls
        {
        public:
            [[nodiscard]] bool UpdateTexture(const owl::platform::Platform& platform,
                                             owl::vulkan::VulkanTriangle& renderer,
                                             std::string& error)
            {
                const bool down = platform.IsKeyDown(owl::platform::Key::R);
                const bool pressed = down && !replacementKeyDown_;
                replacementKeyDown_ = down;
                if (!pressed)
                    return true;
                // Sample-owned palettes. Each accepted request creates a new native image/set.
                std::array<std::byte, 64 * 64 * 4> pixels;
                for (std::size_t y = 0; y < 64; ++y)
                {
                    for (std::size_t x = 0; x < 64; ++x)
                    {
                        const auto offset = (y * 64 + x) * 4;
                        const bool light = ((x / 8 + y / 8) % 2) == 0;
                        const bool marker = x < 8 && y < 8;
                        const auto rgb = marker         ? std::array{240, 40, 150}
                                         : light        ? std::array{235, 240, 245}
                                         : warmPalette_ ? std::array{210, 100, 25}
                                                        : std::array{20, 140, 210};
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            pixels[offset + channel] = static_cast<std::byte>(rgb[channel]);
                        pixels[offset + 3] = std::byte{255};
                    }
                }
                const auto result = renderer.RequestTextureReplacement(pixels, error);
                if (result == owl::vulkan::TextureReplacementResult::Accepted)
                    warmPalette_ = !warmPalette_;
                else if (result == owl::vulkan::TextureReplacementResult::Busy)
                    owl::foundation::LogMessage(
                        owl::foundation::LogLevel::Info, "Texture Mips",
                        "Texture replacement busy; press R again when ready.");
                return result != owl::vulkan::TextureReplacementResult::Failed;
            }

            [[nodiscard]] owl::vulkan::TextureDrawParameters
            Update(const owl::platform::Platform& platform, const double elapsedSeconds)
            {
                constexpr std::array keys{
                    owl::platform::Key::Digit0, owl::platform::Key::Digit1,
                    owl::platform::Key::Digit2, owl::platform::Key::Digit3,
                    owl::platform::Key::Digit4, owl::platform::Key::Digit5,
                    owl::platform::Key::Digit6, owl::platform::Key::A,
                    owl::platform::Key::Space,
                };
                std::array<bool, keys.size()> down{};
                for (std::size_t index = 0; index < keys.size(); ++index)
                {
                    down[index] = platform.IsKeyDown(keys[index]);
                    if (index < 7 && down[index] && !previousDown_[index])
                    {
                        explicitLod_ = static_cast<float>(index);
                    }
                }

                bool startedAutomaticZoom = false;
                if (down[7] && !previousDown_[7])
                {
                    explicitLod_.reset();
                    automaticSeconds_ = 0.0;
                    paused_ = false;
                    startedAutomaticZoom = true;
                }
                if (down[8] && !previousDown_[8] && !explicitLod_)
                {
                    paused_ = !paused_;
                }
                previousDown_ = down;

                if (explicitLod_)
                {
                    return {.scale = 1.0F, .lod = explicitLod_};
                }

                if (!paused_ && !startedAutomaticZoom)
                {
                    automaticSeconds_ += elapsedSeconds;
                }
                // A cosine makes the endpoints smooth; interpolate in log2 scale so each
                // octave takes useful viewing time, down to one 128th of the starting size.
                const double phase = std::fmod(automaticSeconds_, 16.0) / 16.0;
                const double zoom = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * phase));
                return {.scale = static_cast<float>(std::exp2(-7.0 * zoom)), .lod = std::nullopt};
            }

            [[nodiscard]] std::string Title(const owl::vulkan::TextureDrawParameters& parameters,
                                            const owl::vulkan::VulkanTriangleStats& stats) const
            {
                std::ostringstream title;
                title << "OwlEngine - Vulkan Texture Mips ["
                      << (stats.textureGpuMipGeneration ? "GPU blit" : "CPU mips") << ", "
                      << stats.textureMipLevels << " levels] | ";
                if (parameters.lod)
                {
                    title << "LOD " << static_cast<int>(*parameters.lod);
                }
                else
                {
                    title << "Automatic LOD" << (paused_ ? " (paused)" : "") << ", scale "
                          << std::fixed << std::setprecision(3) << parameters.scale;
                }
                title << " | Texture swaps " << stats.textureReplacements;
                if (stats.textureReplacementPending)
                    title << " (uploading)";
                title << " | 0-6: LOD, A: Auto, Space: Pause, R: Texture";
                return title.str();
            }

        private:
            std::array<bool, 9> previousDown_{};
            std::optional<float> explicitLod_ = 0.0F;
            double automaticSeconds_ = 0.0;
            bool paused_ = false;
            bool replacementKeyDown_ = false;
            bool warmPalette_ = false;
        };

        int RunVulkanWindow(const std::string_view name, owl::vulkan::VulkanTriangleOptions options)
        {
            using owl::foundation::LogLevel;
            using owl::foundation::LogMessage;

            const bool mipSample =
                options.textureMipMode != owl::vulkan::TextureMipMode::SingleLevel;

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
            if (mipSample)
            {
                const auto stats = renderer->Stats();
                LogMessage(LogLevel::Info, name,
                           "Texture: " + std::to_string(stats.textureMipLevels) +
                               " mip levels, generated with " +
                               (stats.textureGpuMipGeneration ? "GPU blit." : "CPU filtering."));
                LogMessage(LogLevel::Info, name,
                           "Controls: 0-6 select explicit LOD; A starts automatic LOD/zoom; "
                           "Space pauses/resumes zoom; R replaces the texture (blue/orange).");
            }
            auto lastMemoryEvent = renderer->Stats();
            LogMemoryStats(*renderer, name, "startup", lastMemoryEvent);
            const auto logMemoryChanges = [&]
            {
                const auto stats = renderer->Stats();
                if (stats.textureReplacements != lastMemoryEvent.textureReplacements ||
                    stats.textureReplacementPending != lastMemoryEvent.textureReplacementPending ||
                    stats.pendingTextureRetirements != lastMemoryEvent.pendingTextureRetirements)
                {
                    LogMemoryStats(*renderer, name, "texture state", stats);
                    lastMemoryEvent = stats;
                }
            };
            MipSampleControls mipControls;
            auto previousFrame = std::chrono::steady_clock::now();
            std::string currentTitle;
            while (platform->PumpEvents() == owl::platform::EventPumpResult::Continue)
            {
                const auto frameStart = std::chrono::steady_clock::now();
                owl::vulkan::TextureDrawParameters drawParameters;
                if (mipSample)
                {
                    if (!mipControls.UpdateTexture(*platform, *renderer, error))
                    {
                        LogMessage(LogLevel::Error, name, error);
                        return 4;
                    }
                    // Observe accepted upload storage before RenderFrame can publish it.
                    logMemoryChanges();
                    const double elapsedSeconds =
                        std::chrono::duration<double>{frameStart - previousFrame}.count();
                    drawParameters = mipControls.Update(*platform, elapsedSeconds);
                    const auto title = mipControls.Title(drawParameters, renderer->Stats());
                    if (title != currentTitle)
                    {
                        if (!window->SetTitle(title, error))
                        {
                            LogMessage(LogLevel::Error, "Platform", error);
                            return 3;
                        }
                        currentTitle = title;
                    }
                }
                previousFrame = frameStart;
                if (renderer->RenderFrame(error, drawParameters) ==
                    owl::vulkan::FrameResult::Failed)
                {
                    LogMessage(LogLevel::Error, name, error);
                    return 4;
                }

                logMemoryChanges();

                // Sample pacing also prevents a busy loop while the drawable is unavailable.
                std::this_thread::sleep_until(frameStart + std::chrono::milliseconds{16});
            }

            if (!renderer->WaitIdle(error))
            {
                LogMessage(LogLevel::Error, name, error);
                return 4;
            }

            const auto stats = renderer->Stats();
            LogMemoryStats(*renderer, name, "after WaitIdle", stats);
            LogMessage(LogLevel::Info, name,
                       "Submitted: " + std::to_string(stats.submittedFrames) +
                           ", presented: " + std::to_string(stats.presentedFrames) +
                           ", indexed draws: " + std::to_string(stats.indexedDraws) +
                           ", swapchain generation: " + std::to_string(stats.swapchainGeneration) +
                           ", submitted serial: " + std::to_string(stats.submittedSerial) +
                           ", completed serial: " + std::to_string(stats.completedSerial) +
                           ", frame upload capacity bytes: " +
                           std::to_string(stats.frameUploadCapacityBytes) +
                           ", frame uploaded bytes: " + std::to_string(stats.frameUploadedBytes) +
                           ", texture replacements: " + std::to_string(stats.textureReplacements) +
                           ", pending texture retirements: " +
                           std::to_string(stats.pendingTextureRetirements) +
                           ", texture replacement pending: " +
                           (stats.textureReplacementPending ? "yes" : "no"));
            return 0;
        }
    } // namespace

    int RunVulkanSample(const std::string_view name, owl::vulkan::VulkanTriangleOptions options)
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

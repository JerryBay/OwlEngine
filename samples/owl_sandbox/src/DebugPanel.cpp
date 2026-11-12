#include "DebugPanel.h"

#include "SDLEventAccess.h"
#include "SDLWindowAccess.h"
#include "VulkanDebugUiAccess.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <algorithm>

namespace owl::sandbox
{
    namespace
    {
        void Counter(const char* name, const std::uint64_t value)
        {
            ImGui::Text("%s: %llu", name, static_cast<unsigned long long>(value));
        }

        void Bytes(const char* name, const std::uint64_t bytes)
        {
            ImGui::Text("%s: %.2f MiB (%llu B)", name,
                        static_cast<double>(bytes) / (1024.0 * 1024.0),
                        static_cast<unsigned long long>(bytes));
        }
    } // namespace

    DebugPanel::~DebugPanel()
    {
        if (context_ == nullptr)
            return;
        ImGui::SetCurrentContext(context_);
        if (platformInitialized_)
            ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(context_);
    }

    bool DebugPanel::Initialize(owl::platform::Window& window, std::string& error)
    {
        SDL_Window* native = owl::platform::SDLWindowAccess::Get(window);
        if (context_ != nullptr || native == nullptr)
        {
            error = "Debug panel requires a live window and one initialization";
            return false;
        }
        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
        platformInitialized_ = ImGui_ImplSDL3_InitForVulkan(native);
        if (!platformInitialized_)
        {
            error = "Cannot initialize the ImGui SDL3 backend";
            return false;
        }
        error.clear();
        return true;
    }

    bool DebugPanel::Attach(owl::vulkan::VulkanTriangle& renderer, std::string& error)
    {
        if (!platformInitialized_)
        {
            error = "Debug panel must initialize before attachment";
            return false;
        }
        if (!owl::vulkan::VulkanDebugUiAccess::Attach(renderer, context_, BuildFrame, this, error))
            return false;
        renderer_ = &renderer;
        return true;
    }

    void DebugPanel::ObserveEvent(const SDL_Event& event, void* userdata) noexcept
    {
        auto& panel = *static_cast<DebugPanel*>(userdata);
        ImGui::SetCurrentContext(panel.context_);
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    owl::platform::EventPumpResult DebugPanel::PumpEvents() noexcept
    {
        return owl::platform::SDLEventAccess::Pump(ObserveEvent, this);
    }

    bool DebugPanel::WantsKeyboard() const noexcept
    {
        ImGui::SetCurrentContext(context_);
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void DebugPanel::BuildFrame(void* userdata)
    {
        auto& panel = *static_cast<DebugPanel*>(userdata);
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        panel.Draw();
        ImGui::Render();
    }

    void DebugPanel::Draw()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextBudgetRefresh_)
        {
            budgets_ = renderer_->QueryMemoryBudgetStats();
            nextBudgetRefresh_ = now + std::chrono::milliseconds{250};
        }
        const auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos({io.DisplaySize.x - 12.0F, 12.0F}, ImGuiCond_FirstUseEver,
                                {1.0F, 0.0F});
        ImGui::SetNextWindowSize({420.0F, std::min(630.0F, io.DisplaySize.y - 24.0F)},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("OwlEngine | Diagnostics"))
        {
            const auto stats = renderer_->Stats();
            ImGui::Text("Vulkan 1.3 | %.1f FPS (CPU UI cadence)", io.Framerate);
            ImGui::TextDisabled("0-6: LOD | A: Zoom | Space: Pause | R: Texture");
            ImGui::TextDisabled("Ctrl+Tab: UI focus | Esc: Exit");
            ImGui::Text("Keyboard: %s", io.WantCaptureKeyboard ? "UI" : "sample");
            if (ImGui::Button("Return keyboard to sample"))
                ImGui::SetWindowFocus(nullptr);

            if (ImGui::CollapsingHeader("Frames and uploads", ImGuiTreeNodeFlags_DefaultOpen))
            {
                Counter("Submitted / scene draws", stats.submittedFrames);
                Counter("Presented", stats.presentedFrames);
                Counter("Completed serial (CPU observed)", stats.completedSerial);
                Counter("Swapchain generation", stats.swapchainGeneration);
                Bytes("Frame upload capacity", stats.frameUploadCapacityBytes);
                Bytes("Submitted geometry payload", stats.frameUploadedBytes);
                Counter("Texture swaps", stats.textureReplacements);
                ImGui::Text("Texture upload pending: %s",
                            stats.textureReplacementPending ? "yes" : "no");
                Counter("Pending texture retirements", stats.pendingTextureRetirements);
            }
            if (ImGui::CollapsingHeader("Renderer VMA (excludes ImGui)",
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (const auto memory = renderer_->QueryMemoryStats())
                {
                    Counter("Live allocations", memory->allocationCount);
                    Bytes("Live allocation bytes", memory->allocationBytes);
                    Counter("Reserved blocks", memory->blockCount);
                    Bytes("Reserved block bytes", memory->blockBytes);
                }
                if (const auto resources = renderer_->QueryResourceStats())
                {
                    Counter("Buffers", resources->buffers.count);
                    Bytes("Buffer requested", resources->buffers.requestedBytes);
                    Bytes("Buffer allocated", resources->buffers.allocationBytes);
                    Counter("Images", resources->images.count);
                    Bytes("Image requested", resources->images.requestedBytes);
                    Bytes("Image allocated", resources->images.allocationBytes);
                    Bytes("Retired images (included)", resources->retiredImageAllocationBytes);
                }
            }
            if (ImGui::CollapsingHeader("Heap budgets (process estimates)",
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (!budgets_)
                    ImGui::TextDisabled("VK_EXT_memory_budget unavailable");
                else
                    for (std::uint32_t index = 0; index < budgets_->heapCount; ++index)
                    {
                        const auto& heap = budgets_->heaps[index];
                        ImGui::Separator();
                        ImGui::Text("Heap %u | %s", heap.heapIndex,
                                    heap.deviceLocal ? "Device local" : "Host");
                        Bytes("Capacity", heap.heapSize);
                        Bytes("Usage estimate", heap.usage);
                        Bytes("Budget estimate", heap.budget);
                    }
            }
        }
        ImGui::End();
    }
} // namespace owl::sandbox

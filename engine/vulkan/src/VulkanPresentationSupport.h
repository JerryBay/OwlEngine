#pragma once

namespace owl::vulkan::detail
{
    enum class SwapchainMaintenance
    {
        None,
        Khr,
        Ext,
    };

    struct PresentationSupportCapabilities
    {
        bool khrSurfaceMaintenance1 = false;
        bool extSurfaceMaintenance1 = false;
    };

    struct SwapchainMaintenanceAvailability
    {
        bool khrExtension = false;
        bool extExtension = false;
        bool feature = false;
    };

    [[nodiscard]] SwapchainMaintenance
    SelectSwapchainMaintenance(const PresentationSupportCapabilities& instanceSupport,
                               const SwapchainMaintenanceAvailability& deviceSupport) noexcept;
} // namespace owl::vulkan::detail

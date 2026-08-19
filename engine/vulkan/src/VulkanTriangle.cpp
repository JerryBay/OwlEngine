#include "SDLWindowAccess.h"

#include <owl/platform/Window.h>
#include <owl/vulkan/VulkanTriangle.h>

#include <vulkan/vulkan.h>

#include <type_traits>
#include <utility>

static_assert(VK_API_VERSION_1_3 == VK_MAKE_API_VERSION(0, 1, 3, 0));
static_assert(std::is_same_v<
              decltype(owl::platform::SDLWindowAccess::Get(std::declval<owl::platform::Window&>())),
              SDL_Window*>);

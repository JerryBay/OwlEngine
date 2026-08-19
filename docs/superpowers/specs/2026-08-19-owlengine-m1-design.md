# OwlEngine M1 Vulkan Bootstrap and Frame Lifecycle Design

- Status: Approved
- Date: 2026-08-19
- Parent roadmap: [OwlEngine Roadmap Design](2026-08-17-owlengine-roadmap-design.md)
- Prerequisite: M0 clean-clone and CI acceptance

## 1. Objective

M1 adds the first native Vulkan 1.3 execution path to OwlEngine. It proves that the M0
Platform boundary can host a Vulkan surface, that a suitable GPU can be selected, and that a
swapchain can acquire, clear, present, resize, and recover without validation errors.

This milestone deliberately keeps Vulkan calls explicit. It creates evidence for M2-M5; it
does not extract an RHI or introduce a renderer architecture.

## 2. Scope

### Included

- vcpkg-managed Vulkan Headers and Loader for reproducible project and CI builds
- Optional LunarG Vulkan SDK use for validation layers, `vulkaninfo`, and capture tooling
- Vulkan instance, debug messenger, physical-device selection, logical device, and queues
- SDL-created Vulkan surface and Vulkan-owned `VkSurfaceKHR`
- Swapchain selection, creation, image views, and swapchain recreation
- Per-frame fences, acquire/present semaphores, command pools, and command buffers
- Dynamic Rendering clear path followed by an indexed triangle
- Debug names and structured logging of Vulkan results and selected capabilities
- Focused CPU tests for deterministic queue, extension, and swapchain-selection logic

### Excluded

- RHI interfaces, backend-neutral handles, or D3D12 support
- Resource allocator abstraction, staging/upload framework, texture loading, or glTF
- Descriptor-management system, shader package system, material system, or Render Graph
- Multi-queue scheduling, async compute, bindless resources, MSAA, HDR, or post-processing
- ImGui, editor UI, screenshots/reference images, and Android support

## 3. Confirmed Decisions

| Decision | Rationale |
| --- | --- |
| Use vcpkg for Vulkan Headers and Loader. | CI and a clean clone get the same project dependency contract. |
| Use the installed Vulkan SDK only for diagnostics and tooling. | Validation layers and capture tools remain machine capabilities, not committed binary dependencies. |
| Add one `OwlVulkan` static target below `OwlSandbox` and above no future RHI. | Vulkan now has a real, cohesive object-lifetime owner; an RHI has no second-backend evidence yet. |
| Keep direct Vulkan calls visible. | M1 is a learning and reference slice; wrappers that only rename Vulkan calls obscure lifetime and synchronization evidence. |
| Support separate graphics and present queue families. | A single-family assumption would make device selection incorrect on valid hardware. |
| Own `VkSurfaceKHR` and all swapchain resources in `OwlVulkan`. | They are Vulkan objects and must be destroyed before their `VkInstance`/`VkDevice` owners. |
| Commit one sample-local precompiled SPIR-V vertex/fragment pair. | M1 can exercise the triangle without introducing a general DXC/HLSL build contract before M4 owns that system. |
| Add `OwlSandbox --sample triangle` and leave `--sample smoke` unchanged. | M0 keeps a stable platform-only acceptance path while M1 gains an explicit Vulkan acceptance path. |
| Use a private Platform SDL-access header for the surface bridge. | `OwlVulkan` gets the one SDL capability it needs without adding a public native-handle escape hatch or a second target. |

## 4. Target Boundaries

```text
OwlSandbox
    |
OwlVulkan ---- Vulkan Loader
    |
OwlPlatform -- SDL3
    |
OwlFoundation -- spdlog
```

`OwlSandbox` continues to own command-line sample selection and process exit codes. It creates
the platform window, gives that window to `OwlVulkan`, runs the frame loop, and owns demo policy.

`OwlVulkan` owns Vulkan instance-to-swapchain lifetime, device policy, synchronization, shader
modules, the triangle pipeline, and Vulkan diagnostics. It must not expose native Vulkan objects
to `OwlSandbox`.

`OwlPlatform` remains SDL-owned and exposes no native SDL or Vulkan types in its public headers.
M1 adds only three Platform capabilities justified by the renderer:

- a `WindowSurfaceApi::None/Vulkan` creation intent on `WindowDesc`;
- pixel framebuffer extent query for swapchain sizing;
- a private SDL window-access header visible only to `OwlVulkan`, not a public raw-handle API.

`None` remains the default for Smoke and other ordinary windows. `Vulkan` maps privately to
`SDL_WINDOW_VULKAN`, because SDL requires that flag before `SDL_Vulkan_CreateSurface()` can create
the platform surface. The flag is never added unconditionally: doing so would introduce a Vulkan
Loader/driver requirement into non-Vulkan window paths. D3D12 requires no corresponding SDL flag
and will extend the creation policy only when its backend is implemented.

The internal bridge lets `OwlVulkan` call SDL's Vulkan surface helper while keeping `SDL_Window*`,
`VkInstance`, and `VkSurfaceKHR` out of the general Platform API. A generic `void*` native-handle
escape hatch remains prohibited.

## 5. Lifecycle and Ownership

Creation order:

1. `Platform` and `Window`
2. Vulkan instance and optional debug messenger
3. SDL-backed Vulkan surface
4. physical-device and queue-family selection
5. logical device and queues
6. swapchain, swapchain image views, and swapchain-dependent pipeline state
7. per-frame command and synchronization objects

Shutdown is the strict reverse order. Swapchain recreation waits only for submitted work that can
reference old swapchain resources, destroys swapchain-dependent resources, obtains a non-zero
pixel extent, then creates replacements. Device-, instance-, and surface-level objects survive a
normal resize.

Minimized windows are not fatal. The loop continues pumping events and defers recreation until
SDL reports a non-zero pixel extent.

## 6. Device and Swapchain Policy

A physical device is suitable only when it has:

- Vulkan 1.3 support;
- the `VK_KHR_swapchain` device extension;
- graphics and present queue-family selections;
- at least one surface format and present mode;
- required Vulkan 1.3 features for Dynamic Rendering and Synchronization2.

The selection record stores graphics and present queue-family indices as optional values during
enumeration, then becomes immutable after suitability is established. A single family is preferred
when available; separate families use concurrent swapchain sharing mode in M1 to avoid premature
queue-ownership transfer complexity.

Prefer an SRGB surface format when available, choose mailbox presentation when available and FIFO
otherwise, and clamp the requested pixel extent to surface capabilities. These preferences are
reported in structured logs.

## 7. Frame Contract

For each frame slot:

1. Pump SDL events; exit on the existing quit policy.
2. If recreation is pending, recreate only when the pixel extent is non-zero.
3. Wait for the slot fence, then acquire the next swapchain image.
4. Treat `VK_ERROR_OUT_OF_DATE_KHR` as a recreation request, not a fatal error.
5. Reset the slot command pool and record image transitions, clear rendering, and the triangle.
6. Submit with acquire and render-finished semaphores, then present.
7. Treat present out-of-date or suboptimal results as a request to recreate after the frame.

The baseline uses two frames in flight. It performs no CPU readback, no implicit global synchronization,
and no generalized barrier abstraction.

## 8. Diagnostics and Failure Policy

- Validation is enabled in debug-capable builds only when the requested layer is available; its
  absence produces an explicit warning instead of a startup failure.
- A debug messenger routes severity, message ID, and object names into `OwlFoundation` logging.
- Every non-success Vulkan result is checked at the call site and logged with the original numeric
  result and operation name.
- Device-selection rejection logs are summarized by candidate rather than emitted as an opaque
  "no device" failure.
- Object debug names are assigned to instance-owned resources that appear in validation or captures.

## 9. Demo and Acceptance

`OwlSandbox --sample triangle` starts the M1 Vulkan sample. It opens the existing SDL window,
clears the swapchain image, and draws an indexed triangle with precompiled local shaders.

M1 is ready for implementation planning only when the following acceptance criteria are retained:

1. A compatible desktop device starts the triangle sample with validation enabled and no validation
   errors.
2. Resize, minimize, restore, and close all complete without a crash, hang, or leaked validation
   message.
3. The sample runs for 10,000 frames without a validation error.
4. A RenderDoc capture contains a valid acquire, submission, dynamic-rendering draw, and present.
5. CPU tests cover queue-family selection, extension requirements, present-mode choice, and extent
   clamping.
6. The existing M0 configure, build, test, and smoke paths continue to work.

## 10. Decision Log

**Confirmed**

- Vulkan Headers and Loader are vcpkg dependencies; the SDK is optional diagnostics tooling.
- The first renderer remains native Vulkan and intentionally has no RHI.
- The triangle uses checked-in, sample-local precompiled SPIR-V; DXC/HLSL build integration is deferred to M4.
- `--sample triangle` is the Vulkan acceptance command; `--sample smoke` remains the M0 platform-only command.
- `WindowSurfaceApi::Vulkan` explicitly requests `SDL_WINDOW_VULKAN`; ordinary and future D3D12
  windows do not acquire that Vulkan dependency.

**Rejected**

- A generic public native-window handle: it spreads SDL/platform ownership upward without a stable
  cross-backend contract.
- A single initialization "god object": lifecycle boundaries need independent tests and clear
  destruction order.

**Open**

- None before implementation planning. Future Vulkan features remain intentionally out of M1 scope.

**Evidence**

- M0 exposes only a move-only `Window` and intentionally prohibits a public SDL escape hatch.
- The roadmap requires explicit Vulkan calls, separate queue-family support, a clear-plus-triangle
  demo, 10,000 validation-clean frames, and a RenderDoc capture.

**Next validation**

Follow the companion M1 implementation plan with file ownership, test-first checkpoints, and
supported configure/build/run commands. No implementation may expand beyond this design without a
new design decision.

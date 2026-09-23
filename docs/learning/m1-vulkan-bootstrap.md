# M1 Vulkan Bootstrap Acceptance

M1 establishes a native Vulkan reference: Instance and Surface, device/queue selection,
Swapchain, two frame slots, Dynamic Rendering, indexed drawing, and window-driven recreation.
The resource owners make lifetimes explicit; a general RHI and resource allocator remain later work.

## What the checks establish

- CPU tests exercise selection and result policies independently of a GPU.
- Local GPU tests exercise real resource lifetimes, clear/indexed rendering, resize,
  minimized deferral, restore, and teardown with and without optional presentation fences.
- A formal Sandbox run checks the deployed assets and application event loop. Acceptance
  requires at least 10,000 presented frames, clean shutdown, and no validation errors.
- RenderDoc inspection establishes the captured draw, shader interfaces, render target,
  and pixel output. It does not replace synchronization validation.

Use the [Windows validation instructions](../building/windows.md#validation-enabled-acceptance).
CTest rejects `[error] [VulkanValidation]` output even when Catch2 assertions pass. Confirm
that the layer is actually enabled: tests can still run without it. Direct test executable
and Sandbox runs require inspecting the logs as well as the exit code.
Keep implementation, runtime stability, visual output, and API correctness as separate results.

## Synchronization correction

The acquire semaphore is waited at `COLOR_ATTACHMENT_OUTPUT`. The first swapchain image
barrier now uses that same source stage, connecting its layout transition to the semaphore
wait. Previously, source stage `NONE` left this execution dependency missing and triggered
`SYNC-HAZARD-WRITE-AFTER-READ`. The destination stage only orders the later attachment writes;
it does not connect the transition to the earlier wait.

Source access remains `NONE`, and `UNDEFINED` still discards previous contents. Discarding
contents does not waive synchronization with outstanding image accesses. See the
[Khronos swapchain synchronization examples](https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples#swapchain-image-acquire-and-present).

## Verification

On 2026-09-23, VS2026 Debug and RelWithDebInfo each passed all five local GPU tests with
Khronos Validation Layer 1.4.357, synchronization validation, and submit-time validation enabled
on NVIDIA GeForce RTX 5060, driver 610.88. Both reported zero validation errors and warnings.
The tests cover clear/triangle rendering with automatic presentation fences and forced
compatibility fallback, resize, minimized deferral, restore, and teardown.

The CTest failure rule was also exercised against the still-unmodified RelWithDebInfo binary:
Clear and Triangle were rejected for the validation errors, returning CTest exit code 8.
After rebuilding the correction, the same tests returned exit code 0. Both default presets
passed 58 tests and skipped the five opt-in GPU tests.

The corrected formal Debug Triangle presented 12,124 frames with 12,124 indexed draws;
Clear presented 707 frames. Triangle, Clear, and Smoke all exited with code 0 and logged
`Shutdown complete.`. Both Vulkan samples reported zero validation errors and warnings
through startup, resize/maximize/minimize/restore, rendering, and teardown. Their final
swapchain generation was 5. Triangle used the close-window path; Clear and Smoke used Escape.

Remaining limits: complete client-edge inspection during continuous manual dragging, VS2022
on the other workstation, separate graphics/present-family hardware, and native failure
injection are not established by this run. The earlier RenderDoc/MCP inspection remains a
separate pre-fix capture; this run verifies the correction using synchronization validation.

Detailed local logs and the diagnostic harness are in the ignored
`build/diagnostics/m1-validation-20260923/` directory. `regression-before/` preserves the failing
CTest check; `fixed-gpu/` and `fixed-sandbox/` contain the corrected runs. Portable current status and remaining
work belong in [PROJECT.md](../../PROJECT.md).

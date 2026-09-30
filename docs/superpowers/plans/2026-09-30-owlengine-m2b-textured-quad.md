# M2B Textured Quad Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display a procedural, oriented checkerboard on an indexed quad via the existing single-mip Vulkan image upload.

**Architecture:** Keep the current `VulkanTriangle` window, Swapchain, frame-sync and submission owner. Add a private textured-quad owner for immutable geometry, uploaded image, view, sampler, one combined-image-sampler descriptor, and a format-dependent graphics pipeline. The texture and descriptor survive Swapchain recreation; only the graphics pipeline changes if the color format changes.

**Tech Stack:** C++20, Vulkan 1.3, SDL3, VMA, CMake, Catch2, precompiled GLSL/SPIR-V.

**Spec:** `docs/superpowers/specs/2026-09-23-owlengine-m2-design.md` section 5.

## Global Constraints

- Preserve Smoke, Clear, and Triangle behavior and use the existing graphics queue and frame loop.
- Use one RGBA8 single-mip sampled image and one combined image sampler; no general Shader System or RHI.
- Ordinary builds must not require a shader compiler; check in source, SPIR-V, and regeneration instructions.
- Drain submitted uploads and draws before releasing their resources, including failed initialization.
- Target VS2026 locally; VS2022 is checked on the other workstation.

---

### Task 1: CLI and Sample Entry

**Files:** `engine/foundation/include/owl/foundation/CommandLine.h`, `engine/foundation/src/CommandLine.cpp`, `samples/owl_sandbox/src/main.cpp`, `samples/owl_sandbox/src/TextureSample.h`, `samples/owl_sandbox/src/TextureSample.cpp`, `samples/owl_sandbox/src/VulkanSample.cpp`, `samples/owl_sandbox/CMakeLists.txt`, `tests/foundation/CommandLineTests.cpp`.

**Interfaces:** `Command::Texture` selects `RunTextureSample()`. It supplies executable-relative `assets/m2/texture.vert.spv` and `texture.frag.spv` through `VulkanTriangleOptions::textureShaders`.

- [x] Add a Catch2 case accepting `--sample texture` and rejecting trailing arguments; build the Foundation test to observe the missing enum/command failure.
- [x] Add the command and sample entry, keep the current shared event loop, and copy both shader binaries with the Sandbox target.
- [x] Rebuild and run the command-line tests, Sandbox help, and invalid-platform smoke case.

### Task 2: Texture Draw Owner

**Files:** `engine/vulkan/src/VulkanTexturedQuadPipeline.h`, `engine/vulkan/src/VulkanTexturedQuadPipeline.cpp`, `engine/vulkan/src/VulkanShaderBinary.h`, `engine/vulkan/src/VulkanShaderBinary.cpp`, `engine/vulkan/src/VulkanTrianglePipeline.cpp`, `engine/vulkan/src/VulkanTrianglePipeline.h`, `engine/vulkan/CMakeLists.txt`, `tests/vulkan/TrianglePipelineTests.cpp`, `tests/vulkan/TexturePipelineTests.cpp`.

**Interfaces:** `Initialize(device, allocator, shaderPaths, error)`, `SetColorFormat(format, error)`, `RecordDraw(command, extent)`, and upload wait/drain functions. Private SPIR-V loader serves both samples with the existing bounded binary-envelope check.

- [x] Add failing tests for the oriented CPU checkerboard bytes and shared SPIR-V loader; build/run the focused tests and confirm the expected failure.
- [x] Generate a bounded asymmetric checkerboard, upload quad vertices/indices and RGBA8 image once, then create view, nearest sampler, descriptor set layout/pool/set, pipeline layout and graphics pipeline.
- [x] Bind the pipeline, viewport, scissor, descriptor, vertex/index buffer, then issue one six-index draw. Make upload-failure and destruction paths drain pending work.
- [x] Run focused CPU tests, shader validation, and build both Debug and RelWithDebInfo.

### Task 3: Shared Frame Integration and Validation

**Files:** `engine/vulkan/include/owl/vulkan/VulkanTriangle.h`, `engine/vulkan/src/VulkanTriangle.cpp`, `tests/vulkan/FrameTests.cpp`, `tests/CMakeLists.txt`, `docs/building/windows.md`, `PROJECT.md`.

**Interfaces:** Clear/Triangle/Texture mode selection in the current renderer. Reject ambiguous simultaneous Triangle and Texture options; preserve the public frame and statistics contract.

- [x] Add a texture frame-lifecycle integration case covering indexed draws, resize, minimize/restore, move and WaitIdle; observe the missing texture option failure before implementing.
- [x] Route initialization, upload drain, format-dependent pipeline recreation, draw recording and indexed-draw statistics through the existing renderer.
- [x] Run complete VS2026 Debug and RelWithDebInfo CTest with GPU opt-in and actual Khronos validation active; inspect logs for validation errors.
- [x] Launch `OwlSandbox --sample texture` to inspect orientation and resize behavior. Capture a frame with RenderDoc if available and verify sampled image, descriptor and six-index draw; report any visual/capture check not independently completed.
- [x] Update build instructions and project memory only with observed verification, then inspect the final diff and status.

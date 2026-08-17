# OwlEngine Roadmap Design

- Status: Approved
- Date: 2026-08-17
- Project type: Long-lived open-source personal rendering engine
- Primary goal: Learn modern graphics APIs, RHI design, and modern rendering architecture through production-quality code

## 1. Project Charter

OwlEngine is a focused renderer and engine laboratory. It is not intended to become a feature-complete commercial engine. Its success is measured by depth of understanding, defensible architecture, reproducible engineering, and evidence-backed performance work.

Priority order:

1. Learning depth
2. Architecture quality
3. Engineering quality
4. Feature count

The project follows an API-first vertical-slice path:

1. Build a real renderer directly on Vulkan 1.3.
2. Extract an RHI only after several concrete Vulkan use cases exist.
3. Add a D3D12 backend and revise the RHI where the second API disproves Vulkan-shaped assumptions.
4. Build Shader, Material, Render Graph, PBR, Bindless, GPU Driven, and Async Compute systems above the validated RHI.
5. Port the renderer to Android Vulkan and optimize it using mobile GPU evidence rather than desktop assumptions.

The roadmap is capability- and Definition-of-Done-driven. Calendar dates do not allow a milestone to pass without its acceptance evidence.

## 2. Confirmed Boundaries

### 2.1 Initial platform and language

- C++20 is the baseline language version.
- Win64 is the first host platform.
- Vulkan 1.3 is the minimum desktop Vulkan contract.
- The project uses current Vulkan headers and tooling while treating Vulkan 1.4 features as optional capabilities.
- D3D12 is the second desktop backend.
- Android Vulkan is added after the desktop RHI and Render Graph are stable.

### 2.2 Confirmed toolchain defaults

- CMake Presets define supported configure, build, and test workflows.
- A pinned vcpkg manifest is the default third-party dependency mechanism.
- SDL3 sits behind the Platform module for desktop and later Android window/input integration.
- HLSL plus DXC is the long-term shader source and compiler path, targeting SPIR-V and DXIL.
- VMA and D3D12MA are used after small native allocation exercises establish the underlying memory model.
- ImGui is a debug and profiling surface, not an editor framework.

These defaults may be replaced only by an Architecture Decision Record that explains the concrete problem and migration cost.

### 2.3 Explicit non-goals before the mobile milestone

- Full scene editor
- General-purpose ECS
- Gameplay scripting
- Physics, audio, networking, or multiplayer
- Console platforms
- Metal backend
- Production ray tracing renderer
- Broad asset-format support
- Custom replacements for mature general-purpose libraries

## 3. Architectural Direction

```text
Samples / Debug Tools
          |
Renderer + Material + Asset Runtime
          |
Shader Runtime + Render Graph
          |
         RHI
      /       \
 Vulkan     D3D12
 Backend    Backend
      \       /
 Platform + Foundation

Offline tools:
Shader Compiler -> compiled shader packages -> Shader Runtime
Asset Cooker    -> cooked platform assets  -> Asset Runtime
```

### 3.1 Ownership rules

- Platform owns windows, input, files, clocks, threads, and native platform handles. It does not own renderer policy.
- RHI owns API-facing objects, command recording, submission, synchronization primitives, and capability reporting.
- Backends own native Vulkan or D3D12 objects and translate RHI contracts into API calls.
- Render Graph owns frame-local dependency analysis, pass ordering, transient lifetimes, pass culling, and generated barriers.
- Shader System owns compilation inputs, reflection metadata, permutations, package versioning, and shader binary caches.
- Material owns renderer-level parameter semantics. It does not allocate native descriptors directly.
- Asset Pipeline converts source assets into platform-ready data. Renderer code owns the transition from cooked CPU data to GPU resources.

### 3.2 RHI design rules

- Do not create the RHI before the Vulkan reference renderer supplies real call sites.
- Expose concepts common to explicit modern APIs: queue, command list, resource, view, pipeline, binding layout, barrier, fence, and swapchain.
- Do not rename Vulkan types and call the result an RHI.
- Do not reduce both APIs to a lowest-common-denominator interface.
- Represent unsupported or optional behavior through an explicit capability structure.
- Separate resource description from memory allocation policy.
- Require callers or Render Graph passes to declare resource access. Do not rely on a hidden global state tracker guessing intent.
- Keep backend types out of Renderer, Material, Shader Runtime, and Render Graph public contracts.
- Avoid virtual dispatch for every draw command unless measurement demonstrates that the simpler command interface is inadequate.

### 3.3 Desktop and mobile rendering rule

The desktop path may use Vulkan Dynamic Rendering for simplicity, but Render Graph pass declarations must retain attachment lifetime and load/store semantics. This allows the Android backend to lower compatible passes into render passes or subpasses, preserve tile-local data, and avoid unnecessary external-memory traffic.

## 4. Milestone Roadmap

Each milestone produces one primary executable demo, an evidence package, and a learning note. A milestone is not complete when the code merely compiles.

### M0: Reproducible Engineering Baseline

**Learning goal:** Understand how a cross-platform native project owns its toolchain, dependencies, targets, and developer workflow.

**Core concepts:** CMake target ownership, presets, dependency pinning, build configurations, CI, sanitizers/static analysis, installation layout, and licenses.

**Implementation:**

- Establish Foundation, Platform, Samples, Tests, Tools, and Docs target boundaries.
- Add CMake configure/build/test presets for supported Win64 toolchains.
- Add pinned dependency manifests and a bootstrap path that does not depend on machine-specific absolute paths.
- Add logging, assertion policy, formatting, warnings, and a minimal test runner.
- Add a GitHub Actions job for configure, build, and CPU tests.

**Demo:** `OwlSandbox --sample smoke` opens a window, reports platform/toolchain information, and exits cleanly.

**Definition of Done:** A fresh clone on a second machine can configure, build, test, and run using documented commands. Debug and RelWithDebInfo are supported. No dependency relies on an unrecorded global path.

**Common pitfalls:** Building a large home-grown Foundation library, relying on a globally installed Vulkan SDK without detection, or hiding dependency downloads inside opaque configure logic.

**Trade-off:** A package manager adds bootstrap complexity but is preferable to unpinned `FetchContent` declarations and manually installed libraries.

### M1: Vulkan Bootstrap and Frame Lifecycle

**Learning goal:** Understand how a Vulkan application selects a device and presents frames.

**Core concepts:** Instance, validation, physical/logical device, feature chains, queue families, surface, swapchain, image acquisition, presentation, and swapchain recreation.

**Implementation:**

- Create the Vulkan instance, debug messenger, device, queues, surface, and swapchain.
- Add object debug names and structured Vulkan error logging.
- Implement a minimal frame loop and swapchain-dependent resource lifecycle.

**Demo:** Clear color followed by an indexed triangle.

**Definition of Done:** Resize, minimize, restore, and swapchain recreation work. The demo runs for at least 10,000 frames without validation errors and produces a valid RenderDoc capture.

**Common pitfalls:** A single initialization god object, assuming one queue family, rebuilding unrelated resources with the swapchain, or treating out-of-date swapchains as fatal errors.

**Trade-off:** Keep direct Vulkan calls visible and explicit even if the bootstrap code is verbose; abstraction is intentionally deferred.

### M2: GPU Resources, Memory, and Uploads

**Learning goal:** Understand the separation between resource objects, memory allocation, views, and data transfer.

**Core concepts:** Memory types and heaps, binding, alignment, coherent versus non-coherent memory, staging, persistent mapping, image layouts, mip generation, memory budgets, and deferred destruction.

**Implementation:**

- Implement Vulkan buffer, image, image view, sampler, and upload helpers.
- Add per-frame upload allocation and a GPU-safe deferred deletion queue.
- Build one small native allocation exercise, then integrate VMA for the maintained allocator path.
- Expose resource and allocation statistics in the debug UI.

**Demo:** A textured indexed mesh with generated mip levels.

**Definition of Done:** Resource stress tests show no leaks, use-after-free, missing flush/invalidate operations, or validation errors. Resource destruction remains correct for multiple frames in flight.

**Common pitfalls:** Writing a general-purpose allocator, ignoring alignment, destroying resources still referenced by submitted work, and conflating an image with its views.

**Trade-off:** The native allocation exercise maximizes learning; VMA prevents allocator maintenance from displacing renderer work.

### M3: Commands, Submission, and Synchronization

**Learning goal:** Build a precise mental model of CPU recording, GPU execution, memory dependencies, and queue coordination.

**Core concepts:** Command pools/buffers, submission, fences, binary and timeline semaphores, Synchronization2, stage/access masks, frames in flight, queue ownership, and timestamp queries.

**Implementation:**

- Add explicit frame contexts with command and transient allocation lifetimes.
- Add synchronization helpers that preserve Vulkan stage/access intent.
- Add GPU timestamps and a transfer-queue experiment.
- Keep the production renderer on one graphics queue until a separate queue demonstrates value.

**Demo:** Stream texture data while rendering an animated scene.

**Definition of Done:** Synchronization validation is clean. Uploads, resize, and shutdown are deterministic under stress. Captures show expected barriers and submission order.

**Common pitfalls:** `ALL_COMMANDS` barriers everywhere, CPU waits each frame, resetting command pools before completion, and assuming multiple queues imply overlap.

**Trade-off:** Manual barriers are retained long enough to learn them; automatic state planning is deferred to Render Graph.

### M4: Descriptor, Shader, and Pipeline Foundations

**Learning goal:** Understand how shader interfaces become resource bindings and immutable pipeline state.

**Core concepts:** Descriptor sets/layouts, pipeline layouts, push constants, SPIR-V, reflection, specialization/permutation, pipeline state, and cache identity.

**Implementation:**

- Establish HLSL register/space conventions and compile Vulkan shaders through DXC.
- Produce reflection metadata as a versioned build artifact.
- Add descriptor-pool allocation, binding-layout creation, and deterministic pipeline keys.
- Add development shader reload while preserving offline compilation for distributable builds.

**Demo:** Several material and pipeline variants rendered in one scene, with visible shader error reporting and reload.

**Definition of Done:** Shader dependency changes rebuild the correct artifacts. Diagnostics map to source. Pipeline cache keys include shader, permutation, layout, render target, and fixed-function state.

**Common pitfalls:** Runtime-only shader compilation, hidden register conventions, descriptor allocation per draw, incomplete cache keys, and uncontrolled permutation growth.

**Trade-off:** HLSL plus DXC reduces long-term dual-backend duplication; the project still inspects generated SPIR-V and DXIL rather than treating compilation as a black box.

### M5: Native Vulkan Reference Renderer

**Learning goal:** Turn isolated API mechanisms into a coherent renderer before deciding what the RHI must express.

**Core concepts:** Render data extraction, draw packets, camera/frame constants, depth, shadows, basic PBR, image-based lighting, HDR, tone mapping, and profiling.

**Implementation:**

- Load a focused subset of glTF.
- Implement a manually ordered Vulkan forward renderer with depth, one shadow path, metallic-roughness PBR, IBL, and tone mapping.
- Add debug views for attachments, normals, roughness, and GPU timings.

**Demo:** Damaged Helmet plus a representative environment scene such as Sponza.

**Definition of Done:** Fixed camera views have reference images. CPU time, per-pass GPU time, draw count, and memory usage are recorded. The render path is stable enough to provide several real RHI call sites.

**Common pitfalls:** Adding a full scene system, editor, animation stack, or advanced post-processing before the renderer has supplied stable architectural evidence.

**Trade-off:** Manual pass ordering is temporary duplication, but it prevents Render Graph and RHI abstractions from being invented without concrete requirements.

### M6: RHI v0 Extraction

**Learning goal:** Distinguish stable rendering semantics from Vulkan-specific mechanics.

**Core concepts:** API-independent contracts, backend ownership, capabilities, handles and lifetimes, queue submission, resource states, bindings, and error boundaries.

**Implementation:**

- Extract Device, Queue, CommandList, Buffer, Texture, View, Sampler, Pipeline, Binding, Fence, and Swapchain interfaces.
- Move native Vulkan implementation and translation code behind the Vulkan backend.
- Add explicit capability and format-support queries.
- Add CPU tests for enum/state translation, handle generations, and deferred destruction.

**Demo:** The M5 renderer runs through the RHI on Vulkan with the same scene and assets.

**Definition of Done:** No Vulkan type appears above the backend boundary. Reference images remain within tolerance and performance remains within an agreed small regression budget. Every RHI operation is justified by an existing use case.

**Common pitfalls:** Renaming Vulkan concepts, hiding all synchronization, returning backend pointers, virtualizing every tiny operation, and designing hypothetical Metal or console behavior.

**Trade-off:** The first RHI is deliberately revisable. Source compatibility is less important than correcting ownership and semantics when DX12 evidence arrives.

### M7: D3D12 Backend and RHI Validation

**Learning goal:** Use a second explicit API to test whether the RHI models GPU work rather than Vulkan syntax.

**Core concepts:** Root signatures, descriptor heaps, command allocators/lists, fences, resource states, enhanced barriers, heaps/placed resources, PSOs, and DXGI swapchains.

**Implementation:**

- Add D3D12 device, swapchain, resource, descriptor, pipeline, command, and synchronization implementations.
- Compile HLSL to DXIL and consume the same shader metadata contract.
- Use D3D12MA after a focused placed-resource exercise.
- Support enhanced barriers as a capability with a legacy barrier fallback.

**Demo:** Runtime selection with `--rhi=vulkan` or `--rhi=d3d12` renders the same fixed scene.

**Definition of Done:** Vulkan validation and D3D12 debug layers are clean. Backend reference images compare within documented tolerance. RHI changes are accompanied by an ADR explaining the cross-API mismatch they resolve.

**Common pitfalls:** Mapping Vulkan image layouts directly to D3D12, oversized root signatures, descriptor-heap reuse while GPU work is in flight, and resetting allocators before fence completion.

**Trade-off:** Backend parity is semantic and visual, not identical API call structure. Optional modern D3D12 paths cannot become unconditional RHI requirements.

### M8: Shader System and Material Model

**Learning goal:** Separate shader compilation, runtime programs, pipeline variants, and material parameters.

**Core concepts:** DXC dual targets, reflection normalization, permutation domains, binary/package caches, binding conventions, material templates, instances, and parameter updates.

**Implementation:**

- Build versioned shader packages containing SPIR-V, DXIL, reflection, dependencies, and permutation identity.
- Add Shader Runtime and pipeline-library caches.
- Add Material Template and Material Instance data without exposing native descriptor allocation.
- Add invalidation and clear diagnostics for shader/package incompatibility.

**Demo:** One scene switches material instances and selected shader features without backend-specific assets.

**Definition of Done:** Release builds consume precompiled packages. Rebuild invalidation is deterministic. Material behavior and layouts match across both backends.

**Common pitfalls:** Boolean-macro combinatorial explosion, compiler reflection differences leaking upward, materials owning GPU allocation, and caches without compiler/version identity.

**Trade-off:** A constrained permutation domain is less flexible than arbitrary macros but produces predictable build time and cache behavior.

### M9: Render Graph v1

**Learning goal:** Derive ordering, lifetimes, and barriers from declared resource usage.

**Core concepts:** Directed acyclic graphs, pass/resource declarations, imported and transient resources, liveness, pass culling, topological ordering, and barrier synthesis.

**Implementation:**

- Add explicit pass setup and execution phases.
- Require every pass read/write to declare access, stages, attachment use, and load/store intent.
- Compile a single-graphics-queue graph without resource aliasing.
- Export graph structure, lifetimes, and generated barriers for inspection.

**Demo:** A post-processing chain or small deferred-rendering laboratory with optional passes.

**Definition of Done:** Graph compilation is deterministic. Unused passes are removed. Cycles and undeclared resource use fail clearly. Generated barriers pass both API validation paths. CPU graph tests cover ordering and lifetime cases.

**Common pitfalls:** Hidden resource capture inside callbacks, making persistent assets graph-owned, implementing aliasing and multi-queue scheduling in v1, and optimizing away attachment semantics needed by mobile GPUs.

**Trade-off:** The initial graph gives up aliasing and async scheduling to make correctness and observability tractable.

### M10: Renderer v1

**Learning goal:** Build a modern, measurable desktop renderer on top of the stable systems.

**Core concepts:** Forward+, clustered lighting, cascaded shadows, IBL, HDR, tone mapping, post-processing, visibility, and frame-level performance budgets.

**Implementation:**

- Use Forward+ as the main renderer.
- Implement clustered light assignment, cascaded shadow maps, PBR/IBL, HDR, tone mapping, and a focused post stack.
- Keep deferred rendering as a Render Graph learning sample rather than the primary architecture.

**Demo:** A complex glTF scene with many dynamic lights and debug views for clusters, shadows, and attachments.

**Definition of Done:** Both backends match within visual tolerances. Per-pass timings, draw counts, light counts, and memory budgets are captured. Quality settings have documented costs.

**Common pitfalls:** Treating PBR as shader equations alone, adding effects without budgets, relying on backend-specific shader behavior, and hiding temporal state in frame-local graph resources.

**Trade-off:** Forward+ aligns better with a later mobile path; the deferred sample remains useful for learning G-buffer bandwidth and graph scheduling.

### M11: Bindless Resources

**Learning goal:** Understand descriptor indexing, global resource tables, indirection, and GPU-visible lifetime safety.

**Core concepts:** Vulkan descriptor indexing, D3D12 shader-visible heaps, partially bound arrays, update-after-bind, handle generations, residency, and capability tiers.

**Implementation:**

- Add global texture/resource tables with stable logical handles and delayed slot reuse.
- Remove per-draw descriptor updates from the bindless rendering path.
- Add capability-driven fallback bindings for unsupported devices.
- Add occupancy, allocation, and stale-handle diagnostics.

**Demo:** A scene with thousands of materials and textures using heterogeneous resources.

**Definition of Done:** No descriptor location is overwritten while submitted GPU work can reference it. Stale handles are detected in development builds. Both desktop backends pass stress tests and fallback behavior is tested.

**Common pitfalls:** Immediate slot reuse, unbounded tables without limits, assuming desktop support on Android, and confusing bindless access with resource residency.

**Trade-off:** Bindless is a renderer capability tier, not the only legal material path.

### M12: GPU-Driven Rendering

**Learning goal:** Move visibility and draw generation to the GPU while preserving correctness and debuggability.

**Core concepts:** Indirect draws, draw data, frustum and occlusion culling, HZB, LOD, prefix sums/compaction, buffer device address, and ExecuteIndirect.

**Implementation:**

- Add GPU instance data, culling, LOD selection, compacted visible lists, and indirect command generation.
- Add HZB-based occlusion with conservative behavior and visualization.
- Preserve a CPU-driven comparison and fallback path.

**Demo:** A large-instance scene with controllable culling and LOD modes.

**Definition of Done:** CPU submission cost scales weakly with visible draw count. GPU-generated visibility can be compared against the CPU reference. Measurements show the workload size where GPU-driven execution becomes beneficial.

**Common pitfalls:** CPU readback stalls, stale occlusion, tiny workloads dominated by compute overhead, platform-specific indirect-command assumptions, and loss of capture readability.

**Trade-off:** GPU-driven rendering is enabled by workload and capability, not imposed on every scene.

### M13: Render Graph v2 and Async Compute

**Learning goal:** Schedule dependent work across queues and prove actual overlap.

**Core concepts:** Multi-queue DAGs, timeline synchronization, cross-queue ownership, transient aliasing, hardware queue topology, contention, and overlap measurement.

**Implementation:**

- Extend graph compilation with compute/copy queue assignments and explicit cross-queue dependencies.
- Add transient-resource aliasing only after lifetime correctness is well tested.
- Move suitable work such as SSAO or Bloom to an experimental async path.
- Add schedule visualization and queue timestamps.

**Demo:** Toggle synchronous and asynchronous graph schedules for the same frame.

**Definition of Done:** A profiler proves useful overlap and frame-time improvement on at least one target GPU. Devices that regress automatically or configurably use the single-queue schedule. Both backends remain validation-clean.

**Common pitfalls:** Equating queue count with parallel hardware, creating bandwidth contention, over-synchronizing queue boundaries, and aliasing resources with incompatible lifetimes.

**Trade-off:** Correct single-queue execution remains the reference; async is an optimization policy, not a correctness dependency.

### M14: Android Vulkan Bring-up

**Learning goal:** Adapt the established engine contracts to Android lifecycle, device diversity, and constrained memory.

**Core concepts:** NDK application lifecycle, native surfaces, capability/extension matrices, present modes, asset packaging, ABI builds, memory budgets, and device loss/recreation.

**Implementation:**

- Add Android platform and build presets without forking Renderer code.
- Add Vulkan capability profiles rather than requiring desktop Vulkan 1.3 behavior.
- Package cooked assets and target shader artifacts for device deployment.
- Handle pause, resume, rotation, surface recreation, and application shutdown.

**Demo:** The same representative scene runs through the Vulkan RHI on physical Android devices.

**Definition of Done:** A documented fresh Android build installs and runs. Lifecycle transitions are repeatable. At least two different mobile GPU families are tested, with capability and memory reports captured.

**Common pitfalls:** Assuming desktop formats/features, treating surface loss as device failure, keeping unlimited resources resident, ignoring APK asset behavior, and validating only on an emulator.

**Trade-off:** Mobile uses feature profiles and fallbacks; raising the universal RHI baseline would reduce valuable device coverage.

### M15: Mobile Rendering and Optimization

**Learning goal:** Design for tile-based GPUs, bandwidth limits, thermal behavior, and energy efficiency.

**Core concepts:** Tile memory, render-pass merging, load/store operations, transient attachments, overdraw, bandwidth, ASTC, FP16, MSAA, dynamic resolution, thermal throttling, and sustained performance.

**Implementation:**

- Extend Render Graph lowering to preserve tile-local attachments and merge compatible work.
- Audit load/store operations, attachment formats, overdraw, texture compression, and transient memory.
- Add mobile quality tiers, dynamic resolution, and power-aware profiling scenarios.
- Compare Dynamic Rendering and render-pass/subpass paths where supported and meaningful.

**Demo:** A mobile-tuned Forward+ scene with bandwidth, overdraw, resolution, and quality debug modes.

**Definition of Done:** GPU counters demonstrate reduced external-memory traffic for selected optimizations. The renderer stays within a documented memory budget and completes a sustained thermal test without unexplained degradation. Results cover at least two mobile GPU families.

**Common pitfalls:** Optimizing only instantaneous FPS, preserving unnecessary attachment contents, desktop-sized G-buffers, excessive overdraw, and assuming the same winning path across mobile vendors.

**Trade-off:** Mobile may use different pass grouping, formats, and quality policies while preserving shared renderer intent and RHI contracts.

## 5. Cross-Cutting Definition of Done

Every milestone must satisfy all applicable items:

1. One primary demo exercises the new capability through the real runtime path.
2. Debug runs are clean under the relevant API validation/debug layer.
3. A representative RenderDoc, PIX, or Android GPU capture is inspected and recorded.
4. CPU time, GPU time, draw/dispatch count, and memory data are captured when performance is relevant.
5. Focused CPU tests cover deterministic logic such as cache keys, handles, graph compilation, and state translation.
6. GPU smoke tests exercise resource creation, submission, presentation, resize, and shutdown where the environment permits.
7. The clean-clone configure/build/test/run path remains valid.
8. The milestone adds a learning note explaining the problem, the API mechanisms, the chosen design, rejected alternatives, and remaining unknowns.
9. Public samples and documentation use only supported workflows.
10. Build success is not reported as proof of visual correctness, persistence, or performance.

## 6. Testing and Evidence Strategy

### 6.1 CPU tests

- Handle generation and stale-handle detection
- Format, usage, barrier, and capability translation
- Shader and pipeline cache keys
- Material layout and parameter packing
- Render Graph ordering, culling, lifetime, and cycle detection
- Asset package parsing and version rejection

### 6.2 GPU tests

- Backend initialization and shutdown
- Resource upload and readback
- Graphics and compute submission
- Swapchain resize and recreation
- Descriptor lifetime stress
- Fixed-scene reference images per backend and GPU class

Image comparisons use backend-specific tolerances. They are intended to catch large regressions, not require bit-identical floating-point output across vendors.

### 6.3 Performance evidence

Performance claims require a named scene, resolution, quality preset, GPU, driver, build configuration, capture tool, and before/after result. Microbenchmarks are used only for narrow mechanisms; representative frame measurements decide renderer policy.

## 7. Error Handling and Diagnostics

- Recoverable runtime conditions such as swapchain invalidation use explicit result values.
- Programming-contract violations use development assertions with actionable context.
- Native API failures retain the original API result and object/debug names.
- Device loss records recent submissions, resource names, enabled capabilities, and available diagnostic data.
- Shader and asset package version mismatches fail clearly instead of silently rebuilding in distributable builds.
- Backend validation messages are routed into structured logs and can be promoted to test failures.

## 8. Documentation Structure

- `docs/superpowers/specs/`: approved designs and milestone specifications
- `docs/roadmap/`: public roadmap summaries derived from approved specs
- `docs/adr/`: architecture decisions with context, alternatives, and consequences
- `docs/learning/`: API and rendering study notes
- `docs/building/`: supported clean-clone workflows and prerequisites
- `docs/profiling/`: capture procedures, benchmark scenes, and performance results

Each milestone receives its own design and implementation plan. This roadmap does not authorize implementing all milestones as one continuous change set.

## 9. Release Checkpoints

- `v0.1`: M5, Vulkan reference renderer
- `v0.2`: M7, validated Vulkan and D3D12 RHI
- `v0.3`: M10, modern desktop Renderer v1
- `v0.4`: M13, bindless, GPU-driven, and measured async experimentation
- `v0.5`: M15, Android and evidence-backed mobile renderer

A `v1.0` label is intentionally not assigned. It should represent a later stability contract based on real external use, not feature completion.

## 10. Roadmap Review Rules

The roadmap is revisited at each release checkpoint. Changes require evidence from implementation, validation, profiling, device support, or contributor experience. New features do not enter the active milestone unless they are required for its learning objective or Definition of Done.

The immediate next planning scope after this document is approved is M0 only: repository layout, dependency/bootstrap policy, exact presets, supported compilers, smoke executable behavior, CI matrix, and clean-machine acceptance procedure.

## 11. Reference Constraints

- [Vulkan Versions and Porting Guide](https://docs.vulkan.org/guide/latest/versions.html): keep current headers while documenting and checking the minimum supported Vulkan version.
- [Vulkan 1.3 Reference](https://docs.vulkan.org/refpages/latest/refpages/source/VK_VERSION_1_3.html): Vulkan 1.3 promotes Dynamic Rendering, Synchronization2, and related capabilities used by the desktop baseline.
- [D3D12 Enhanced Barriers Support](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options12): enhanced barriers are optional and require capability detection.
- [D3D12 Root Signature Limits](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits): root parameters have explicit size and indirection costs that constrain binding design.
- [Vulkan Tile-Based Rendering Best Practices](https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html): attachment lifetime, load/store operations, transient resources, and pass merging materially affect mobile bandwidth.

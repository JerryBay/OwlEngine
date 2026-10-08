# M2B Mip Sampling Implementation Plan

**Goal:** Keep a persistent mip demonstration window with explicit LOD selection and automatic minification, and verify actual lower-mip sampling.

**Spec:** `docs/superpowers/specs/2026-09-23-owlengine-m2-design.md`, M2B and full M2 acceptance.

**Architecture:** Sandbox owns animation and input. `VulkanTriangle` receives immutable per-draw `TextureDrawParameters` (scale and optional explicit LOD); a private textured pipeline writes an eight-byte vertex/fragment push-constant block. A seven-level image, full-range view, compatible sampler and one immutable descriptor are created once and survive resize. The existing `texture` sample keeps its one-level shader path.

**Constraints:** C++20/Vulkan 1.3; current graphics queue and frame loop; checked-in GLSL/SPIR-V; normal builds require no shader compiler. Work on master as authorized; no automatic Git commit. VS2026 is the local verification target.

### Task 1: Draw contract and resource setup

- [x] Write failing tests for bounded scale/LOD parameters and the mip frame path.
- [x] Add `TextureMipMode::{SingleLevel,Generate,ForceCpu}` and `TextureDrawParameters` to the sample lifecycle API.
- [x] Generate seven levels via existing upload; log actual GPU/CPU path; view all seven levels and allow LOD 0-6.
- [x] Add sample-local shaders with matching `float scale; float lod;` push constants (negative lod selects implicit `texture`, otherwise `textureLod`). Validate and check in SPIR-V.
- [x] Record push constants before the existing indexed draw; validate arguments before command recording. Keep resources immutable across LOD changes.

### Task 2: Persistent interactive sample

- [x] Add failing CLI tests for `texture-mips` and `texture-mips-cpu`, including rejection of trailing arguments.
- [x] Add narrow Platform key-state query and window title support; keep SDL types private.
- [x] Add sample entry and asset deployment. Digits 0-6 show fixed LOD at full scale; A selects implicit sampling and smooth zoom from scale 1 to 1/128; Space pauses/resumes zoom.
- [x] Show mode and controls in the title and log startup mip-generation coverage. Exit through the existing Escape/close path.

### Task 3: Verification and documentation

- [x] Run GPU frame integration for automatic and forced CPU generation, explicit and implicit sampling, resize/minimize/restore, and drain.
- [x] Build/test VS2026 Debug and RelWithDebInfo with Khronos synchronization validation active.
- [x] Capture or read back sampled pixels to establish lower-mip use; inspect image/view/sampler/descriptor state when RenderDoc is available.
- [x] Document launch commands, controls, measured evidence and unverified scope; update PROJECT.md and inspect the final diff.

## Verification scope

2026-10-08: both VS2026 configurations built and passed 117/117 opt-in CTest cases, without skips,
with Khronos synchronization/submit-time validation active and zero validation errors/warnings.
The sampled-pixel tests use the real mip shaders and private draw pipeline, a 64x64 UNORM target,
independent LOD 0/6 color oracles and implicit-versus-explicit LOD 4 comparison. Both generation
paths passed. The default test run keeps GPU tests opt-in.

RenderDoc 1.43 target control captured GPU LOD 0/6/automatic and CPU LOD 6. MCP file replay/query/
export succeeded (one six-index draw, seven-level 64x64 texture and eight-byte Shader block).
Fixed LOD 0 exported the checkerboard; fixed LOD 6 exported uniform average, with slight expected
GPU/CPU quantization differences. Synthetic digit/A/Space events changed modes and paused the
automatic frame near scale 1/128. Logs, captures and images are in ignored
`build/diagnostics/m2b-mip-sampling-20261008/`.

Initial high-integrity processes ignored Layer path overrides. Final diagnostics used a separate
medium-integrity child process; system registration was unchanged. MCP automatic launch remains
unsuccessful in this environment. Human acceptance of controls, VS2022, other hardware and native
driver-failure injection remain outside this run's verified scope.

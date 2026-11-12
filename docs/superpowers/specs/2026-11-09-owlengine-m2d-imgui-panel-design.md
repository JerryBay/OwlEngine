# M2D-4 Minimal ImGui Diagnostics Panel

**Status:** Confirmed, implemented and locally accepted on VS2026 Debug, 2026-11-12.
**Baseline:** `1d912a0` (M2D-3 heap budgets), clean master at inspection.
**Confirmed direction:** Add a small panel to `texture-mips` using vcpkg, SDL3 input and Vulkan
rendering. Reuse resource/budget snapshots; preserve existing frame completion and retirement.
No editor framework, docking, multi-viewport support or RHI extraction in this slice.

## Evidence and two corrections

- The pinned vcpkg baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d` provides ImGui **1.92.8#1**
  with `sdl3-binding` and `vulkan-binding`. No baseline update was required; the bindings are now
  declared in the manifest and installed through the existing configure script.
- `Platform::PumpEvents()` drains SDL events and handles quit/Esc; the sample polls keyboard state.
  There is currently no event observer. Platform's public API does not expose SDL.
- Renderer owns two fenced frame slots. `RecordFrame` draws the scene in dynamic rendering and
  then transitions to Present. `Recreate` drains existing uses before replacing dependent objects.
- Upstream Vulkan backend allocates native buffers/images/memory independently of Owl's VMA.
  Therefore existing VMA resource/allocation totals will not include UI resources. This must be
  explicit in the panel; process usage estimates can include them.
- The previous suggestion of no GPU allocations on any frame was too strong. The backend creates
  and grows buffers as needed, and its font texture updates submit work and wait for queue idle.
  The proposed acceptance is stable allocation behavior **after warm-up for unchanged UI content,
  capacity and font size**, allowing initialization and occasional growth/font updates.

## Ownership and flow

1. **Sandbox `DebugPanel`:** owns ImGuiContext, SDL3 ImGui backend and the panel's CPU code. It
   borrows the sample window, reads existing renderer snapshots, and owns no VkDevice or swapchain.
   It lives before Renderer creation and outlives Renderer destruction. Start with embedded default
   font and English technical labels, no external font asset or automatic imgui.ini persistence.
2. **Platform private SDL bridge:** introduce `SDLEventAccess`, analogous to `SDLWindowAccess`,
   with a synchronous pump accepting an event observer and userdata. Public `PumpEvents()` uses
   the same implementation with no observer. Observe each polled event before existing quit/Esc
   handling. No ImGui dependency in Platform, global event watch or second SDL polling loop.
3. **Renderer private `VulkanImGuiRenderer`:** owns the Vulkan ImGui backend and a dedicated
   descriptor pool; borrows Device and the sample context. It is destroyed after Renderer drains
   GPU work but before Device and before Sandbox destroys the context/SDL backend.
4. **Private `VulkanDebugUiAccess` bridge:** attach the context and a synchronous sample UI-builder
   callback before the first render. Only the sample and integration tests include this private
   header. The public Renderer header needs only the friend declaration, not SDL/ImGui/Vulkan types.
   Existing `RenderFrame()` remains the entrypoint; no general renderer callback registry is added.
5. Build UI only after a reusable frame slot and a drawable swapchain image are available. Invoke
   the sample callback after the Vulkan backend's NewFrame; it runs SDL3 NewFrame, ImGui NewFrame,
   panel construction and ImGui Render. Draw data is consumed synchronously during this frame and
   is never queued across frames. Deferred/minimized iterations still pump events but do not begin
   ImGui frames or advance its GPU buffer ring/texture retirement ages.

Implemented per-frame order:

```text
Pump SDL events -> forward to ImGui -> apply permitted sample keys
RenderFrame -> recreate if necessary -> wait slot -> collect/publish uploads -> acquire
  -> build UI from snapshots
  -> record geometry upload and scene rendering
  -> end scene rendering -> color write-to-read/write dependency
  -> begin UI rendering (LOAD existing color, STORE result) -> record ImGui -> end UI
  -> transition to Present -> submit with existing slot fence -> present
```

The second dynamic-rendering scope follows the previously proposed placement after the scene.
Its explicit color dependency and LOAD/STORE keep the scene visible beneath alpha-blended UI.
One submission/fence covers scene and UI draws. This costs an extra rendering scope; a later
mobile pass design should evaluate merging UI into the scene scope to avoid unnecessary tile
load/store. No general render-pass or Render Graph API is introduced here.

## Input, lifetime and statistics contracts

- Always forward input to ImGui. Gate sample `R`, `0-6`, `A` and Space using its keyboard-capture
  state from the previous UI frame. Continue updating the sample's key-edge bookkeeping while
  captured so releasing UI focus does not replay a held key. Window close and Esc retain the
  existing application-exit behavior. Keyboard navigation can exercise capture with this panel.
- Set backend `ApiVersion` to Vulkan 1.3 and use its dynamic-rendering pipeline for the actual
  swapchain color format. Use the pinned backend's separate sampled-image and sampler descriptor
  pool requirements, not older combined-image-sampler examples.
- Configure the backend's rotating draw buffers for the two existing fenced frames in flight.
  In this pinned backend the main-window `ImageCount` drives that ring; do not use the acquired
  image index as reuse proof or adopt ImGui's example swapchain helpers. Call RenderDrawData once
  per scene submission. A post-record failure is terminal for that renderer, preventing retry
  from silently advancing this ring. Verify reuse explicitly under synchronization validation.
- Extent-only resize updates viewport/framebuffer scale. Rebuild the UI pipeline on color-format
  change only after existing drain proof; keep owned pool and font resources where compatible.
  Swapchain image count and the two frame slots are separate concepts. Minimize/restore must not
  age UI GPU resources using unsubmitted CPU iterations.
- Context, callback userdata and window addresses remain stable. Renderer moves retain its Impl
  and UI owner. Shutdown and partial initialization must release backend resources/pool before
  their parents; failed optional UI initialization reports an error rather than a partial panel.
  Ordinary integration checks and our descriptor-pool creation return through the existing error
  path. **Confirmed refinement, 2026-11-12:** the unmodified backend has no complete rollback for
  native Vulkan failures (font image ownership is published late; upload buffers are raw locals).
  Its CheckVkResultFn therefore prints VkResult to stderr, flushes and aborts on failure. It must
  not return into invalid native work or throw through incomplete cleanup. This does not add
  recovery for device loss/OOM or alter the existing renderer's own error returns.
- Show frame submit/present/completion, upload capacity/payload, pending texture uploads and
  retirement counts; Buffer/Image requested/allocated bytes; VMA live/reserved bytes; per-heap
  capacity, usage and budget. Label the allocator section `Renderer VMA (excludes ImGui)` and
  budgets as process estimates. Do not reinterpret UI CPU allocations as GPU allocations.
- Update cheap counters and resource snapshots on rendered frames; refresh displayed heap-budget
  snapshots at a modest interval (proposed 250 ms). Temporary upload/retirement phases can finish
  before a human sees them; do not hold GPU work back solely to make a counter visible.
- Use a movable/collapsible panel near the upper right, preserving the texture's upper-left marker.
  Integrate only the GPU mip sample initially; no extra sample command or separate test executable.

## Confirmed decisions

1. **Ownership/input:** recommend the private event/renderer bridges and sample-owned context
   above. Platform continues to own polling; UI capture gates sample controls, while Esc still
   exits. This adds small private integration seams instead of making Platform depend on ImGui.
2. **GPU implementation/cost:** recommend the unmodified upstream Vulkan backend. Accept warm-up,
   occasional buffer/font growth and its font-update queue wait. A strict zero-growth/no-wait UI
   path would require a custom renderer/upload implementation and is a materially larger slice.
3. **Accounting:** recommend retaining VMA-only resource totals and visibly excluding ImGui.
   Counting UI GPU resources in those totals would require routing their allocations through our
   allocator (custom backend or maintained upstream patch), which is outside this minimal slice.

The user confirmed these three implementation contracts together on 2026-11-12. They are
chosen integration tradeoffs, not restrictions imposed by Vulkan.

## Focused acceptance

- Configure the existing VS2026 preset with the two pinned ImGui bindings; build Debug. Preserve
  VS2022 presets for the other workstation and Help/Smoke's delayed Vulkan-loader behavior.
- Extend existing event and GPU mip lifecycle checks for observer delivery, keyboard capture,
  UI-disabled behavior, moves, resize/minimize/restore and normal teardown. Add at most a focused
  UI case if the existing fixture cannot exercise the real backend; retain one OwlUnitTests target.
- Run affected Debug cases with actual Khronos synchronization validation, no errors/warnings.
  Do not repeat the full suite or both configurations by default. Select Help/Smoke compatibility
  checks because the dependency/link/input paths change.
- One visible GPU mip run: readable panel, stable scene, R replacements, key capture/release,
  collapse/expand, resize/minimize/restore and Esc exit. Visual acceptance is separate from tests.
- Inspect a bounded warm steady-state Vulkan API trace or capture for UI draw commands and resource
  reuse. VMA counters alone cannot prove absence of UI GPU allocations. Record first-use/growth
  separately; if steady-state allocation behavior is not measured, leave that claim unverified.
- Update PROJECT/README after implementation with actual evidence. No commit/push is implied by
  creating or approving this design.

## Implementation and verification record (2026-11-12)

- `DebugPanel` owns context, SDL3 input and displayed snapshots; it is declared before Renderer
  so normal and failed startup teardown preserve the borrowed context. `VulkanImGuiRenderer`
  owns the backend and pool inside the drained Renderer Impl. No public SDL/ImGui/Vulkan API
  was added to the renderer; `VulkanDebugUiAccess` is a private sample/test bridge.
- `SDLEventAccess` observes each polled event before Platform's quit/Esc decision. The sample
  uses previous-frame keyboard capture and updates key-edge state even while captured. Events
  always reach ImGui. No event callback is retained beyond the synchronous pump.
- The pinned ImGui backend uses separate image and sampler descriptor sets. Its two-entry draw
  buffer ring advances once per submitted drawable frame, independently of the three local WSI
  images. UI frames are not built during minimized/acquire-deferred iterations. Existing native
  submit/record failures remain terminal, so a failed record cannot silently desynchronize rings.
- Extent recreation preserves UI GPU resources; format recreation rebuilds only the UI pipeline
  after drain. The normal sample window is movable/collapsible and uses embedded English text,
  with no ini persistence. The CPU-mip and other sample paths remain UI-disabled.

| Evidence | Result and scope |
| --- | --- |
| Configure/build | Existing VS2026 configure installed ImGui 1.92.8#1 from the pinned baseline; Debug Sandbox and tests built successfully. |
| Focused tests | `[frame-lifecycle],[events]`: 7 cases, 58,284 assertions, no skips, actual Khronos synchronization/submit-time validation, zero errors/warnings. |
| UI lifecycle | Existing GPU-mip replacement fixture now draws ImGui with two frame slots, automatic/compatibility present policies, moves, resize, minimize/restore, collapse/expand, 32 bounded replacements and teardown. UI frame count matches successful submissions; minimized frames do not advance it. |
| Input | One new SDL event case proves observer delivery before exit handling. The UI fixture checks requested capture state; actual sample shortcut gating and keyboard return were user-verified. |
| Compatibility | Help CTest passed. Smoke created a window, loaded zero `vulkan-1.dll` modules, and exited 0 after normal close. |
| Visible acceptance | User reported panel/scene, R/LOD/zoom controls, Ctrl+Tab capture and return button, collapse/expand, resize/minimize/restore and Esc all normal. |
| Sample drain | Normal shutdown logged after 3,785 submits/presents; submitted/completed serials both 3,785; geometry payload 287,660 = 76 * 3,785; 16 replacements, zero pending uploads/retirements. VMA returned to 5 allocations / 154,784 bytes. |
| Native API trace | Frames 120-179: 60 presents, 120 dynamic-rendering scopes, 180 indexed draws (scene plus UI), zero memory allocations/resource creates/queue-idle waits. Startup frames 0-3 include 12 total allocations (scene and UI) and one font-upload queue-idle wait. This is bounded unchanged-content evidence, not a guarantee for growth or every font. |

The traced sample loaded validation and logged no validation errors. Two loader warnings announce
the explicitly enabled api_dump layer; they are explained diagnostic setup messages. The clean
0-warning synchronization result comes from the separate focused test run. The sample wrapper
did not preserve a numeric process exit code; its normal shutdown log and disappearance are the
exit evidence. Its swapchain generation remained 1, so confirmed native resize/recreation evidence
comes from automated tests rather than inferring it from the user's visual report.

Weakest-assumption check: backend buffer reuse must follow fenced frame slots, not acquired image
indices. Source inspection establishes its `ImageCount`-driven ring; the integrated case cycles
it with a different WSI image count and verifies UI/submission identity through deferred frames.
Synchronization validation and the API trace corroborate the normal path. Actual color-format
changes, host/GPU OOM and device-loss injection, VS2022 and RelWithDebInfo were not run. No new
RenderDoc capture or full regression was needed for this slice.

Local ignored evidence: `build/m2d4-build-debug.log`,
`build/diagnostics/m2d4-imgui-20261112/{focused-Debug.log,focused-Debug.json,panel.log,panel.err,api-dump.txt,trace-summary.json,compatibility.json}`.

## Sources

- Pinned port: `.tools/vcpkg/ports/imgui/{vcpkg.json,portfile.cmake,CMakeLists.txt}` at the baseline above.
- [ImGui 1.92.8 Vulkan backend](https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_vulkan.cpp)
  and [interface](https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_vulkan.h).
- [ImGui 1.92.8 SDL3 interface](https://github.com/ocornut/imgui/blob/v1.92.8/backends/imgui_impl_sdl3.h)
  and [input capture guidance](https://github.com/ocornut/imgui/blob/v1.92.8/docs/FAQ.md#q-how-can-i-tell-whether-to-dispatch-mousekeyboard-to-dear-imgui-or-my-application).

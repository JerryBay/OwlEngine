# M2C-1 Submission Progress Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for this approved slice, with a read-only independent review. Preserve the existing mip-sampling worktree changes.

**Goal:** Give each successful renderer graphics submission a stable serial and track fence-observed completion for later upload reuse and resource retirement.

**Architecture:** `VulkanFrameSlot` stores its last associated submission serial; `VulkanSubmissionProgress` owns the monotonic submitted/completed values. The existing renderer supplies actual submit/wait results. Graphics progress remains independent of acquire/present, and startup uploads retain their own blocking contract.

**Tech Stack:** C++20, native Vulkan 1.3/Synchronization2, existing two frame slots and graphics queue, Catch2/CMake.

**Spec:** `docs/superpowers/specs/2026-09-23-owlengine-m2-design.md`, Frame uploads and deferred destruction; user approved the submission-progress slice.

## Global Constraints

- Keep the current graphics queue/fences and single window-thread owner. No Timeline Semaphore, new queue, RHI, upload arena or retirement container in this slice.
- Successful submission assigns a nonzero serial; failed submission, unsuccessful wait and acquire/present results cannot advance graphics progress.
- Observe completion before fence reuse. Initial signaled fences have no submission. Never reuse a pending slot or wrap a serial to zero.
- Keep startup upload, swapchain/presentation release and device-loss teardown guarantees distinct from graphics serial completion.
- Work on master; preserve user changes and do not stage/commit automatically. Verify local VS2026 Debug/RelWithDebInfo; VS2022 belongs to the other workstation.

### Task 1: Submission metadata and state policy

**Files:** `engine/vulkan/src/VulkanFrame.h`, `VulkanFrame.cpp`, `tests/vulkan/FrameTests.cpp`.

**Interfaces:** Add `VulkanFrameSlot::submissionSerial`. Add private-module `VulkanSubmissionProgress::{CanSubmit(const VulkanFrameSlot&), RecordSubmitResult(VulkanFrameSlot&, VkResult), RecordWaitResult(VulkanFrameSlot&, VkResult), SubmittedSerial(), CompletedSerial()}`. Add `detail::NextSubmissionSerial(uint64_t) -> optional<uint64_t>` to enforce nonzero, nonwrapping serial allocation.

- [x] Add CPU tests with hand-derived serials 1/2/3: failed submit preserves state, successful submit associates a slot, failed waits retain pending work, later completion never regresses, reused slot gets a new identity, initial signaled/no-submission slot advances nothing. Check UINT64_MAX exhaustion.
- [x] Build the tests and observe failure for the missing production policy.
- [x] Implement the narrow result-driven policy; result failure preserves all state. `CanSubmit` must pass before native submission; successful completion clears pending after recording progress.
- [x] Run focused `[submission]` tests and inspect their failure-mode coverage.

### Task 2: Renderer integration and evidence

**Files:** `engine/vulkan/src/VulkanTriangle.cpp`, `engine/vulkan/include/owl/vulkan/VulkanTriangle.h`, `samples/owl_sandbox/src/VulkanSample.cpp`, `tests/vulkan/FrameTests.cpp`; update project/build/learning documents after verification.

- [x] Add failing integration expectations for Stats `submittedSerial`/`completedSerial`: submit count matches the serial domain, completion is bounded and monotonic, pending distance is at most two, pre-submit deferred frames allocate no serial, move/resize retains it, WaitIdle ends with equality. A successful submit followed by out-of-date Present retains its serial even when RenderFrame returns Deferred.
- [x] Preflight serial/slot availability after WaitSlot and before acquire. Feed actual vkQueueSubmit2 result to progress; successful vkWaitForFences and destruction fallback device-idle results publish graphics completion. Device loss does not masquerade as successful completion.
- [x] Snapshot serials into Stats and include them in the Sandbox shutdown log, after WaitIdle.
- [x] Format touched C++ and build both VS2026 configurations. Run full opt-in CTest with the process-local Khronos layer and synchronization/submit-time validation. Check actual layer loading, errors, warnings and skips.
- [x] Update PROJECT.md and learning notes with the serial/fence ownership distinction; record implemented, verified and unverified scope, and keep per-frame upload storage as the next slice.
- [x] Independently review result ordering, teardown, overflow, and separation from WSI; run git diff --check.

## Verification

- Red: the missing policy first failed compilation; buildable stubs then failed the CPU serial assertions. The real-GPU frame test failed with `submittedSerial == 0` while one frame had been submitted, before Stats integration.
- Green: five focused CPU cases pass (68 assertions). VS2026 Debug and RelWithDebInfo builds succeed; full opt-in CTest passes 122/122 in each configuration with no skips.
- Khronos validation was actually loaded with synchronization/submit-time checks enabled; both full logs contain zero validation errors and warnings. Local logs/scripts/results: `build/diagnostics/m2c-submission-progress-20261008/` (ignored).
- Formal Clear Sandbox normal-close runs exited with code 0 and logged serial equality: Debug 59/59, RelWithDebInfo 56/56. These elevated-process runs lacked the validation layer; their evidence is limited to the real Sandbox shutdown/log path. The separate full CTest runs establish validation coverage.
- Read-only independent review found and corrected one test assumption: Deferred after successful submit/out-of-date Present must retain that submission identity. No remaining blocking findings.
- Native failure injection, forced driver out-of-date results and VS2022 remain unverified. No new visual output or RenderDoc acceptance was required by this progress-only slice. Per-frame upload storage and resource retirement remain separate next slices.

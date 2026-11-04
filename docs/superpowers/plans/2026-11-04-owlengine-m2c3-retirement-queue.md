# M2C-3 Task 1 Resource Retirement Queue Implementation Plan

> Execute this task inline with the existing single-agent workflow. The user authorized work on
> master; preserve the preceding uncommitted M2C-2 acceptance documentation. Do not commit or push
> automatically. Use the test-driven-development cycle below.

**Date:** 2026-11-04

**Status:** Task 1 implemented and verified locally. This task implements the CPU ownership queue only. Native resource
replacement and renderer integration are subsequent tasks, not claimed by these tests.

**Goal:** Retain resource ownership until its last successful graphics use is known complete.

**Architecture:** A private bounded `detail::ResourceRetirementQueue<Resource, MaxPending>` owns
`unique_ptr<Resource>` entries and their final last-use serials. The renderer's existing
`VulkanSubmissionProgress` remains the completion authority. Resource owners remain responsible
for dependency destruction order and the renderer remains responsible for draining GPU work.

**Tech Stack:** C++20, standard library, Catch2, the existing OwlVulkan/OwlUnitTests targets.

**Spec:** [M2 design: frame uploads and deferred destruction](../specs/2026-09-23-owlengine-m2-design.md#frame-uploads-and-deferred-destruction).
**Prerequisite:** [Accepted M2C-2 upload storage](2026-10-09-owlengine-m2c2-frame-upload-storage.md).

## Constraints and Design

- Keep the queue private to `engine/vulkan/src`; no Foundation, Platform or public renderer API changes.
- One owner thread and one graphics submission domain. No Vulkan calls, fence polling, waits,
  new dependencies, queue families, RHI or resource registry in this task.
- `lastUseSerial` identifies the final successful submission using the resource, not its upload
  completion, a frame-slot index, a fence handle or a future submission. While active, the future
  caller must update last use after every successful draw submission. After retirement, no new
  use or resubmission of recorded references is permitted.
- `completedSerial` must come from that same queue's observed completion. The container compares
  supplied values; it cannot prove their provenance or detect a forged completion value.
- Zero last use means no outstanding submitted GPU use (including separate startup uploads).
  Such an entry is eligible at completion zero. Equality is sufficient for release.
- `TryRetire(serial, unique_ptr&)` transfers ownership only on success. Null input or full
  capacity returns false without modifying the input or any existing entry. No implicit collect,
  eviction, allocation growth or GPU wait; the caller can collect and retry later.
- `Collect(completedSerial)` scans all occupied entries and destroys exactly those with
  `lastUseSerial <= completedSerial`. Enqueue order need not match last-use order. Unrelated
  entries have no destruction-order guarantee; dependent objects belong in one resource owner.
- Fixed `std::array` slots bound bookkeeping storage and make enqueue/collect allocation-free.
  Operations are O(MaxPending), appropriate for the small renderer-owned queue. The eventual
  texture owner is separately allocated when creating a replacement, not during collection.
  A FIFO would require retirement order to match last-use order; an unbounded container adds
  growth and allocation failure during ownership transfer. Neither is needed here.
- Resources need not be movable: moving a unique pointer preserves the native owner's address.
  The queue itself is neither copyable nor movable; the current renderer moves its Impl pointer.
- Resource destructors must be nonthrowing and must not reenter the queue. Queue destruction
  releases remaining owners without waiting. As with current Vulkan resource wrappers, the
  outer owner must first establish completion or terminal device loss; an unresolved drain must
  not unwind GPU owners. Do not manufacture completion with UINT64_MAX to force shutdown cleanup.

## Task 1: CPU Ownership Queue

**Files:**

- Create `engine/vulkan/src/VulkanResourceRetirement.h`: private header-only typed queue.
- Create `tests/vulkan/ResourceRetirementTests.cpp`: ownership/destruction behavior without a GPU.
- Modify `engine/vulkan/CMakeLists.txt`: list the owned header for IDE visibility.
- Modify `tests/CMakeLists.txt`: register the source in the existing Vulkan test group.
- Update `PROJECT.md` and this plan with measured results and the next boundary.

**Interfaces:**

```cpp
namespace owl::vulkan::detail
{
    template <typename Resource, std::size_t MaxPending>
    class ResourceRetirementQueue
    {
    public:
        ResourceRetirementQueue() noexcept = default;
        ResourceRetirementQueue(const ResourceRetirementQueue&) = delete;
        ResourceRetirementQueue& operator=(const ResourceRetirementQueue&) = delete;
        ResourceRetirementQueue(ResourceRetirementQueue&&) = delete;
        ResourceRetirementQueue& operator=(ResourceRetirementQueue&&) = delete;
        [[nodiscard]] bool TryRetire(std::uint64_t lastUseSerial,
                                     std::unique_ptr<Resource>& resource) noexcept;
        std::size_t Collect(std::uint64_t completedSerial) noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;
    };
}
```

- [x] Write ownership tests first. Use nonmovable CPU objects whose destructors increment
  externally owned per-resource counters. Assertions observe object lifetime, pointer retention
  on rejection and destruction exactly once; they do not duplicate queue internals.

  | Regression caught | Explicit scenario / expected behavior |
  | --- | --- |
  | Early destruction or wrong equality boundary | Retire at 7; collect 0/6 keeps alive; collect 7 destroys once |
  | Head-of-line blocking or retiring insertion order | Enqueue 8,3,6; collect 3/7/8 releases only eligible owners |
  | Dropped entries with equal serials | Three owners at 5 all release at 5, never again |
  | Incorrect treatment of zero | Serial 0 releases at 0; serial 1 stays alive |
  | Overwriting a full queue / consuming failed input | Two pending entries; third remains caller-owned until space exists |
  | Empty ownership occupying a slot | Reject null input without changing pending entries |
  | Overflow in completion arithmetic | UINT64_MAX remains pending at max-1 and releases at max |
  | Stale identity after slot reuse | Release serial 2, reuse storage for 7, collect 2/6 must retain it |
  | Lost ownership at outer teardown | After externally established drain, remaining owners release once on scope exit |

  The critical test asserts both timing and ownership:

  ```cpp
  REQUIRE(queue.TryRetire(7, resource));
  CHECK_FALSE(resource);
  CHECK(queue.Collect(6) == 0);
  CHECK(destructions[0] == 0);
  CHECK(queue.Collect(7) == 1);
  CHECK(destructions[0] == 1);
  ```
- [x] Add buildable method stubs returning failure/zero and register the files. Build Debug
  OwlUnitTests and run `[retirement]`; require assertion failures because retirement is absent,
  not missing includes or syntax errors. Keep the failing output under ignored `build/`.
- [x] Implement fixed entries `{lastUseSerial, unique_ptr<Resource>}` and pending count. Reject
  null/full before ownership transfer, move into an empty slot, and collect with a direct
  comparison (no serial increment/subtraction). Clear each freed slot before later reuse.

  ```cpp
  if (entry.resource && entry.lastUseSerial <= completedSerial)
  {
      entry.resource.reset();
      entry.lastUseSerial = 0;
      --pendingCount_;
      ++released;
  }
  ```
- [x] Format both new C++ files using repository `.clang-format`. Build OwlUnitTests in VS2026
  Debug and RelWithDebInfo. Run new retirement and existing submission/upload-arena CPU tests
  through CTest in both configurations, with no skips. Inspect the final diff and do not claim
  native GPU retirement, visual acceptance or VS2022 verification from these CPU tests.

```powershell
cmake --build --preset windows-vs2026-debug --target OwlUnitTests
./build/windows-vs2026/tests/Debug/OwlUnitTests.exe '[retirement]'
cmake --build --preset windows-vs2026-relwithdebinfo --target OwlUnitTests
$cpuCases = '^(Resource retirement |Submission |Upload arena |Unsuccessful fence observations |Fence reuse |A failed submit |Frame upload byte accounting )'
ctest --preset windows-vs2026-debug -R $cpuCases --output-on-failure
ctest --preset windows-vs2026-relwithdebinfo -R $cpuCases --output-on-failure
git diff --check
```

## Following Integration Boundary

The next task extracts one immutable texture-binding owner (image, view, sampler and descriptor
allocation) from `VulkanTexturedQuadPipeline`, preserving startup rendering and releasing
descriptor dependents/view before image/allocation. The subsequent runtime replacement task will
retain the active owner's last successful graphics serial, prepare a completed replacement before
publication, and retire the old bundle atomically. Full capacity preserves the active resource;
collect uses observed completion from frame waits. Swapchain/present retirement stays separate.
New GPU/readback/stress and interactive acceptance will be required for that integration.

## Verification Record

- Design reviewed against current submission progress, frame teardown and texture ownership.
  The CPU queue does not replace any of those responsibilities.
- 2026-11-04: buildable stubs failed all 9 new cases (9 failed assertions, exit 42). The final
  Debug direct run passed 9 cases / 93 assertions. OwlUnitTests target builds succeeded in
  VS2026 Debug and RelWithDebInfo; focused CTest passed 28/28 per configuration without skips.
  This includes all new retirement cases, existing submission policies, upload-arena CPU cases
  and frame-upload byte accounting. Formatting and diff checks passed.
- Source review confirms enqueue/collect bookkeeping has fixed storage and no native calls,
  waits or dynamic growth; incomplete entries retain ownership and full rejection preserves
  the input. Shutdown safety still depends on the outer owner's completed-use/device-loss
  contract. No independent-agent review or native failure injection was performed.
- Logs: `build/m2c3-task1-{red-build,red-tests,green-tests,debug-build,relwithdebinfo-build,debug-ctest,relwithdebinfo-ctest}.log`
  (local, ignored). Full GPU CTest, new synchronization validation, Sandbox/RenderDoc acceptance
  and VS2022 were not run for this CPU-only task; the previous M2C-2 results remain historical.

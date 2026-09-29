# M1: from clear to an indexed triangle

The existing acquire, frame fences, image transitions, Dynamic Rendering scope, submit, and present
sequence stays in `VulkanTriangle`. Providing `VulkanTriangleOptions::triangleShaders` enables a
draw inside that rendering scope. Leaving it absent preserves the original clear-only sample.
`OwlSandbox` selects the sample and asset paths; Vulkan objects remain private to `OwlVulkan`.

## Geometry and memory

`VulkanTrianglePipeline` owns one immutable buffer containing three vertices followed by three
16-bit indices. A vertex is two position floats and three color floats. Buffer usage enables both
vertex and index reads; the two binding commands point to different byte offsets in the same buffer.
Vertex data occupies 60 bytes; indices occupy 6 bytes, beginning at offset 60.

At the M1 checkpoint, geometry used a directly mapped host-visible allocation. M2A replaces that
path with a temporary host-visible staging buffer and a device-local destination. M2B-1 retains
that path and moves allocation to VMA: `VulkanBuffer` owns a buffer and allocation slice, while
the renderer owns their allocator. `VulkanBufferUpload` owns the copy and its temporary resources.
The visible geometry and shaders are unchanged.

The startup path writes exactly 66 bytes to staging, flushes if its memory is non-coherent, submits
`vkCmdCopyBuffer`, and records a transfer-write to vertex/index-read barrier. A successful fence wait
allows staging and command resources to be released and destination ownership to pass to the pipeline.
The startup wait is deliberate; drawing does not introduce a per-frame idle wait.

Buffer creation describes logical size/use; allocation obeys native memory requirements, which may
require more bytes than the payload. `DEVICE_LOCAL` does not exclude `HOST_VISIBLE`, so this path
expresses required properties rather than assuming a specific discrete or unified memory topology.
A later D3D12 backend will express heap placement and resource states through its own contracts.

References: [host-to-device synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
and [mapped memory ranges](https://docs.vulkan.org/refpages/latest/refpages/source/VkMappedMemoryRange.html).

## Shader modules and graphics state

The checked-in GLSL/SPIR-V pair has explicit location-based interfaces and no descriptors or push
constants. `ReadTriangleSpirv` reads aligned `uint32_t` storage and rejects missing/truncated files
and invalid binary headers. It is not a semantic validator; generation uses SPIRV-Tools validation.
Temporary Shader Modules are released after graphics pipeline creation. Small CPU copies of the
SPIR-V remain available if the surface color format later changes.

The graphics pipeline connects the vertex layout, vertex/fragment entry points, triangle-list
assembly, rasterization, multisampling, and color output. Pipeline Layout is empty because this draw
has no shader resources. Depth, blending, culling, and MSAA are disabled for the first triangle.
The viewport has positive height: negative clip-space Y is toward the top of this sample's window.

Dynamic Rendering still requires pipeline color-format compatibility, expressed with
`VkPipelineRenderingCreateInfo`. Viewport and scissor are dynamic, so changing only window extent
reuses the pipeline. Changing the color format rebuilds it after the renderer has finished submitted
work. Geometry and device objects survive swapchain recreation. Pipeline, layout, buffer, and memory
are released before the allocator and device; the borrowed window remains alive through cleanup.

Reference: [Khronos Dynamic Rendering sample](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html).

## Verification boundary

CPU tests cover buffer description/host-access policy and the shader-file envelope. GPU tests
also check VMA mapping isolation, moves, allocation release and exact byte roundtrips. They exercise indexed
draw submission and rendered window lifecycle in both presentation-fence and compatibility modes.
Counters establish submitted work, not visual correctness. A visual check and RenderDoc draw/output
inspection are separate acceptance steps; a run without Validation Layer does not establish a
validation-clean result. See `PROJECT.md` for the accepted M1 evidence and remaining portability limits.

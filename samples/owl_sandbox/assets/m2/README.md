# M2 textured quad shaders

These sample-local GLSL 450 sources are shipped with precompiled SPIR-V binaries. The
Sandbox build copies the `.spv` files without requiring a shader compiler or Vulkan SDK.

Both entry points are `main`. The vertex shader reads clip-space `vec2` position at
location 0 and `vec2` UV at location 1, passing UV to fragment location 0. The fragment
shader samples the combined image sampler at descriptor set 0, binding 0 and writes to
color attachment location 0. The application must create a compatible descriptor set
layout and pipeline layout. There are no push constants.

## Regenerate

Use the same pinned [glslang 16.5.0 Windows x86-64 release](https://github.com/KhronosGroup/glslang/releases/tag/16.5.0)
as the M1 triangle shaders. Extract `glslang-16.5.0-windows-x86_64-release.zip` into
the ignored `.tools/glslang-16.5.0` directory. The archive SHA-256 is:

```text
06b71298b750268c127f2ee7ae0ef7525e2068120c6c8a3a08b2f58ca6f325ce
```

From the repository root, run:

```powershell
$textureCompiler = '.tools/glslang-16.5.0/bin/glslang.exe'
& $textureCompiler -V --target-env vulkan1.3 --spirv-val -e main -o samples/owl_sandbox/assets/m2/texture.vert.spv samples/owl_sandbox/assets/m2/texture.vert
if ($LASTEXITCODE -ne 0) { throw 'Vertex shader compilation or validation failed' }
& $textureCompiler -V --target-env vulkan1.3 --spirv-val -e main -o samples/owl_sandbox/assets/m2/texture.frag.spv samples/owl_sandbox/assets/m2/texture.frag
if ($LASTEXITCODE -ne 0) { throw 'Fragment shader compilation or validation failed' }
& $textureCompiler -l -q samples/owl_sandbox/assets/m2/texture.vert samples/owl_sandbox/assets/m2/texture.frag
if ($LASTEXITCODE -ne 0) { throw 'Shader stage interface validation failed' }
```

The target is Vulkan 1.3 / SPIR-V 1.6. `--spirv-val` validates each module, and the
link command checks the stage interface. Shader validation alone does not check the
application's descriptor binding or actual rendered pixels.

The checked-in binaries were generated with glslang 16.5.0 and validated with
`spirv-val --target-env vulkan1.3`:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `texture.vert.spv` | 976 | `9e1b014c3da92b84029cc77f9b9a029c90ac71df63b7c047ff4b475a2f8c0adc` |
| `texture.frag.spv` | 572 | `3674883755c5c16270ebbee573fca01e7253eec5363a6c7ac1c5abf304a2aa30` |

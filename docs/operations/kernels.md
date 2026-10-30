# Kernel containers

The GPU part's kernels are WGSL sources under `rhi/kernels/` (and the test-only ones under `test/kernels/`), compiled offline into Maul RHI shader containers that are committed as binary files (`<name>.container`). A build of the library needs no shader tools; changing a kernel does. This page says how to rebuild them and what CI checks. [gpu.md](../internals/gpu.md) explains why the kernels are built this way.

## What CI checks

CI has none of the shader tools, so it cannot rebuild a container. `python3 tools/gen_kernels.py --check` needs none of them: each committed container must carry, byte for byte, a fresh splice of its kernel's WGSL sources (the container keeps the WGSL beside the compiled forms). A kernel part edited without a rebuild fails the check. The check also refuses `atomicStore`, which the pinned SPIRV-Cross crosses into HLSL that DXC refuses; kernels write `_ = atomicExchange(&a, v);` instead.

Each directory of containers has a stamp, `containers.stamp`: the Maul RHI release whose tools cooked them, the flags their DXIL was compiled with, and each container's SHA-256. The container format has no place for these, and nothing in DXIL's bytes tells whether DXC compiled it with `-Gis`. Without it, DXC marks every float operation fast and folds the math, so a kernel on D3D12 would compute other than its source and the other APIs. The check refuses:

- a stamp of another release than `MAUL_VFX_RHI_VERSION` in `cmake/Rhi.cmake`;
- a stamp whose flags lack `-Gis`;
- a container that is missing, not in its stamp or not the bytes stamped;
- a stamped name that no kernel gives.

A container cooked again with a stale Maul RHI tree, or edited by hand, fails it even where its WGSL is current.

## Rebuilding

Needs, on the path:

- `naga` (the WGSL compiler of the wgpu project; the committed containers were built with naga 30.0.1),
- `spirv-val` and `spirv-cross` (the Vulkan SDK's, or Ubuntu's `spirv-tools` and `spirv-cross` packages; SPIRV-Cross 2021.01.15 as Ubuntu 24.04 ships it),
- `dxc` (DirectX Shader Compiler); with its validator `libdxil` beside it the DXIL is signed,

and a source tree of Maul RHI at the version the build uses (`MAUL_VFX_RHI_VERSION` in `cmake/Rhi.cmake`), for its container tools (`tools/mrhi_container.py`, `mrhi_dxil.py`, `mrhi_msl.py`).

```sh
git clone --depth 1 --branch v<version> https://github.com/Rawframe-Project/maul-rhi.git ../maul-rhi
python3 tools/gen_kernels.py ../maul-rhi            # every kernel
python3 tools/gen_kernels.py ../maul-rhi sim gas    # some, by name
```

The script refuses a tree of another release than the pin, and one whose `tools/mrhi_dxil.py` passes DXC no `-Gis`. Cooking some kernels by name needs a stamp of the same release and flags; after a move of Maul RHI, cook every kernel. The script then writes both stamps again: the digests of the containers it cooked, and the old stamp's for the rest, so a cook by name never vouches for a container it did not make.

For each kernel the script splices its sources, compiles them to SPIR-V with naga under its Restrict index policy (every index held in range, [gpu.md](../internals/gpu.md#kernels)), validates them for Vulkan 1.3, crosses them to DXIL and to Metal with Maul RHI's tools, and writes the container with the WGSL beside the compiled forms into `<name>.container`. Each kernel's bindings are described by `<name>.json` beside it.

The build turns each container into the C array the GPU part compiles: `cmake/KernelContainer.cmake` writes `kernels/<name>_container.h` under the build tree's `mvfx-kernels/`, on the host's CMake, so cross builds take the same step. The array is `s_<name in camel case>Container`, aligned to 8. C23's `#embed` would need no such step, but MSVC lacks it.

Commit the sources, the containers and the stamps together, then run the GPU part's suites on lavapipe (`ctest --preset linux-rhi-lavapipe`, see [development.md](development.md#the-gpu-parts-tests)): CI runs them only at night.

## Splicing

A line `// include: PATH.wgsl` in a kernel is replaced by `rhi/kernels/PATH.wgsl` before naga reads it, each file once. `shared/` holds what several kernels splice (functions with no bindings); a kernel's own directory (`sim/`, `gas/`) holds the parts only it splices, cut at their top-level declarations so no file passes the family's length limit. `rhi/kernels/shared/marching_words.wgsl` is written by `tools/gen_marching_cubes.py`, not by hand.

## Size

The containers are large (`sim.container` alone is over 9 MB) because each carries SPIR-V, DXIL, Metal and WGSL for every entry point. They are committed as bytes, not as C text, which would take six times the room and bring `sim` near GitHub's 100 MB file limit. Git treats them as binary (the family's `.gitattributes` detects their zero bytes), so their diffs are not shown; a change to a kernel rewrites its container whole. They are kept in the repository rather than fetched, because a build must not need the shader tools or the network.

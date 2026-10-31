# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI, the effect file format and the
simulation's results.

## [0.7.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.6.0...v0.7.0) (2026-10-31)


### ⚠ BREAKING CHANGES

* **api:** `mvfxRangeSource` is `mvfxMakeRangeSource`, the GPU part's `mvfxGpuSim_Expand*` calls are `mvfxGpuSim_Add*Expansion`, four reads take a `const mvfxContext*` and are not refused as busy, and `mvfxContext_Step`, `mvfxContext_StepTicks` and `MVFX_MAX_STEP_SECONDS` are declared in `maul-vfx/context.h`.
* **api:** the counters of `mvfxEmitterStats`, `mvfxEmitterPlan` and `mvfxWaterInfo`, and the GPU part's statistics words that mirror them, are renamed by the family's counter rule.
* the GPU part takes Maul RHI 0.15.0, whose `mrhiPass_WriteBuffer`, `mrhiResourcePlan` and `mrhiQuerySetDef` changed, so a program built against Maul RHI 0.14.0 must move with it.
* the GPU part takes Maul RHI 0.14.0, whose enumerations are `int32_t` and whose bit sets and `mrhiStructType` are `uint32_t`, so a program built against Maul RHI 0.13.0 must move with it.
* **api:** every kind typedef is `int32_t`, `mvfxGasWalls` and `mvfxWaterWalls` are `uint32_t`, the event masks are the new `uint32_t` `mvfxEventMask`, and the raw `space` fields are `mvfxSimulationSpace`, which moves to `modules.h`. Struct layouts change. `MVFX_FILE_VERSION` is 4 and the JSON form's format 5: files of version 3 are refused.
* **api:** `mvfxMesh_GetHullCenter` and `mvfxMesh_GetHullVertexCount` take a `const mvfxContext*`, no longer fail with `mvfx_errorCapacity` or count misuse, and give the hull of the positions a mesh was created with. A rigid hull no longer follows a `mvfxMesh_SetPositions` done before the effect is made.
* **api:** mvfxForce.strength is an mvfxScalarSource, resolved per instance per step: a range draws once per instance and force, a curve reads the emitter's loop age at the step's start, a parameter its x at each step. strengthFromParameter and parameter are removed. Effect files are version 3. The golden scene's hashes change, because the scene now drives forces by curves and a range.
* **family:** mvfxHash64 takes its byte count as int64_t. Its result is unchanged for every count that fits in int32_t.
* **api:** mvfxCreateEffect, mvfxWriteEffect, mvfxReadEffect and mvfxCreateEffectFromBytes take an optional mvfxEffectRefusal* as their last parameter, which names the refused field by its file tag path; mvfxGetEffectFieldPath writes that path as text. A def an effect file holds that the validator refuses is reported at the refused field's offset, no longer at the effect section's.
* **file:** effect files are written as tagged fields that leave defaults out (MVFX_FILE_VERSION 2); files of version 1 are refused with mvfx_errorVersion, and the effect JSON form moves to format 3, every field optional.
* **api:** public names follow the family's naming rule. mvfxResultName -> mvfxGetResultName mvfxGetContextMisuse -> mvfxContext_GetMisuse mvfxTaskFn -> mvfxTaskFunction mvfxEnqueueTaskFn -> mvfxEnqueueTaskFunction mvfxFinishTaskFn -> mvfxFinishTaskFunction mvfxModuleFn -> mvfxModuleFunction mvfxModule_Random -> mvfxDrawModuleRandom mvfxSrgbSteps -> mvfxGetSrgbSteps mvfxMesh_HullCenter -> mvfxMesh_GetHullCenter mvfxMesh_HullVertexCount -> mvfxMesh_GetHullVertexCount mvfxGpuSim_HostGridBytes -> mvfxContext_GetGpuHostGridBytes mvfxGpuSim_MeshBytes -> mvfxMesh_GetGpuBytes mvfxGpuSim_EmitterPasses -> mvfxGpuSim_GetEmitterPasses mvfxGpuSim_GasPasses -> mvfxGpuSim_GetGasPasses mvfxGpuSim_WaterPasses -> mvfxGpuSim_GetWaterPasses mvfxGpuSim_UploadBytes -> mvfxGpuSim_GetUploadBytes mvfxGpuSim_BoundsOf -> mvfxGpuSim_GetBounds field enabled -> enable field sceneQueries -> enableSceneQueries field depthImage -> enableDepthImage field hasTerrain -> enableTerrain the effect JSON form, format 1 -> 2 parameter values -> valuesOut of mvfxBakeCurlNoise parameter bytes -> bytesOut of mvfxWriteEffect parameter memory -> memoryOut of mvfxReadEffect parameter out -> wordsOut of mvfxDrawModuleRandom parameter steps -> stepsOut of mvfxGetSrgbSteps parameter out -> valuesOut of mvfxInstance_ReadGas, ReadGasAs, SampleGas, ReadWater parameter out -> samplesOut of mvfxInstance_SampleWater parameter records -> recordsOut of mvfxInstance_WriteParticles parameter lights -> lightsOut of mvfxInstance_WriteLights parameter items -> itemsOut of mvfxContext_OrderEmitters
* **water:** a water surface's heights and depths are held within MVFX_MAX_WATER_DEPTH (1e12 of its lengths) after each step's pushes, crossings, pours and sources.
* **output:** a refused output call zeroes the program's buffers over the span it is given, except for layout records with a NULL context or a stale layout. It gives counts, sizes and bounds of 0 even when the context or another output is NULL. A call refused as busy from host code writes none of its outputs, counts included, where it set some of them to 0 before.
* **effect:** mvfxCreateEffect and mvfxCreateEffectFromBytes set effectOut to the null id when the context is NULL; it was left as it was.
* maul-vfx-rhi builds against Maul RHI 0.13.0. A host that drives the GPU part calls Maul RHI by its 0.12.0 names (`mrhiPass_Begin`, `mrhiDevice_CompileFrame`, `mrhiRequest_TakeReadback` and the rest) and sets its device's `budgets`, as the guide shows. On D3D12, the kernels' float math now runs as written, so its results can differ from those of builds against Maul RHI 0.10.0.
* **gpu:** a sheet's triangle's cross product (mvfxCrossEdges) flushes to +0 every scaled edge component, product and difference below binary32's least normal, scaled or not. A triangle whose edges' products all fall there (components below about 2^-63, or parts of a scaled cross product below 4 square box lengths) now has a zero or smaller cross product, and a triangle whose cross product becomes 0 takes no drag.
* **math:** mvfxLengthOf, and with it every length the core measures (units' directions, sheets' areas, gas confinement's gradient), changes its bits for a vector whose largest component is below 2^-38 and some of whose squares fell below binary32's least normal. Such a length is now measured scaled by 2^100, where none of its squares is subnormal; every other vector keeps its bits.
* **gas:** a gas def whose cellSize is below MVFX_MIN_GAS_CELL (1e-6) is refused with mvfx_errorLimit.
* **effect:** a value past one of the library's named bounds (MVFX_MAX_*, MVFX_MIN_*) is refused with mvfx_errorLimit where it answered mvfx_errorInvalid, in defs and direct arguments alike; a def past a bound counts no misuse. NaN, infinities and ranges without a named constant still answer mvfx_errorInvalid.
* the GPU part takes Maul RHI 0.10.0, whose devices a host makes and passes it, and answers a device's refusal of a call from inside its own allocator with mvfx_errorBusy; a creation past a device limit within the adapter's now answers mvfx_errorCapacity where it answered mvfx_errorUnsupported.
* **step:** the step holds a moved particle's speed within MVFX_MAX_SPEED by scaling its velocity, keeping its heading, instead of clamping each axis; the host modules' check kills a speed past the bound rather than a component past it.
* **gpu:** mvfxCreateGpu and mvfxCreateGpuSim answer Maul RHI's status when the device refuses to make an object (mvfx_errorCapacity, mvfx_errorUnsupported, mvfx_errorState for a device not ready yet, or the new mvfx_errorPlatform), and mvfxCreateGpuSim answers mvfx_errorCapacity when its allocator fails inside a part, instead of mvfx_errorState.
* **family:** every error result but invalid and capacity takes the family's shared value (stale -3, state -6, version -7, limit -9, busy -10; missing -32), and the new unsupported (-4) and format (-8) are returned. A call from the context's host code answers mvfx_errorBusy instead of mvfx_errorState; a call needing a planned or an unplanned instance, or an effect the GPU part does not run, answers mvfx_errorUnsupported instead of mvfx_errorState; a creation past a table of the context's def answers mvfx_errorCapacity instead of mvfx_errorLimit; malformed effect file bytes answer mvfx_errorFormat instead of mvfx_errorInvalid; mvfxPlannedBirth's reserved[2] are eventKeyLow and eventKeyHigh.
* **step:** a rigid body turns by a Casimir splitting of its free flow on both paths, its spin and orientation together, instead of an explicit gyroscopic step followed by a turn of its orientation: every spinning body that is not of one moment about every axis moves differently, and angular drag slows a body's spin before it turns.
* **effect:** an effect, effect file or spawn request that gives a rigid sphere a Y or Z unlike its X, or a capsule a Z unlike its X, is refused with `mvfx_errorInvalid`. A host module that writes such a size now has its particle killed, counted in `valuesKilled`. Born sizes change on those axes: a drawn size takes X's draw there, and a parameter's or an inherited size takes X there.

### Added

* **api:** build a mesh's hull once, when the mesh is created ([#191](https://github.com/Rawframe-Project/maul-vfx/issues/191)) ([ad3f555](https://github.com/Rawframe-Project/maul-vfx/commit/ad3f55541cdce8a7fc2139e529ff567c6868c634)), closes [#187](https://github.com/Rawframe-Project/maul-vfx/issues/187)
* **api:** make a force's strength a value source ([#184](https://github.com/Rawframe-Project/maul-vfx/issues/184)) ([dbdf723](https://github.com/Rawframe-Project/maul-vfx/commit/dbdf7233bf0c71d4594bd705042f497f90b8610f)), closes [#183](https://github.com/Rawframe-Project/maul-vfx/issues/183)
* **api:** name the field an effect refusal is about ([#182](https://github.com/Rawframe-Project/maul-vfx/issues/182)) ([178a612](https://github.com/Rawframe-Project/maul-vfx/commit/178a6120cfe6db16e5724c17a39e407c608b7059)), closes [#181](https://github.com/Rawframe-Project/maul-vfx/issues/181)
* **file:** write effect files as tagged fields without defaults ([#180](https://github.com/Rawframe-Project/maul-vfx/issues/180)) ([a15fa6c](https://github.com/Rawframe-Project/maul-vfx/commit/a15fa6c4fffe1c7200b4f15ff6dd12acecb4d0f2)), closes [#75](https://github.com/Rawframe-Project/maul-vfx/issues/75)


### Fixed

* **api:** answer 0 for a NULL context's misuse and particles ([#218](https://github.com/Rawframe-Project/maul-vfx/issues/218)) ([2998954](https://github.com/Rawframe-Project/maul-vfx/commit/2998954348488e934d62d0c2189c83308df168ee)), closes [#217](https://github.com/Rawframe-Project/maul-vfx/issues/217)
* **api:** free an object's slot before its memory when it is destroyed ([#194](https://github.com/Rawframe-Project/maul-vfx/issues/194)) ([9525581](https://github.com/Rawframe-Project/maul-vfx/commit/95255813692244300ed20feca444a9d8cdd92378)), closes [#192](https://github.com/Rawframe-Project/maul-vfx/issues/192)
* **api:** plan a kill rule's least age only for the kind that reads it ([#220](https://github.com/Rawframe-Project/maul-vfx/issues/220)) ([24a648e](https://github.com/Rawframe-Project/maul-vfx/commit/24a648effb139f868a4896fa66354d3e6770e744)), closes [#219](https://github.com/Rawframe-Project/maul-vfx/issues/219)
* **effect:** give the null id when an effect is made without a context ([#172](https://github.com/Rawframe-Project/maul-vfx/issues/172)) ([52045a3](https://github.com/Rawframe-Project/maul-vfx/commit/52045a3e00de8c93cbf3afa3b4b5b45ebff59aaf)), closes [#169](https://github.com/Rawframe-Project/maul-vfx/issues/169)
* **effect:** hold a rigid sphere's and capsule's unused sizes at X ([#109](https://github.com/Rawframe-Project/maul-vfx/issues/109)) ([e6c72ee](https://github.com/Rawframe-Project/maul-vfx/commit/e6c72ee5e270ba192ca9a3938932b9ae937bb9f5)), closes [#71](https://github.com/Rawframe-Project/maul-vfx/issues/71)
* **effect:** name an out-of-range neighbor source as its field ([#204](https://github.com/Rawframe-Project/maul-vfx/issues/204)) ([b7fc26d](https://github.com/Rawframe-Project/maul-vfx/commit/b7fc26d6f71a0ecf4b889e9c2940777d5c820117)), closes [#193](https://github.com/Rawframe-Project/maul-vfx/issues/193)
* **gas:** hold a cell's scalars within 2^80 in its buoyancy ([#155](https://github.com/Rawframe-Project/maul-vfx/issues/155)) ([778764e](https://github.com/Rawframe-Project/maul-vfx/commit/778764edf714f06b2386180f043a93efeef33f00)), closes [#150](https://github.com/Rawframe-Project/maul-vfx/issues/150)
* **gas:** hold a gas's faces to the speed bound, its cells above 1e-6 ([#149](https://github.com/Rawframe-Project/maul-vfx/issues/149)) ([070ffd8](https://github.com/Rawframe-Project/maul-vfx/commit/070ffd8d56b13804da4aa31342c7f9f815f38451)), closes [#73](https://github.com/Rawframe-Project/maul-vfx/issues/73)
* **gpu:** answer the device's status when it refuses at creation ([#125](https://github.com/Rawframe-Project/maul-vfx/issues/125)) ([6e2563e](https://github.com/Rawframe-Project/maul-vfx/commit/6e2563e94e0275817f4c447eae0e8cad292b7a1e)), closes [#122](https://github.com/Rawframe-Project/maul-vfx/issues/122)
* **gpu:** flush a sheet's cross product below the least normal alike ([#154](https://github.com/Rawframe-Project/maul-vfx/issues/154)) ([0c26c1b](https://github.com/Rawframe-Project/maul-vfx/commit/0c26c1b12ac08e6207927370f35e0c6cf94327af)), closes [#148](https://github.com/Rawframe-Project/maul-vfx/issues/148)
* **gpu:** keep a rigid body's mass exponent apart ([#124](https://github.com/Rawframe-Project/maul-vfx/issues/124)) ([477e253](https://github.com/Rawframe-Project/maul-vfx/commit/477e25362896d90f5b0712ead29cdcbefa36512a)), closes [#112](https://github.com/Rawframe-Project/maul-vfx/issues/112)
* **gpu:** leave a NaN or infinite velocity to the kill rules ([#137](https://github.com/Rawframe-Project/maul-vfx/issues/137)) ([6d4aeb6](https://github.com/Rawframe-Project/maul-vfx/commit/6d4aeb6f79b771d8169eb815d8bd0a0ea68b8fb6)), closes [#132](https://github.com/Rawframe-Project/maul-vfx/issues/132)
* **gpu:** reduce every angle exactly in the kernels' sine and cosine ([#142](https://github.com/Rawframe-Project/maul-vfx/issues/142)) ([3b3cb38](https://github.com/Rawframe-Project/maul-vfx/commit/3b3cb38833016bcb749160e7397be3da98f18ae4)), closes [#134](https://github.com/Rawframe-Project/maul-vfx/issues/134)
* **gpu:** round a quotient below binary32's least normal as C divides ([#152](https://github.com/Rawframe-Project/maul-vfx/issues/152)) ([bb17a2b](https://github.com/Rawframe-Project/maul-vfx/commit/bb17a2b8b242752c200f4775f389f59e8adde074)), closes [#151](https://github.com/Rawframe-Project/maul-vfx/issues/151)
* **gpu:** take a grid cell past 2^24 in integers, as the core floors it ([#164](https://github.com/Rawframe-Project/maul-vfx/issues/164)) ([91275b4](https://github.com/Rawframe-Project/maul-vfx/commit/91275b40cfe49228cdbc20002093805f779ae238)), closes [#163](https://github.com/Rawframe-Project/maul-vfx/issues/163)
* **gpu:** test finiteness on the bits against the largest float ([#141](https://github.com/Rawframe-Project/maul-vfx/issues/141)) ([41d4700](https://github.com/Rawframe-Project/maul-vfx/commit/41d4700d09842e8863695ab017802dc4a8138daf)), closes [#139](https://github.com/Rawframe-Project/maul-vfx/issues/139)
* **gpu:** test finiteness on the bits where a device may fold it ([#140](https://github.com/Rawframe-Project/maul-vfx/issues/140)) ([ec5754e](https://github.com/Rawframe-Project/maul-vfx/commit/ec5754e8107e40a897417ed4882b68229d3d5619)), closes [#136](https://github.com/Rawframe-Project/maul-vfx/issues/136)
* **math:** measure a short vector scaled up, so no square flushes ([#159](https://github.com/Rawframe-Project/maul-vfx/issues/159)) ([e61fcef](https://github.com/Rawframe-Project/maul-vfx/commit/e61fcef06ffa4f1b8ed26237666aaa70e1f0d59e)), closes [#158](https://github.com/Rawframe-Project/maul-vfx/issues/158)
* **math:** reduce a sine's argument past 2^24 exactly ([#133](https://github.com/Rawframe-Project/maul-vfx/issues/133)) ([ccdbe02](https://github.com/Rawframe-Project/maul-vfx/commit/ccdbe02fb8489af2d6104cc1fa910933672d08b0)), closes [#129](https://github.com/Rawframe-Project/maul-vfx/issues/129)
* **modules:** hold a rigid body's spin to what its own step gives it ([#128](https://github.com/Rawframe-Project/maul-vfx/issues/128)) ([e81d82e](https://github.com/Rawframe-Project/maul-vfx/commit/e81d82ef3ded1c9f9f60d74721bf385c54a957f3)), closes [#107](https://github.com/Rawframe-Project/maul-vfx/issues/107)
* **output:** clear every output a refused call gives ([#173](https://github.com/Rawframe-Project/maul-vfx/issues/173)) ([18541e2](https://github.com/Rawframe-Project/maul-vfx/commit/18541e2ab350f81edb174863456422369f3cd0ec)), closes [#170](https://github.com/Rawframe-Project/maul-vfx/issues/170)
* **step:** hold a fast particle's speed along its heading ([#135](https://github.com/Rawframe-Project/maul-vfx/issues/135)) ([a7484f6](https://github.com/Rawframe-Project/maul-vfx/commit/a7484f6c8b820b7878417a0450e2c8a3690cec2f)), closes [#131](https://github.com/Rawframe-Project/maul-vfx/issues/131)
* **step:** hold a needle's inverse inertia at 2^40 in its units ([#113](https://github.com/Rawframe-Project/maul-vfx/issues/113)) ([7ee6631](https://github.com/Rawframe-Project/maul-vfx/commit/7ee66313dab32e4dd3cd26781f93a0126b45e8b5)), closes [#71](https://github.com/Rawframe-Project/maul-vfx/issues/71)
* **step:** hold a rigid body's velocity to the speed bound ([#130](https://github.com/Rawframe-Project/maul-vfx/issues/130)) ([f3f56c0](https://github.com/Rawframe-Project/maul-vfx/commit/f3f56c0c40854f7d9d75614e678cf62309bae38d)), closes [#127](https://github.com/Rawframe-Project/maul-vfx/issues/127)
* **step:** keep units finite where a squared length passes binary32 ([#146](https://github.com/Rawframe-Project/maul-vfx/issues/146)) ([5c4db51](https://github.com/Rawframe-Project/maul-vfx/commit/5c4db51c3e826d82283f49e81117244414dd45b0)), closes [#72](https://github.com/Rawframe-Project/maul-vfx/issues/72)
* **step:** solve a rigid body in its own units ([#106](https://github.com/Rawframe-Project/maul-vfx/issues/106)) ([499c782](https://github.com/Rawframe-Project/maul-vfx/commit/499c782529ded5156bfaf889d0b9a74b226be2fc)), closes [#71](https://github.com/Rawframe-Project/maul-vfx/issues/71)
* **step:** solve a rigid pair in the lighter body's units ([#110](https://github.com/Rawframe-Project/maul-vfx/issues/110)) ([316486a](https://github.com/Rawframe-Project/maul-vfx/commit/316486ae53fa05bce1bccbba6395d3f71ae9e2a0)), closes [#71](https://github.com/Rawframe-Project/maul-vfx/issues/71)
* **step:** turn rigid bodies by a Casimir splitting of their free flow ([#120](https://github.com/Rawframe-Project/maul-vfx/issues/120)) ([ad128f5](https://github.com/Rawframe-Project/maul-vfx/commit/ad128f5e758d48ccd79f8b0a1d83aafb22aabaa8))
* **water:** hold water heights and depths within MVFX_MAX_WATER_DEPTH ([#178](https://github.com/Rawframe-Project/maul-vfx/issues/178)) ([99b51c2](https://github.com/Rawframe-Project/maul-vfx/commit/99b51c27420548f3c9f51416cfaf0481d312014a)), closes [#74](https://github.com/Rawframe-Project/maul-vfx/issues/74)


### Changed

* **api:** make kinds int32_t and flag sets uint32_t ([#206](https://github.com/Rawframe-Project/maul-vfx/issues/206)) ([c533c68](https://github.com/Rawframe-Project/maul-vfx/commit/c533c684de6f6c06a77bc1a075e977d2e00ad310)), closes [#188](https://github.com/Rawframe-Project/maul-vfx/issues/188) [#205](https://github.com/Rawframe-Project/maul-vfx/issues/205)
* **api:** name makers and adders, take const reads, move the step ([#215](https://github.com/Rawframe-Project/maul-vfx/issues/215)) ([b3398d7](https://github.com/Rawframe-Project/maul-vfx/commit/b3398d76a13510d57baf6f9d38cd98cca786e619)), closes [#196](https://github.com/Rawframe-Project/maul-vfx/issues/196)
* **api:** name the API by the family rule ([#179](https://github.com/Rawframe-Project/maul-vfx/issues/179)) ([94d8b89](https://github.com/Rawframe-Project/maul-vfx/commit/94d8b8937194c0ef4d1e53a316c24d805a6c73bf)), closes [#177](https://github.com/Rawframe-Project/maul-vfx/issues/177)
* **api:** name the counters by what they count ([#212](https://github.com/Rawframe-Project/maul-vfx/issues/212)) ([ee63bc7](https://github.com/Rawframe-Project/maul-vfx/commit/ee63bc7069834c84007902716ea84c47265cf481)), closes [#195](https://github.com/Rawframe-Project/maul-vfx/issues/195)
* **effect:** refuse a value past a named bound as mvfx_errorLimit ([#145](https://github.com/Rawframe-Project/maul-vfx/issues/145)) ([214ffd1](https://github.com/Rawframe-Project/maul-vfx/commit/214ffd110d8bf427ee7741c440f808b9db37cb55)), closes [#121](https://github.com/Rawframe-Project/maul-vfx/issues/121)
* **family:** follow the family's results and stored bytes ([#123](https://github.com/Rawframe-Project/maul-vfx/issues/123)) ([8fc9752](https://github.com/Rawframe-Project/maul-vfx/commit/8fc975282f9fe91380ba32d73f90632a69b1b35b)), closes [#118](https://github.com/Rawframe-Project/maul-vfx/issues/118)
* **family:** sync .github v0.0.39, hash byte counts as int64_t ([#186](https://github.com/Rawframe-Project/maul-vfx/issues/186)) ([73b732d](https://github.com/Rawframe-Project/maul-vfx/commit/73b732d33901958acf1f3892b649946448e5a1bc)), closes [#185](https://github.com/Rawframe-Project/maul-vfx/issues/185)


### Build

* take Maul RHI 0.10.0 and answer its busy as mvfx_errorBusy ([#138](https://github.com/Rawframe-Project/maul-vfx/issues/138)) ([32c7e3d](https://github.com/Rawframe-Project/maul-vfx/commit/32c7e3dff0ffd0b2a89128853a4f937287fb2f18)), closes [#126](https://github.com/Rawframe-Project/maul-vfx/issues/126)
* take Maul RHI 0.13.0 and cook every kernel's DXIL exactly ([#166](https://github.com/Rawframe-Project/maul-vfx/issues/166)) ([85baed9](https://github.com/Rawframe-Project/maul-vfx/commit/85baed96750166dd277ca0b15511b6e76e25b996)), closes [#157](https://github.com/Rawframe-Project/maul-vfx/issues/157)
* take Maul RHI 0.14.0 ([#210](https://github.com/Rawframe-Project/maul-vfx/issues/210)) ([314e5fd](https://github.com/Rawframe-Project/maul-vfx/commit/314e5fd901e425c9e9d975f57f55991821f8c123)), closes [#207](https://github.com/Rawframe-Project/maul-vfx/issues/207)
* take Maul RHI 0.15.0 ([#214](https://github.com/Rawframe-Project/maul-vfx/issues/214)) ([ecf2886](https://github.com/Rawframe-Project/maul-vfx/commit/ecf2886865d5f59d02b6b40ce29e679622fae694)), closes [#213](https://github.com/Rawframe-Project/maul-vfx/issues/213)

## [0.6.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.5.0...v0.6.0) (2026-10-27)

### Changed

- **Breaking: the GPU part (`maul-vfx-rhi`) builds on Maul RHI 0.8.0**
  instead of 0.7.0. A host that uses Maul RHI itself moves to 0.8.0
  too: every new device texture reads as zeros in its first frame,
  cleared in Maul RHI's own zero pass, and drivers built outside Maul
  RHI's tree declare driver interface version 8. The GPU part's own
  textures are its 1-pixel depth and normal images, which no kernel
  reads, so its results are unchanged. The core, the simulation's
  results, the effect file format and the GPU part's tolerances are
  unchanged
  ([#101](https://github.com/Rawframe-Project/maul-vfx/pull/101)).
- **Breaking: `mvfxPlannedLiquid` gains `unit`, after `radius`.** Its
  `square`, `poly6`, `spiky`, `restInverse`, `softening`,
  `tensileInverse` and `tensileScale` are now in that unit rather than
  in the particles' space; see the fix below
  ([#95](https://github.com/Rawframe-Project/maul-vfx/pull/95)).
- On WebAssembly the float environment's entries compile away, so each
  public function that works with floats compiles to its worker alone:
  the web build's library is 1.4%
  smaller, with the same results. Native builds are unchanged
  ([#105](https://github.com/Rawframe-Project/maul-vfx/pull/105)).

### Fixed

- A particle liquid stays finite over its kernel radius's whole valid
  range. Before, a radius near either end, such as a world-space liquid
  under an instance's largest or smallest scale, made its kernel
  factors overflow or round to 0, and its particles' places and
  velocities went non-finite by the second step. The kernels now
  measure lengths in a unit, the power of two that leaves the radius
  from 1 to 2 in it. Every scaling is exact, so results within
  binary32's normal range are the same bits and the golden hashes are
  unchanged
  ([#95](https://github.com/Rawframe-Project/maul-vfx/pull/95)).
- The GPU part's liquid kernel moves a correction out of the unit by a
  product, exact on every device, rather than a division, which WGSL
  allows to be 2.5 ULP off
  ([#98](https://github.com/Rawframe-Project/maul-vfx/pull/98)).

## [0.5.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.4.0...v0.5.0) (2026-10-26)

### Changed

- **Breaking:** the GPU part (`maul-vfx-rhi`) now builds on Maul RHI
  0.7.0 instead of 0.3.0. Its public headers include Maul RHI's, and a
  host hands it its own Maul RHI device, so a host that uses Maul RHI
  itself moves to 0.7.0 too. What a host gets from Maul RHI 0.3.0 to
  0.7.0:
  - every new buffer reads as zeros until the frame graph writes it,
    on every backend, cleared in the first frame that imports it by a
    pass of Maul RHI's own that takes nothing from the frame's budgets;
    the buffers `mvfxGpuSim_GetParticles` hands out are therefore
    zeroed in the frame of the simulation's first step, though until
    that frame has run they stay undefined, as `sim.h` says;
  - shader containers are version 2, and version 1 is refused: the GPU
    part's kernel containers are rebuilt with 0.7.0's tools, and a
    host's own containers must be written again with them;
  - a pipeline cache is read only under the Maul RHI ABI version that
    wrote it, so caches from 0.6.0 or earlier are refused;
  - every failed call sets the pointers and ids it was given to NULL or
    the null id;
  - a program's pass ids start at 2;
  - on Direct3D 12, a transfer pass that clears runs on the compute
    queue, since copy lists cannot clear: the GPU part's contacts' and
    self-collision's write passes every step, a grid's the first time
    it empties its counts, an emitter's at the first step, and a gas's
    and a water surface's after a clear;
  - conformance mustpass lists for driver interface versions 4 to 6 are
    gone, and diagnostic codes are no longer promised to stay fixed.

  The core is unchanged, and so are the simulation's results, the
  effect file format and the GPU part's tolerances. On Direct3D 12
  (WARP) the drag factors stay at #84's measure: decay 8.01e-7, carry
  1.85e-7 and push 2.20e-7 against bounds of 4e-6, 2e-6 and 2e-6
  ([#91](https://github.com/Rawframe-Project/maul-vfx/pull/91)).
- `sim.h` and the user guide say that every buffer
  `mvfxGpuSim_GetParticles` gives is undefined until the simulation's
  first step is recorded and its frame has run
  ([#89](https://github.com/Rawframe-Project/maul-vfx/pull/89)).

### Fixed

- The GPU sample builds on Windows with warnings as errors again: it
  defines `_CRT_SECURE_NO_WARNINGS` for its `getenv`, as the tests do.
  The nightly Direct3D 12 job had stopped at its build since #69, in
  0.4.0 ([#92](https://github.com/Rawframe-Project/maul-vfx/pull/92)).

## [0.4.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.3.2...v0.4.0) (2026-10-26)

### Changed

- **Breaking:** every value a particle stores or shows now stays within
  a limit for its role, so every output (records, sprites, ribbons,
  lights, events, bounds) is finite for an effect without a solver of
  its own. Before, the products formed after a birth were unbounded: a
  size, alpha or rotation speed over life could reach 1e48, any space
  scale past 3.4 made a size drawn near `MVFX_MAX_DRAW` infinite, and a
  host module could leave a NaN. A limit applies where the value comes
  from:
  - **Refused** (`mvfx_errorInvalid`): an effect whose life curve's
    value times its largest baked sample passes the new
    `MVFX_MAX_LIFE_SCALE` (1e6), whose gradient key passes the new
    `MVFX_MAX_COLOR` (1e6), or whose value source can draw past its cap:
    a size's axis past `MVFX_MAX_EXTENT` over its size over life, a
    colour's channel past `MVFX_MAX_COLOR` over its colour over life, a
    spin or angular velocity past the new `MVFX_MAX_SPIN` (1e6 rad/s)
    over its rotation speed over life, a custom attribute past
    `MVFX_MAX_VALUE` over its scale, anything else past
    `MVFX_MAX_DRAW`. So are a parameter default or a value set with
    `mvfxInstance_SetParameter` that takes a source reading it past its
    cap, and a `mvfxSpawnRequest` colour or size past its emitter's cap.
  - **Held:** an event's inherited colour and size, to the spawning
    emitter's caps; each moved particle's velocity, within the new
    `MVFX_MAX_SPEED` (1e9) per axis, times the instance's scale for a
    world-space emitter.
  - **Killed:** a particle a host module leaves with a place past the
    new `MVFX_MAX_PLACE` (1e30) on an axis, a non-finite angle or
    orientation, a velocity past the hold or a value past its cap. It
    is counted in the new `mvfxEmitterStats.valuesKilled`, apart from
    `killed`.

  `mvfxOverLifeView` gains `colorCaps` and `sizeCaps`, so a host's own
  GPU path can hold inherited values as the library does. Every effect
  in the samples, the tests and the user guide is taken unchanged, and
  the largest value any of them uses, apart from the tests written at
  the limits, is at least 1,500 times below its limit. The check after
  a host module costs under 1 ns a particle a step (measured on 100,000
  sprites, one core, Release). The simulation's results for every
  effect still taken, the effect file's bytes and version, and the GPU
  part's tolerances are unchanged. Solvers (liquids, rigid particles,
  contacts and units, gases, water surfaces) keep their own state and
  are not yet bounded this way
  ([#79](https://github.com/Rawframe-Project/maul-vfx/pull/79)).

### Fixed

- The GPU part's drag factors no longer lose precision in binary32 just
  past their series cutoff. A birth's move, a particle with its own drag
  scale, a particle under winds with a falloff and a particle in water
  took a push up to 16.9 times off its value; the push is now within
  3.6e-7 of the core's (relative), and every factor within 9e-7. GPU
  results change within the GPU part's tolerances, which do not change.
  The CPU path, its results and the effect file are unchanged
  ([#84](https://github.com/Rawframe-Project/maul-vfx/pull/84)).
- An emitter whose speed limit is off no longer carries that limit's
  dampen into its step, so a NaN there no longer reaches the binary64
  exponential's conversion, which sanitizers reported as undefined
  behaviour; the binary64 exponential also gives a NaN back before
  converting. No output changed; results are unchanged
  ([#82](https://github.com/Rawframe-Project/maul-vfx/pull/82)).

### Security

- The GPU part (`MAUL_VFX_RHI=ON`) now writes every byte of the buffers
  `mvfxGpuSim_GetParticles` hands to the host at a simulation's first
  step, before any of them holds a step's results. After a clear and at
  a simulation's first step, the whole statistics record is uploaded,
  with every word the emitter does not keep at 0. At that first step
  the two particle buffers, the events buffer and the plexus links
  buffer are cleared. `sim.h` states that unkept statistics words hold
  0, and `sim.h` and the user guide state that slots and entries past
  each count are undefined and not to be read. Results, the effect file
  and the GPU part's tolerances are unchanged
  ([#86](https://github.com/Rawframe-Project/maul-vfx/pull/86),
  [#87](https://github.com/Rawframe-Project/maul-vfx/pull/87),
  [GHSA-hj35-99h7-jf52](https://github.com/Rawframe-Project/maul-vfx/security/advisories/GHSA-hj35-99h7-jf52)).

## [0.3.2](https://github.com/Rawframe-Project/maul-vfx/compare/v0.3.1...v0.3.2) (2026-10-25)

### Fixed

- `mvfxCreateGpuSim` no longer builds the 2 MB simulation struct as a
  temporary. An unoptimized build put that temporary on the stack,
  where its 2,160,296-byte frame overflowed a Windows thread's default
  1 MB stack on every call. Optimized builds were not affected.
  Results, the effect file and the GPU part's tolerances are unchanged
  ([#63](https://github.com/Rawframe-Project/maul-vfx/pull/63)).

## [0.3.1](https://github.com/Rawframe-Project/maul-vfx/compare/v0.3.0...v0.3.1) (2026-10-25)

### Fixed

- The GPU part's kernels are compiled with naga's Restrict index policy:
  an index into an array, a buffer or a texture is held in range, so a
  kernel's result is defined on every driver for any input, as the
  core's is. The kernel containers' headers grow by 5.9% (74,257,784 to
  78,646,288 bytes). Results, the effect file, the CPU path and the GPU
  part's tolerances are unchanged
  ([#60](https://github.com/Rawframe-Project/maul-vfx/pull/60)).

## [0.3.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.2.0...v0.3.0) (2026-10-25)

### Changed

- **Breaking:** every value a source draws is now finite. The new
  `MVFX_MAX_DRAW` (1e38, `value.h`) bounds it: a source's base at its
  largest size times the larger size of its ends must be at most that.
  The base is a curve's largest baked value (a cubic's rise between keys
  counts, not only its keys), a gradient's largest colour or alpha key,
  or a parameter's value. Refused:
  - an effect with a curve or gradient source past the bound, made from
    a def or read from an effect file;
  - an effect whose parameter defaults take a source reading them past
    it, each component checked against the ends of the sources that
    read it;
  - a value set with `mvfxInstance_SetParameter` that does the same,
    with `mvfx_errorInvalid`, counted as misuse; a component no source
    reads takes any finite value.

  Before, a curve key of 1e18 between ends of 1e30 drew an infinite
  value. The effect file's bytes and version are unchanged, and so are
  the simulation's results for every effect still taken. Values can
  still become infinite after a birth, through the over-life products (a
  size times its size over life, a colour times its colour over life, a
  rotation speed times its scale), which
  [#53](https://github.com/Rawframe-Project/maul-vfx/issues/53) tracks
  ([#55](https://github.com/Rawframe-Project/maul-vfx/pull/55)).

### Fixed

- The GPU part gives a NaN the value the CPU path gives wherever a
  kernel turns a float into an integer (a lattice cell, a sample count,
  a curve's sample, a capsule's core spans, a contact's tick). It holds
  the float as the core's `fminf(fmaxf(v, low), high)` does, taking a
  NaN to `low`, where before the integer depended on the driver. Every
  finite value converts as before, so results and the GPU part's
  tolerances are unchanged
  ([#54](https://github.com/Rawframe-Project/maul-vfx/pull/54)).

## [0.2.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.1.2...v0.2.0) (2026-10-25)

### Changed

- **Breaking:** the GPU part (`maul-vfx-rhi`) now builds on Maul RHI
  0.3.0 instead of 0.0.1. Its public headers include Maul RHI's, and a
  host hands it its own Maul RHI device, so a host that uses Maul RHI
  itself moves to 0.3.0 too. A Maul RHI driver built outside Maul RHI's
  tree must be rebuilt for its driver interface version 7. What a host
  gets from Maul RHI 0.1.0 to 0.3.0:
  - on Direct3D 12, the GPU part's transfer passes (uploads, clears and
    copies, all of buffers) run on a copy queue of their own, each after
    the passes it depends on; a device also takes more memory from the
    host's allocator when it is made;
  - a buffer read in a second read state takes the barrier it was
    missing, which can raise a frame's barrier count.

  The core is unchanged, and so are the simulation's results, the effect
  file format and the GPU part's tolerances
  ([#49](https://github.com/Rawframe-Project/maul-vfx/pull/49)).

### Fixed

- A planned instance's births that the host spawns before a looping
  emitter's start delay ends now report a `sourceAge` of 0 in their plan
  (`mvfxPlannedBirth`), the loop's start, instead of a negative share of
  the loop. What those births draw is unchanged
  ([#50](https://github.com/Rawframe-Project/maul-vfx/pull/50)).

## [0.1.2](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.1.2) (2026-10-24)

### Changed

- The allocator's `alloc` and `free` and the task hooks' `enqueueTask`
  and `finishTask` now run in the host thread's own floating-point
  setting, even when the library calls them from inside a step or an
  effect's creation. Called from there, they used to run in the
  library's. Host modules still run in the library's setting, since
  their results feed the simulation.

### Fixed

- Every public call that computes floats now runs in the library's
  floating-point setting (round to nearest, subnormals kept, no
  traps). It leaves the thread's setting as it found it, sticky
  exception flags included, and on 32-bit x86 the x87 control word too.
  Until 0.1.1 only a step, a prewarm, an effect's compile and the task
  ranges did this. Every other call ran in the host thread's setting,
  so a host with flush-to-zero, denormals-are-zero or another rounding
  mode could get different bits, and the call's arithmetic could leave
  exception flags behind. The same bits under any host setting now
  hold for:
  - effects: `mvfxCreateEffect`, `mvfxCreateEffectFromBytes`,
    `mvfxReadEffect`, `mvfxWriteEffect`, `mvfxEffect_GetEmitterView`;
  - meshes: `mvfxCreateMesh`, `mvfxMesh_SetPositions`,
    `mvfxMesh_HullCenter`, `mvfxMesh_HullVertexCount`;
  - instances: `mvfxCreateInstance`, `mvfxInstance_SetTransform`,
    `mvfxInstance_SetColliders`, `mvfxInstance_SetAttachFrames`,
    `mvfxInstance_SetDepthImage`, `mvfxInstance_WakeBodies`,
    `mvfxInstance_PushWater`, `mvfxInstance_AnswerSceneQueries`,
    `mvfxInstance_Stop`, `mvfxInstance_Restart`,
    `mvfxInstance_GetEmitterStats`;
  - steps: `mvfxContext_Step`, `mvfxContext_StepTicks`,
    `mvfxInstance_Prewarm`;
  - output: `mvfxInstance_WriteParticles`, `mvfxInstance_WriteLights`,
    `mvfxInstance_WriteRibbons`, `mvfxInstance_WritePlexus`,
    `mvfxInstance_WriteSurfaceMesh`, `mvfxInstance_ExpandSprites`,
    `mvfxInstance_ExpandRibbons`, `mvfxInstance_ExpandTubes`,
    `mvfxInstance_ExpandSheets`, `mvfxInstance_ExpandPlexus`,
    `mvfxInstance_GetBounds`, `mvfxContext_OrderEmitters`;
  - gases and water: `mvfxInstance_ReadGas`, `mvfxInstance_ReadGasAs`,
    `mvfxInstance_SampleGas`, `mvfxInstance_GetGasInfo`,
    `mvfxInstance_ReadWater`, `mvfxInstance_SampleWater`,
    `mvfxInstance_GetWaterInfo`;
  - planned instances: `mvfxInstance_GetEmitterPlan`,
    `mvfxInstance_GetEmitterStepPlan`, `mvfxInstance_GetGasPlan`,
    `mvfxInstance_GetGasStepPlan`, `mvfxInstance_GetWaterPlan`,
    `mvfxInstance_GetWaterStepPlan`, `mvfxInstance_SetLiquidSpeed`,
    `mvfxInstance_SetWaterSpeed`, `mvfxInstance_SetWaterCounts`;
  - without a context: `mvfxSrgbSteps`, `mvfxBakeCurlNoise`.

  The other public calls compute no float and need no change:
  defaults, counts, views of stored values, destruction, integer draws,
  grids' values (converted to binary16 by their bits) and inputs stored
  as given.

## [0.1.1](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.1.1) (2026-10-24)

### Fixed

- A context step ran the gas substeps (pours, splats, buoyancy, push,
  confinement, burning and solids) and the planning of planned gas
  instances in the host thread's floating-point mode. A host running
  with flush-to-zero, denormals-are-zero or another rounding mode could
  get different gas results. The whole step now runs in the library's
  mode, and the host's mode is restored after it.

## [0.1.0](https://github.com/Rawframe-Project/maul-vfx/compare/v0.0.1...v0.1.0) (2026-10-24)

### Changed

- **Breaking:** the GPU part's def, `mvfxGpuDef`, carries the ABI version after its cookie, as the context's def does. `mvfxDefaultGpuDef` is now built into the program from the headers it includes, no longer exported by the library. Rebuild against the 0.1.0 headers: `mvfxCreateGpu` refuses a def made with other headers with `mvfx_errorVersion`.
- **Breaking:** `mvfxMesh_HullCenter` takes a `bool* hullOut` before `centerOut`, which says whether the mesh has a hull a rigid particle can take. A mesh whose vertices span no volume, or whose hull has more than `MVFX_MAX_HULL_VERTICES` vertices, now gives `mvfx_success` with `hullOut` false and a zero center, where it gave `mvfx_errorInvalid`. `mvfxMesh_HullVertexCount` gives `mvfx_success` with a count of 0 for a mesh whose vertices span no volume.
- **Breaking:** the misuse count (`mvfxGetContextMisuse`) now counts only arguments a correct program never passes:
  - `mvfxCreateEffectFromBytes` counts nothing for a file refused for its contents, including what the file asks of the asset table's grids and meshes (a grid of the wrong shape, a mesh without the shape's mode or without a hull).
  - `mvfxCreateEffectFromBytes` counts one for each invalid argument of the program's own: NULL bytes with a size, a negative size, and a NULL or invalid asset table.
  - `mvfxCreateEffect` and the hull queries count nothing for a mesh with no hull, since its vertices are a model's data.
  - A program that asserts the count stays at zero now fails only on its own mistakes, never on a bad file or model.

## [0.0.1](https://github.com/Rawframe-Project/maul-vfx/releases/tag/v0.0.1) (2026-10-23)

The first release: visual effects simulated on the CPU with the same
bits on every platform and thread count, handed to any renderer as
data, and an optional GPU part that runs them through Maul RHI.

### Added

- Contexts that take the program's allocator, task hooks and limits;
  effects compiled once from defs; instances that play them at a
  place, with a seed, a quality level and an off-screen policy; steps
  in ticks of 2^-20 seconds whose results do not depend on how time
  is split.
- Emission by rate and rate curve, bursts, distance travelled, other
  emitters' events and the program's own requests, from points,
  lines, circles, spheres, hemispheres, boxes, cones and meshes.
- Motion integrated exactly under gravity and drag; wind, attractors,
  vortices, curl-noise turbulence (analytic or baked into a grid),
  vector fields and a gas's velocity; velocity modules, values over
  life, kill rules, custom attributes, instance parameters and the
  program's own modules.
- Collision with planes, spheres, boxes, heightfields and distance
  grids along each step's whole path, with the program's scene through
  queries it answers, and with the camera's depth image.
- Neighbor grids, flocking and steering, pair forces, contacts and
  plexus links; ropes, chains and cloth by XPBD that pin, tear, take
  the wind and keep off themselves; rigid spheres, capsules, boxes and
  convex hulls that tumble, stack and sleep.
- Smoke and fire on grids, liquids by position-based fluids or APIC,
  surfaces of particles meshed by marching cubes, and water surfaces
  of waves or shallow water that particles float on, push and pour
  into.
- Output in the program's own layouts: particle records sorted
  stably, quads, ribbons, tubes, beams, plexus strips, lights, sheet
  meshes, surface meshes, gas and water fields and samples, bounds and
  draw order.
- Events, sub-emitters, quality levels and a particle budget.
- Effect files, read as hostile input with the offset of any fault,
  and a JSON form for diffs and authoring (`tools/effect_json.py`).
- The GPU part, `maul-vfx-rhi`: planned instances simulated in WGSL
  kernels on Vulkan, Direct3D 12, Metal and WebGPU through Maul RHI,
  equal to the CPU path in every count and within stated tolerances
  in its floats, with every output written on the device.
- Support for Linux (x86-64 and arm64), macOS, Windows and the web.

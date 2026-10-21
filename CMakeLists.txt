cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-vfx)
set(MAUL_API_PREFIX mvfx)
set(MAUL_MACRO_PREFIX MVFX)
set(MAUL_TITLE "Maul VFX")
set(MAUL_PREFIX MAUL_VFX)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-vfx/base.h MVFX MAUL_VERSION)
project(maul-vfx
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Particles, ribbons, trails and beams simulated deterministically for any renderer"
    LANGUAGES C)

option(MAUL_VFX_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_VFX_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_VFX_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_VFX_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_VFX_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_VFX_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_VFX_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_VFX_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_VFX_FUZZ "Build the libFuzzer targets (Clang)" OFF)
option(MAUL_VFX_RHI "Build maul-vfx-rhi, the GPU part on Maul RHI" OFF)

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_VFX_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-vfx ${MAUL_LIBRARY_KIND}
    src/allocator.c
    src/aero.c
    src/appearance.c
    src/attach.c
    src/beam.c
    src/bounds.c
    src/collide.c
    src/base.c
    src/birth.c
    src/context.c
    src/convex.c
    src/curve.c
    src/curve_integral.c
    src/death_heap.c
    src/depth.c
    src/direction.c
    src/effect.c
    src/effect_gas.c
    src/effect_parts.c
    src/effect_api.c
    src/elementary.c
    src/expand.c
    src/force.c
    src/grid.c
    src/grid_api.c
    src/integrate.c
    src/kill.c
    src/layout.c
    src/layout_api.c
    src/life.c
    src/liquid.c
    src/liquid_kernel.c
    src/emitter_step.c
    src/planner.c
    src/plan_resolved.c
    src/emitter_view.c
    src/event.c
    src/neighbor.c
    src/contact.c
    src/plexus.c
    src/unit.c
    src/steer.c
    src/event_spawn.c
    src/float_mode.c
    src/gas.c
    src/gas_api.c
    src/gas_plan.c
    src/gas_view.c
    src/gas_forces.c
    src/gas_lattice.c
    src/gas_liquid.c
    src/box.c
    src/read_format.c
    src/water.c
    src/water_api.c
    src/water_step.c
    src/water_shallow.c
    src/water_ground.c
    src/water_couple.c
    src/file.c
    src/file_codec.c
    src/file_fields.c
    src/gas_surface.c
    src/marching_table.c
    src/surface_mesh.c
    src/gas_pressure.c
    src/gas_read.c
    src/gas_solid.c
    src/gas_splat.c
    src/gas_step.c
    src/gradient.c
    src/hash.c
    src/host_module.c
    src/hull.c
    src/instance.c
    src/instance_api.c
    src/instance_inputs.c
    src/mesh.c
    src/mesh_api.c
    src/noise.c
    src/output.c
    src/over_life.c
    src/ribbon.c
    src/rigid.c
    src/rigid_body.c
    src/rigid_box.c
    src/rigid_colour.c
    src/rigid_contact.c
    src/rigid_pair.c
    src/rigid_shape.c
    src/rigid_sleep.c
    src/rigid_wake.c
    src/scene_query.c
    src/self.c
    src/shape.c
    src/slots.c
    src/sort.c
    src/space.c
    src/spawn.c
    src/step.c
    src/tasks.c
    src/tube.c
    src/value.c
    src/velocity.c
    src/velocity_modules.c
    src/view.c)
maul_configure_library(maul-vfx)
# sqrt, floor, ceil and the other exactly rounded functions the library
# takes from libm (mvfx-0001); the tests' oracles use more of it.
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(maul-vfx PUBLIC m)
endif()

if(MAUL_VFX_RHI)
    include(cmake/Rhi.cmake)
endif()

if(MAUL_VFX_BUILD_TESTS)
    enable_testing()
    maul_add_test(aero WHITEBOX)
    maul_add_test(allocator WHITEBOX)
    maul_add_test(attach WHITEBOX)
    maul_add_test(base)
    maul_add_test(beam)
    maul_add_test(chain)
    maul_add_test(self WHITEBOX)
    maul_add_test(sheet)
    maul_add_test(bounds)
    maul_add_test(collide WHITEBOX)
    maul_add_test(collide_grid WHITEBOX)
    maul_add_test(contact)
    maul_add_test(context)
    maul_add_test(curve WHITEBOX)
    maul_add_test(curve_integral WHITEBOX)
    maul_add_test(depth WHITEBOX)
    maul_add_test(effect)
    maul_add_test(effect_plan WHITEBOX)
    maul_add_test(force WHITEBOX)
    maul_add_test(gas WHITEBOX)
    maul_add_test(gas_pressure WHITEBOX)
    maul_add_test(gas_forces WHITEBOX)
    maul_add_test(gas_solid WHITEBOX)
    maul_add_test(gas_push WHITEBOX)
    maul_add_test(gas_couple WHITEBOX)
    maul_add_test(gas_liquid WHITEBOX)
    maul_add_test(gas_surface WHITEBOX)
    maul_add_test(surface_mesh WHITEBOX)
    maul_add_test(gas_read WHITEBOX)
    maul_add_test(water)
    maul_add_test(water_shallow WHITEBOX)
    maul_add_test(water_couple WHITEBOX)
    maul_add_test(liquid WHITEBOX)
    maul_add_test(liquid_flow WHITEBOX)
    maul_add_test(elementary WHITEBOX)
    maul_add_test(emitter_step WHITEBOX)
    maul_add_test(plan WHITEBOX)
    maul_add_test(plan_gas WHITEBOX)
    maul_add_test(plan_liquid WHITEBOX)
    maul_add_test(event WHITEBOX)
    maul_add_test(event_spawn WHITEBOX)
    maul_add_test(expand)
    maul_add_test(expand_ribbon)
    maul_add_test(elementary_sweep WHITEBOX MANUAL)
    maul_add_test(golden WHITEBOX)
    maul_add_test(file WHITEBOX)
    maul_add_test(gradient WHITEBOX)
    maul_add_test(grid WHITEBOX)
    maul_add_test(half WHITEBOX)
    maul_add_test(host WHITEBOX)
    maul_add_test(host_module WHITEBOX)
    maul_add_test(integrate WHITEBOX)
    maul_add_test(kill WHITEBOX)
    maul_add_test(lights)
    maul_add_test(instance)
    maul_add_test(mesh WHITEBOX)
    maul_add_test(noise WHITEBOX)
    maul_add_test(output WHITEBOX)
    maul_add_test(over_life WHITEBOX)
    maul_add_test(motion WHITEBOX)
    maul_add_test(half_sweep WHITEBOX MANUAL)
    maul_add_test(neighbor)
    maul_add_test(offscreen)
    maul_add_test(pair_force)
    maul_add_test(plexus)
    maul_add_test(quality)
    maul_add_test(random WHITEBOX)
    maul_add_test(ribbon)
    maul_add_test(ribbon_shape)
    maul_add_test(scene_query WHITEBOX)
    maul_add_test(shape WHITEBOX)
    maul_add_test(slots WHITEBOX)
    maul_add_test(sort WHITEBOX)
    maul_add_test(spawn WHITEBOX)
    maul_add_test(step)
    maul_add_test(tear WHITEBOX)
    maul_add_test(rigid WHITEBOX)
    maul_add_test(rigid_contact WHITEBOX)
    maul_add_test(rigid_pair WHITEBOX)
    maul_add_test(hull WHITEBOX)
    maul_add_test(convex WHITEBOX)
    maul_add_test(rigid_hull WHITEBOX)
    maul_add_test(rigid_sleep WHITEBOX)
    maul_add_test(steering)
    maul_add_test(trail)
    maul_add_test(tube)
    maul_add_test(value WHITEBOX)
    maul_add_test(velocity_modules WHITEBOX)
    maul_add_test(tasks WHITEBOX)
    maul_add_test(host_calls)
    # The web build has no threads; the twins need four.
    if(NOT EMSCRIPTEN)
        maul_add_test(twins WHITEBOX POSIX)
    endif()
    if(MAUL_VFX_RHI)
        # The GPU part's kernels on a real device; skipped (77) without an
        # adapter unless MVFX_RHI_REQUIRED is set.
        maul_add_test(rhi_kernels WHITEBOX)
        target_link_libraries(test_rhi_kernels PRIVATE maul-vfx-rhi)
        target_include_directories(test_rhi_kernels PRIVATE ${PROJECT_SOURCE_DIR}/rhi/src
            ${PROJECT_SOURCE_DIR}/test/kernels)
        set_tests_properties(rhi_kernels PROPERTIES SKIP_RETURN_CODE 77)
        # Planned instances on the device beside the CPU path, through the
        # tests' shared harness: their particles, and their records.
        foreach(suite rhi_sim rhi_records rhi_events rhi_spawn rhi_shapes rhi_ribbons
                rhi_ribbon_points rhi_beams rhi_lights rhi_sprites rhi_neighbors rhi_steering
                rhi_contacts rhi_plexus rhi_units rhi_rigid rhi_pairs rhi_gas rhi_liquid)
            maul_add_test(${suite} WHITEBOX)
            target_sources(test_${suite} PRIVATE ${PROJECT_SOURCE_DIR}/test/rhi_harness.c
                ${PROJECT_SOURCE_DIR}/test/rhi_apart.c)
            target_link_libraries(test_${suite} PRIVATE maul-vfx-rhi)
            # Each compares a step at a time on lavapipe: the longest take
            # over 200 seconds on CI's runners, which vary by half again,
            # so they get 900 in place of a run's default.
            set_tests_properties(${suite} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
        endforeach()
        # A mesh's region and the element its thresholds choose, beside the
        # CPU path's sampler; no device needed.
        maul_add_test(rhi_mesh_image WHITEBOX)
        target_link_libraries(test_rhi_mesh_image PRIVATE maul-vfx-rhi)
        target_include_directories(test_rhi_mesh_image PRIVATE ${PROJECT_SOURCE_DIR}/rhi/src)
        maul_add_test(rhi_object WHITEBOX)
        target_link_libraries(test_rhi_object PRIVATE maul-vfx-rhi)
        target_include_directories(test_rhi_object PRIVATE ${PROJECT_SOURCE_DIR}/rhi/src)
        # The bytes a step uploads, on the test driver's frame log.
        maul_add_test(rhi_uploads)
        target_link_libraries(test_rhi_uploads PRIVATE maul-vfx-rhi)
        # Host code calling back into a simulation, refused.
        maul_add_test(rhi_host_calls)
        target_link_libraries(test_rhi_host_calls PRIVATE maul-vfx-rhi)
    endif()
    maul_add_header_tests()
    # The library keeps within WebAssembly's default 64 KiB stack: on the
    # web its tests run with that stack, every frame checked against it,
    # so a change that overflows it fails them.
    if(EMSCRIPTEN)
        get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            if(target MATCHES "^test_")
                target_link_options(${target} PRIVATE -sSTACK_SIZE=65536
                                    -sSTACK_OVERFLOW_CHECK=2)
            endif()
        endforeach()
    endif()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the bytes programs load and hand the library. test/corpus holds a
# minimized seed corpus for each (fuzz_<area>.tar.gz); test_golden --seeds
# <dir> writes the golden scene's effects as the targets' first seeds.
if(MAUL_VFX_FUZZ)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'. Undefined
    # behaviour stops the run, as a failed check does.
    target_compile_options(maul-vfx PRIVATE -fsanitize=fuzzer-no-link,address,undefined
                                            -fno-sanitize-recover=all)
    target_link_options(maul-vfx PUBLIC -fsanitize=address,undefined)
    foreach(area file step)
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-vfx)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined
                                                     -fno-sanitize-recover=all)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

if(MAUL_VFX_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_VFX_INSTALL)
    include(cmake/Install.cmake)
endif()

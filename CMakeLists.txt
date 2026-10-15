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
    src/curve.c
    src/curve_integral.c
    src/death_heap.c
    src/depth.c
    src/direction.c
    src/effect.c
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
    src/emitter_step.c
    src/event.c
    src/neighbor.c
    src/contact.c
    src/plexus.c
    src/unit.c
    src/steer.c
    src/event_spawn.c
    src/float_mode.c
    src/gradient.c
    src/hash.c
    src/host_module.c
    src/instance.c
    src/instance_api.c
    src/mesh.c
    src/mesh_api.c
    src/noise.c
    src/output.c
    src/over_life.c
    src/ribbon.c
    src/rigid.c
    src/rigid_contact.c
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
    maul_add_test(elementary WHITEBOX)
    maul_add_test(emitter_step WHITEBOX)
    maul_add_test(event WHITEBOX)
    maul_add_test(event_spawn WHITEBOX)
    maul_add_test(expand)
    maul_add_test(expand_ribbon)
    maul_add_test(elementary_sweep WHITEBOX MANUAL)
    maul_add_test(golden WHITEBOX)
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
    maul_add_test(steering)
    maul_add_test(trail)
    maul_add_test(tube)
    maul_add_test(value WHITEBOX)
    maul_add_test(velocity_modules WHITEBOX)
    maul_add_test(tasks WHITEBOX)
    # The web build has no threads; the twins need four.
    if(NOT EMSCRIPTEN)
        maul_add_test(twins WHITEBOX POSIX)
    endif()
    maul_add_header_tests()
endif()

if(MAUL_VFX_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_VFX_INSTALL)
    include(cmake/Install.cmake)
endif()

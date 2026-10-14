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
    src/appearance.c
    src/collide.c
    src/base.c
    src/birth.c
    src/context.c
    src/curve.c
    src/curve_integral.c
    src/death_heap.c
    src/direction.c
    src/effect.c
    src/effect_api.c
    src/elementary.c
    src/force.c
    src/grid.c
    src/grid_api.c
    src/integrate.c
    src/kill.c
    src/life.c
    src/emitter_step.c
    src/float_mode.c
    src/gradient.c
    src/hash.c
    src/host_module.c
    src/instance.c
    src/instance_api.c
    src/mesh.c
    src/mesh_api.c
    src/noise.c
    src/over_life.c
    src/shape.c
    src/slots.c
    src/spawn.c
    src/step.c
    src/tasks.c
    src/value.c
    src/velocity.c
    src/velocity_modules.c)
maul_configure_library(maul-vfx)
# sqrt, floor, ceil and the other exactly rounded functions the library
# takes from libm (mvfx-0001); the tests' oracles use more of it.
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(maul-vfx PUBLIC m)
endif()

if(MAUL_VFX_BUILD_TESTS)
    enable_testing()
    maul_add_test(allocator WHITEBOX)
    maul_add_test(base)
    maul_add_test(collide WHITEBOX)
    maul_add_test(collide_grid WHITEBOX)
    maul_add_test(context)
    maul_add_test(curve WHITEBOX)
    maul_add_test(curve_integral WHITEBOX)
    maul_add_test(effect)
    maul_add_test(effect_plan WHITEBOX)
    maul_add_test(force WHITEBOX)
    maul_add_test(elementary WHITEBOX)
    maul_add_test(emitter_step WHITEBOX)
    maul_add_test(elementary_sweep WHITEBOX MANUAL)
    maul_add_test(golden WHITEBOX)
    maul_add_test(gradient WHITEBOX)
    maul_add_test(grid WHITEBOX)
    maul_add_test(half WHITEBOX)
    maul_add_test(host WHITEBOX)
    maul_add_test(host_module WHITEBOX)
    maul_add_test(integrate WHITEBOX)
    maul_add_test(kill WHITEBOX)
    maul_add_test(instance)
    maul_add_test(mesh WHITEBOX)
    maul_add_test(noise WHITEBOX)
    maul_add_test(over_life WHITEBOX)
    maul_add_test(motion WHITEBOX)
    maul_add_test(half_sweep WHITEBOX MANUAL)
    maul_add_test(random WHITEBOX)
    maul_add_test(shape WHITEBOX)
    maul_add_test(slots WHITEBOX)
    maul_add_test(spawn WHITEBOX)
    maul_add_test(step)
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

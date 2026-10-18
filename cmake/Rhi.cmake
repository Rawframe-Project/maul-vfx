# The GPU part, maul-vfx-rhi: a static library beside maul-vfx, its
# kernels run with Maul RHI, found installed or fetched at its release tag
# as Maul UI's part does. The core never depends on it.

set(MAUL_VFX_RHI_VERSION 0.7.0)
find_package(maul-rhi ${MAUL_VFX_RHI_VERSION} QUIET)
if(NOT maul-rhi_FOUND)
    set(MAUL_RHI_BUILD_TESTS OFF)
    set(MAUL_RHI_BUILD_BENCH OFF)
    set(MAUL_RHI_BUILD_SAMPLES OFF)
    set(MAUL_RHI_INSTALL OFF)
    # The part's tests run on its test driver as well as on a device.
    set(MAUL_RHI_TEST_DRIVER ${MAUL_VFX_BUILD_TESTS})
    include(FetchContent)
    FetchContent_Declare(maul-rhi
        GIT_REPOSITORY https://github.com/Rawframe-Project/maul-rhi.git
        GIT_TAG v${MAUL_VFX_RHI_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(maul-rhi)
endif()

add_library(maul-vfx-rhi STATIC rhi/src/allocator.c rhi/src/grids.c rhi/src/kernels.c
    rhi/src/numbers.c rhi/src/record.c rhi/src/records.c rhi/src/sim.c
    rhi/src/sim_records.c rhi/src/sim_spawn.c)
add_library(maul-vfx-rhi::maul-vfx-rhi ALIAS maul-vfx-rhi)
target_include_directories(maul-vfx-rhi PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/rhi/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(maul-vfx-rhi PUBLIC maul-vfx maul-rhi::maul-rhi)
set_target_properties(maul-vfx-rhi PROPERTIES LINKER_LANGUAGE C)
maul_apply_flags(maul-vfx-rhi)

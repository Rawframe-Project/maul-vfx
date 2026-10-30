# The GPU part, maul-vfx-rhi: a static library beside maul-vfx, its
# kernels run with Maul RHI, found installed or fetched at its release tag
# as Maul UI's part does. The core never depends on it.

set(MAUL_VFX_RHI_VERSION 0.13.0)
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

add_library(maul-vfx-rhi STATIC rhi/src/allocator.c rhi/src/grid_numbers.c rhi/src/grids.c
    rhi/src/kernels.c rhi/src/mesh_image.c rhi/src/numbers.c rhi/src/record.c rhi/src/records.c
    rhi/src/sim.c rhi/src/sim_contacts.c rhi/src/sim_events.c rhi/src/sim_gas.c rhi/src/sim_grid.c
    rhi/src/sim_attach.c rhi/src/sim_follow.c rhi/src/sim_liquid.c rhi/src/sim_liquid_gas.c
    rhi/src/sim_frame.c rhi/src/sim_lanes.c rhi/src/sim_mesh.c
    rhi/src/sim_sizes.c rhi/src/sim_plexus.c rhi/src/sim_records.c rhi/src/sim_self.c
    rhi/src/sim_spawn.c rhi/src/sim_splat.c rhi/src/sim_surface_gas.c rhi/src/sim_tables.c
    rhi/src/sim_water.c rhi/src/sim_water_sample.c rhi/src/samples.c rhi/src/sim_gas_read.c
    rhi/src/marching_words.c rhi/src/sim_surface_mesh.c)
add_library(maul-vfx-rhi::maul-vfx-rhi ALIAS maul-vfx-rhi)
target_include_directories(maul-vfx-rhi PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/rhi/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# The kernel containers are committed as bytes, FOLDER/NAME.container
# (tools/gen_kernels.py), and the build writes each into the C array
# kernels/NAME_container.h under MVFX_KERNEL_HEADERS, which the sources
# include. The target TARGET writes a folder's arrays.
set(MVFX_KERNEL_HEADERS "${PROJECT_BINARY_DIR}/mvfx-kernels")
function(mvfx_kernel_containers target folder)
    file(GLOB containers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${folder}/*.container")
    set(headers "")
    foreach(container IN LISTS containers)
        get_filename_component(name "${container}" NAME_WE)
        set(header "${MVFX_KERNEL_HEADERS}/kernels/${name}_container.h")
        add_custom_command(OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" -DNAME=${name} -DFOLDER=${folder} "-DIN=${container}"
                    "-DOUT=${header}" -P "${PROJECT_SOURCE_DIR}/cmake/KernelContainer.cmake"
            DEPENDS "${container}" "${PROJECT_SOURCE_DIR}/cmake/KernelContainer.cmake"
            COMMENT "Writing kernels/${name}_container.h"
            VERBATIM)
        list(APPEND headers "${header}")
    endforeach()
    add_custom_target(${target} DEPENDS ${headers})
endfunction()
mvfx_kernel_containers(mvfx-rhi-kernels rhi/kernels)
add_dependencies(maul-vfx-rhi mvfx-rhi-kernels)
target_include_directories(maul-vfx-rhi PRIVATE "${MVFX_KERNEL_HEADERS}")
target_link_libraries(maul-vfx-rhi PUBLIC maul-vfx maul-rhi::maul-rhi)
set_target_properties(maul-vfx-rhi PROPERTIES LINKER_LANGUAGE C)
maul_apply_flags(maul-vfx-rhi)
mvfx_limit_frames(maul-vfx-rhi)

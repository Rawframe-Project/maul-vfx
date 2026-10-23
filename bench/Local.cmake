# Maul VFX's further benchmarks, kept here because bench/CMakeLists.txt
# is a family file: the speed and memory targets on one core, and the
# scaling over the host's threads where POSIX threads are.
add_executable(${PROJECT_NAME}_bench_particles bench_particles.c)
target_link_libraries(${PROJECT_NAME}_bench_particles PRIVATE ${PROJECT_NAME})
maul_apply_flags(${PROJECT_NAME}_bench_particles)

# The scaling over a host's threads, where POSIX threads are (not the web
# build, whose threads need flags of their own).
find_package(Threads)
if(CMAKE_USE_PTHREADS_INIT AND NOT EMSCRIPTEN)
    add_executable(${PROJECT_NAME}_bench_threads bench_threads.c)
    target_link_libraries(${PROJECT_NAME}_bench_threads PRIVATE ${PROJECT_NAME} Threads::Threads)
    maul_apply_flags(${PROJECT_NAME}_bench_threads)
endif()

# The GPU part's step, where the part is built (MAUL_VFX_RHI).
if(TARGET maul-vfx-rhi)
    add_executable(${PROJECT_NAME}_bench_gpu bench_gpu.c)
    target_link_libraries(${PROJECT_NAME}_bench_gpu PRIVATE maul-vfx-rhi)
    maul_apply_flags(${PROJECT_NAME}_bench_gpu)
endif()

# Maul VFX's further benchmarks (bench/Local.cmake, family record 0006):
# the speed and memory targets on one core, and the scaling over the
# host's threads where POSIX threads are.
add_executable(${PROJECT_NAME}_bench_particles bench_particles.c)
target_link_libraries(${PROJECT_NAME}_bench_particles PRIVATE ${PROJECT_NAME})
maul_apply_flags(${PROJECT_NAME}_bench_particles)

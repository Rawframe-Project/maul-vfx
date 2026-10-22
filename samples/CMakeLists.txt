# In-tree samples: small programs built with the library. The minimal/
# subdirectory is a separate project that consumes the installed
# package instead; CI builds it against a fresh install.

foreach(sample fountain trails fireworks flock cloth smoke liquid pond effect_file)
    add_executable(sample_${sample} ${sample}.c)
    target_link_libraries(sample_${sample} PRIVATE maul-vfx)
    maul_apply_flags(sample_${sample})
    # Each sample returns 0 when what it shows holds: run as a test, so a
    # sample that stops working fails the suite.
    if(MAUL_VFX_BUILD_TESTS)
        add_test(NAME sample_${sample} COMMAND sample_${sample})
    endif()
endforeach()

# The GPU part beside the CPU path, on a device on the first adapter;
# skipped (77) without one.
if(MAUL_VFX_RHI)
    add_executable(sample_gpu gpu.c)
    target_link_libraries(sample_gpu PRIVATE maul-vfx-rhi)
    maul_apply_flags(sample_gpu)
    if(MAUL_VFX_BUILD_TESTS)
        add_test(NAME sample_gpu COMMAND sample_gpu)
        set_tests_properties(sample_gpu PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()

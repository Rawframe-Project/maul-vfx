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
        set_tests_properties(sample_${sample} PROPERTIES LABELS sample)
    endif()
endforeach()

# The GPU part beside the CPU path, on a device on the first adapter;
# skipped (77) without one. The pull request check's GPU smoke test: the
# linux-rhi preset runs it on lavapipe, where its first frame compiles the
# kernels a fountain needs, minutes with an empty shader cache.
# LeakSanitizer is off for it: it cannot suppress lavapipe's own
# allocations, whose module is unloaded before the report.
if(MAUL_VFX_RHI)
    add_executable(sample_gpu gpu.c)
    target_link_libraries(sample_gpu PRIVATE maul-vfx-rhi)
    maul_apply_flags(sample_gpu)
    if(MAUL_VFX_BUILD_TESTS)
        add_test(NAME sample_gpu COMMAND sample_gpu)
        # hygiene: a device test skips without an adapter, unless a cell requires one
        set_tests_properties(sample_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200
                             FAIL_REGULAR_EXPRESSION "Validation Error|VUID-"
                             LABELS "sample;integration"
                             ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
        # A value of MVFX_RHI_REQUIRED that names no driver fails it before
        # any adapter search, so this needs no device: exit status 1 and the
        # refusal's message, both required.
        add_test(NAME sample_gpu_required_unknown
                 COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:sample_gpu>
                         "-DEXPECT=MVFX_RHI_REQUIRED is .vulcan., which names no driver"
                         -P ${PROJECT_SOURCE_DIR}/cmake/ExpectRefusal.cmake)
        set_tests_properties(sample_gpu_required_unknown PROPERTIES
                             ENVIRONMENT "MVFX_RHI_REQUIRED=vulcan" LABELS sample)
    endif()
endif()

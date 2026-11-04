# Runs PROGRAM and fails unless it exits with 1 and its output matches
# EXPECT: a refusal must end the run, not print and carry on, which a test
# passing on its output alone would take for success. It runs as a test's
# command, in the test's environment.
#
# usage: cmake -DPROGRAM=<path> -DEXPECT=<regex> -P ExpectRefusal.cmake

execute_process(COMMAND "${PROGRAM}" RESULT_VARIABLE result OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
message("${output}")
if(NOT result STREQUAL "1")
    message(FATAL_ERROR "${PROGRAM} exited with ${result}, not 1")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR "${PROGRAM} did not print: ${EXPECT}")
endif()

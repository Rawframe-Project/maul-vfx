# Runs PROGRAM and fails unless it exits with 1 and its whole output, but
# for the space about it, matches EXPECT: a refusal must end the run at
# once, not print and carry on. A run that went on might still exit with 1
# for a reason of its own (a leak report, a later failure), so the status
# alone does not show the refusal ended it. It runs as a test's command, in
# the test's environment.
#
# usage: cmake -DPROGRAM=<path> -DEXPECT=<regex> -P ExpectRefusal.cmake

execute_process(COMMAND "${PROGRAM}" RESULT_VARIABLE result OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
message("${output}")
string(STRIP "${output}" output)
if(NOT result STREQUAL "1")
    message(FATAL_ERROR "${PROGRAM} exited with ${result}, not 1")
endif()
if(NOT output MATCHES "^(${EXPECT})$")
    message(FATAL_ERROR "${PROGRAM} printed more or other than: ${EXPECT}")
endif()

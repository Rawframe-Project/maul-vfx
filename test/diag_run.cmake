# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# A diagnosis on WARP (to drop): runs EXE under cdb when the runner has
# it, symbols from the exe's PDB and Microsoft's symbol server, with full
# page heap for the exe's name when PAGEHEAP is set, turned off again
# after the run whatever it does.
#
# usage: cmake -DEXE=<path> [-DPAGEHEAP=ON] -P diag_run.cmake

get_filename_component(name "${EXE}" NAME)
get_filename_component(dir "${EXE}" DIRECTORY)
file(TO_NATIVE_PATH "${dir}" native)
set(ifeo "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\${name}")
if(PAGEHEAP)
    execute_process(COMMAND reg add "${ifeo}" /v GlobalFlag /t REG_DWORD /d 0x02000000 /f)
    execute_process(COMMAND reg add "${ifeo}" /v PageHeapFlags /t REG_DWORD /d 0x3 /f)
    message("full page heap on for ${name}")
endif()
set(ENV{_NT_SYMBOL_PATH}
    "${native};srv*${native}\\symbols*https://msdl.microsoft.com/download/symbols")
file(GLOB cdb "C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe")
if(cdb)
    message("under ${cdb}")
    # At the initial break: the exceptions to stop on, each printing its
    # stack, then run; a first-chance fault is passed on, so a second
    # chance is where the process would die.
    file(WRITE "${dir}/${name}.cdb"
        "sxe -c \".echo first chance; kn 60; gn\" -c2 \".echo second chance; .ecxr; kn 60; q\" av\n"
        "sxe -c \".echo heap corruption; kn 60; q\" c0000374\n"
        "sxe -c \".echo fail fast; kn 60; q\" c0000409\n"
        "g\n")
    file(WRITE "${dir}/${name}.in" "q\n")
    execute_process(COMMAND "${cdb}" -G -lines -cf "${dir}/${name}.cdb" "${EXE}"
                    INPUT_FILE "${dir}/${name}.in" RESULT_VARIABLE result TIMEOUT 1500)
else()
    message("no cdb on this runner: the exe alone")
    execute_process(COMMAND "${EXE}" RESULT_VARIABLE result TIMEOUT 1500)
endif()
if(PAGEHEAP)
    execute_process(COMMAND reg delete "${ifeo}" /f)
    message("full page heap off for ${name}")
endif()
# Always failed, so that ctest prints the whole output.
message(FATAL_ERROR "result: ${result}")

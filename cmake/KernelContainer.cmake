# Writes a kernel's container (FOLDER/NAME.container, as tools/gen_kernels.py
# cooks it) into OUT as the C array the GPU part compiles: s_<name in camel
# case>Container, aligned to 8. It runs as a script on the host's CMake at
# build time, so a cross build (Android, Emscripten) takes it as well.
#
# usage: cmake -DNAME=sim -DFOLDER=rhi/kernels -DIN=<container> -DOUT=<header>
#              -P KernelContainer.cmake

file(SIZE "${IN}" bytes)
if(bytes EQUAL 0)
    message(FATAL_ERROR "${IN} is empty")
endif()

set(symbol "")
string(REPLACE "_" ";" parts "${NAME}")
foreach(part IN LISTS parts)
    string(SUBSTRING "${part}" 0 1 first)
    string(SUBSTRING "${part}" 1 -1 rest)
    if(symbol STREQUAL "")
        string(TOLOWER "${first}" first)
    else()
        string(TOUPPER "${first}" first)
    endif()
    string(APPEND symbol "${first}${rest}")
endforeach()
string(TOUPPER "MAUL_VFX_RHI_${NAME}_CONTAINER_H" guard)

# The array is written a chunk at a time, sixteen bytes a row, each 0xhh
# and a comma, so the largest container takes little memory.
set(out "${OUT}.part")
file(WRITE "${out}" "// The ${NAME} kernel's container, written by cmake/KernelContainer.cmake
// from ${FOLDER}/${NAME}.container, which tools/gen_kernels.py cooks.

#ifndef ${guard}
#define ${guard}

#include <stdalign.h>
#include <stdint.h>

alignas(8) static const uint8_t s_${symbol}Container[${bytes}] = {
")
set(byte "[0-9a-f][0-9a-f]")
string(REPEAT "0x${byte}, " 16 row)
set(chunk 65536)
set(at 0)
while(at LESS bytes)
    file(READ "${IN}" hex OFFSET ${at} LIMIT ${chunk} HEX)
    string(REGEX REPLACE "(${byte})" "0x\\1, " hex "${hex}")
    string(REGEX REPLACE "(${row})" "\\1\n" hex "${hex}")
    string(REGEX REPLACE ", $" ",\n" hex "${hex}")
    string(REPLACE ", \n" ",\n" hex "${hex}")
    string(REPLACE "\n0x" "\n    0x" hex "    ${hex}")
    file(APPEND "${out}" "${hex}")
    math(EXPR at "${at} + ${chunk}")
endwhile()
file(APPEND "${out}" "};

#endif // ${guard}
")
file(RENAME "${out}" "${OUT}")

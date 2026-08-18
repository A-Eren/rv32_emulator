cmake_minimum_required(VERSION 3.20)
project(rv32_emulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RV32_WERROR "Treat all warnings as errors" OFF)
option(RV32_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompilerWarnings)

# ---------------------------------------------------------------------------
# rv_isa -- the instruction table, encoding and decoding. Depends on nothing.
# ---------------------------------------------------------------------------
add_library(rv_isa STATIC
    src/isa/isa.cpp
    src/isa/csr.cpp
    src/isa/decode.cpp
    src/isa/encode.cpp
    src/isa/disasm.cpp
    src/isa/regnames.cpp
)
target_include_directories(rv_isa PUBLIC src)
rv_set_warnings(rv_isa)

# ---------------------------------------------------------------------------
# rv_core -- the machine: registers, memory, MMIO, execution, traps.
# ---------------------------------------------------------------------------
add_library(rv_core STATIC
    src/core/csrfile.cpp
    src/core/memory.cpp
    src/core/devices.cpp
    src/core/bus.cpp
    src/core/exec.cpp
    src/core/hart.cpp
    src/core/memfile.cpp
    src/core/trap.cpp
)
target_link_libraries(rv_core PUBLIC rv_isa)
rv_set_warnings(rv_core)

# ---------------------------------------------------------------------------
# rv_asm -- lexer, parser, assembler. Depends on rv_core because it produces
# images for a specific machine: it needs the memory sizes to report "program
# too large", and it emits the same .mem format the runner consumes.
# ---------------------------------------------------------------------------
add_library(rv_asm STATIC
    src/asm/source.cpp
    src/asm/diagnostic.cpp
    src/asm/diag_render.cpp
    src/asm/directive.cpp
    src/asm/lexer.cpp
    src/asm/symtab.cpp
    src/asm/pseudo.cpp
    src/asm/parser.cpp
    src/asm/sourcemap.cpp
    src/asm/assembler.cpp
)
target_link_libraries(rv_asm PUBLIC rv_core)
rv_set_warnings(rv_asm)

# ---------------------------------------------------------------------------
# rv32 -- the front-end binary.
# ---------------------------------------------------------------------------
add_executable(rv32 src/main.cpp)
target_link_libraries(rv32 PRIVATE rv_asm)
rv_set_warnings(rv32)

# ---------------------------------------------------------------------------
# Tests. No external framework: tests/rv_test.hpp is the whole harness.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(rv_tests
    tests/test_main.cpp
    tests/unit_fields.cpp
    tests/unit_decode.cpp
    tests/unit_roundtrip.cpp
    tests/unit_exec_alu.cpp
    tests/unit_exec_mem.cpp
    tests/unit_exec_branch.cpp
    tests/unit_exec_m.cpp
    tests/unit_csr_trap.cpp
    tests/unit_memfile.cpp
    tests/unit_lexer.cpp
    tests/unit_diag_render.cpp
    tests/unit_assembler.cpp
)
target_link_libraries(rv_tests PRIVATE rv_asm)
target_include_directories(rv_tests PRIVATE tests)
rv_set_warnings(rv_tests)

# One CTest entry per suite, so a failure can be re-run in isolation.
foreach(suite fields decode roundtrip exec_alu exec_mem exec_branch exec_m csr_trap memfile
              lexer diag_render assembler)
    add_test(NAME unit.${suite} COMMAND rv_tests --filter=${suite})
endforeach()

# Self-checking assembly programs: one CTest each, so a failure names the file.
# Globbed at configure time, so re-run cmake after adding one.
file(GLOB RV32_ASM_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/asm/*.s)
foreach(asm_test ${RV32_ASM_TESTS})
    get_filename_component(asm_name ${asm_test} NAME_WE)
    add_test(NAME asm.${asm_name} COMMAND rv32 test ${asm_test})
endforeach()

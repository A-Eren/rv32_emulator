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
# rv32 -- the front-end binary.
# ---------------------------------------------------------------------------
add_executable(rv32 src/main.cpp)
target_link_libraries(rv32 PRIVATE rv_core)
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
)
target_link_libraries(rv_tests PRIVATE rv_core)
target_include_directories(rv_tests PRIVATE tests)
rv_set_warnings(rv_tests)

# One CTest entry per suite, so a failure can be re-run in isolation.
foreach(suite fields decode roundtrip exec_alu exec_mem exec_branch exec_m csr_trap memfile)
    add_test(NAME unit.${suite} COMMAND rv_tests --filter=${suite})
endforeach()

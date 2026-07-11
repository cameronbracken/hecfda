cmake_minimum_required(VERSION 3.16)
project(hecfdapy CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(_core src/bindings/glue.cpp)
target_include_directories(_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hecfda_core/include)
# -ffp-contract=off: matches core/CMakeLists.txt. Clang/GCC default to fusing
# multiply-add expressions (FP_CONTRACT ON even at -O0), which rounds
# differently than .NET's non-fused arithmetic. Required for bit-for-bit
# cross-language reproduction on any FMA-sensitive path (e.g. the graphical
# epsilon finite-difference in GraphicalFrequencyUncertaintyCalculators).
# MSVC defaults to no contraction, so this is guarded to non-MSVC compilers.
target_compile_options(_core PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>)
install(TARGETS _core DESTINATION hecfdapy)

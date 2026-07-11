cmake_minimum_required(VERSION 3.16)
project(hecfda_core CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_library(hecfda_core INTERFACE)
target_include_directories(hecfda_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# every test is a doctest translation unit; DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
# lives in test_smoke.cpp only, others use a shared main via object below.
file(GLOB HECFDA_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(test_src ${HECFDA_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE hecfda_core)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(NOT MSVC)
        # -ffp-contract=off: Clang defaults to fusing multiply-add expressions (FP_CONTRACT ON,
        # even at -O0), which rounds differently than .NET's non-fused arithmetic. Found via Task
        # P2T4b's seeded GraphicalUncertainPairedData fixture: GraphicalFrequencyUncertaintyCalcul
        # ators::compute_slope's epsilon (1e-5) finite-difference division amplifies a single ULP
        # of FMA-vs-non-fused divergence in interpolate_normally() by ~1e5x, breaking bit-for-bit
        # reproduction at scattered indices for some (but not all) inputs. Disabling contraction
        # project-wide matches .NET's strict left-to-right IEEE 754 evaluation and is required for
        # every other fixture's "bit-for-bit" claim to actually hold, not just this one.
        target_compile_options(${test_name} PRIVATE -Wall -Wextra -ffp-contract=off)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

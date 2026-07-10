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
        target_compile_options(${test_name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

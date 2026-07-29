cmake_minimum_required(VERSION 3.14)
project(p101_fsm_tests C CXX)
enable_testing()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(P101_TEST_COVERAGE "Instrument the code under test for coverage" OFF)
set(P101_TEST_COVERAGE_FLAGS "")
if(P101_TEST_COVERAGE)
    set(P101_TEST_COVERAGE_FLAGS --coverage)
endif()

set(P101_LIBS p101_error p101_env p101_c p101_posix)
set(P101_PUBLIC_INCLUDE_DIRS "" CACHE STRING "Extra p101 include dirs")
set(P101_PUBLIC_LINK_DIRS "" CACHE STRING "Extra p101 link dirs")
separate_arguments(P101_PUBLIC_INCLUDE_DIRS_LIST NATIVE_COMMAND "${P101_PUBLIC_INCLUDE_DIRS}")
separate_arguments(P101_PUBLIC_LINK_DIRS_LIST NATIVE_COMMAND "${P101_PUBLIC_LINK_DIRS}")
set(_P101_INC_DIRS ${P101_PUBLIC_INCLUDE_DIRS_LIST} /usr/local/include /opt/homebrew/include /opt/local/include)
set(_P101_LIB_DIRS ${P101_PUBLIC_LINK_DIRS_LIST} /usr/local/lib /usr/local/lib64 /opt/homebrew/lib /opt/local/lib)

set(_P101_RESOLVED "")
foreach(_library IN LISTS P101_LIBS)
    unset(_P101_LIB_${_library} CACHE)
    unset(_P101_LIB_${_library})
    find_library(_P101_LIB_${_library} NAMES ${_library} PATHS ${_P101_LIB_DIRS} NO_DEFAULT_PATH)
    if(NOT _P101_LIB_${_library})
        find_library(_P101_LIB_${_library} NAMES ${_library})
    endif()
    if(NOT _P101_LIB_${_library})
        message(FATAL_ERROR "Required p101 library '${_library}' was not found")
    endif()
    list(APPEND _P101_RESOLVED "${_P101_LIB_${_library}}")
endforeach()

add_library(p101_fsm_under_test STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/fsm.c"
)
target_include_directories(p101_fsm_under_test PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        ${_P101_INC_DIRS}
)
target_compile_options(p101_fsm_under_test PRIVATE ${P101_TEST_COVERAGE_FLAGS})
target_link_libraries(p101_fsm_under_test PUBLIC ${_P101_RESOLVED})

add_executable(test_fsm test_fsm.c)
target_link_libraries(test_fsm PRIVATE p101_fsm_under_test)
target_compile_options(test_fsm PRIVATE ${P101_TEST_COVERAGE_FLAGS})
target_link_options(test_fsm PRIVATE ${P101_TEST_COVERAGE_FLAGS})
add_test(NAME test_fsm COMMAND test_fsm)

add_executable(test_cpp_linkage test_cpp_linkage.cpp)
target_link_libraries(test_cpp_linkage PRIVATE p101_fsm_under_test)
target_compile_options(test_cpp_linkage PRIVATE ${P101_TEST_COVERAGE_FLAGS})
target_link_options(test_cpp_linkage PRIVATE ${P101_TEST_COVERAGE_FLAGS})
add_test(NAME test_cpp_linkage COMMAND test_cpp_linkage)

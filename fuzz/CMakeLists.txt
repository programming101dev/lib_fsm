cmake_minimum_required(VERSION 3.14)
project(p101_fsm_fuzz C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Fuzzing needs clang with libFuzzer")
endif()

set(P101_LIBS p101_error p101_env p101_tool_event p101_c p101_text)
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

set(_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -g -O1)

add_executable(fuzz
        fuzz_fsm.c
        "${CMAKE_CURRENT_SOURCE_DIR}/../src/fsm.c"
)
target_include_directories(fuzz PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        ${_P101_INC_DIRS}
)
target_compile_options(fuzz PRIVATE ${_FUZZ_FLAGS})
target_link_options(fuzz PRIVATE ${_FUZZ_FLAGS})
target_link_libraries(fuzz PRIVATE ${_P101_RESOLVED})

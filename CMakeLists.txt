cmake_minimum_required(VERSION 3.20)

project(MetaphorAudioFix LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "Configure this project with a Windows target toolchain, for example via mingw-w64.")
endif()

execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libwinpthread.a
    OUTPUT_VARIABLE WINPTHREAD_STATIC_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libwinpthread-1.dll
    OUTPUT_VARIABLE WINPTHREAD_DLL_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
    OUTPUT_VARIABLE MINGW_TRIPLE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-sysroot
    OUTPUT_VARIABLE MINGW_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

file(REAL_PATH "${WINPTHREAD_STATIC_PATH}" WINPTHREAD_STATIC_PATH)
get_filename_component(WINPTHREAD_LIB_DIR "${WINPTHREAD_STATIC_PATH}" DIRECTORY)
get_filename_component(WINPTHREAD_ROOT_DIR "${WINPTHREAD_LIB_DIR}" DIRECTORY)
get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

if (IS_ABSOLUTE "${WINPTHREAD_DLL_PATH}" AND EXISTS "${WINPTHREAD_DLL_PATH}")
    file(REAL_PATH "${WINPTHREAD_DLL_PATH}" WINPTHREAD_DLL_PATH)
else()
    unset(WINPTHREAD_DLL_PATH)
    unset(WINPTHREAD_DLL_PATH CACHE)
    find_file(WINPTHREAD_DLL_PATH
        NAMES libwinpthread-1.dll
        PATHS
            "${WINPTHREAD_LIB_DIR}"
            "${WINPTHREAD_ROOT_DIR}"
            "${WINPTHREAD_ROOT_DIR}/bin"
            "${WINPTHREAD_ROOT_DIR}/lib"
            "${COMPILER_DIR}"
            "${COMPILER_DIR}/../bin"
            "${COMPILER_DIR}/../lib"
            "${MINGW_SYSROOT}/bin"
            "${MINGW_SYSROOT}/lib"
            "/usr/${MINGW_TRIPLE}/bin"
            "/usr/${MINGW_TRIPLE}/lib"
            "/usr/${MINGW_TRIPLE}/sys-root/mingw/bin"
    )

    if (NOT WINPTHREAD_DLL_PATH AND EXISTS "/usr/lib/gcc/${MINGW_TRIPLE}")
        file(GLOB_RECURSE WINPTHREAD_DLL_CANDIDATES "/usr/lib/gcc/${MINGW_TRIPLE}/*/libwinpthread-1.dll")
        list(LENGTH WINPTHREAD_DLL_CANDIDATES WINPTHREAD_DLL_COUNT)
        if (WINPTHREAD_DLL_COUNT GREATER 0)
            list(GET WINPTHREAD_DLL_CANDIDATES 0 WINPTHREAD_DLL_PATH)
        endif()
    endif()
endif()

if (NOT EXISTS "${WINPTHREAD_DLL_PATH}")
    message(FATAL_ERROR "Could not locate libwinpthread-1.dll for packaging")
endif()

set(ASI_LOADER_DLL_PATH "${CMAKE_SOURCE_DIR}/third_party/Ultimate-ASI-Loader/winmm.dll")
set(ASI_LOADER_LICENSE_PATH "${CMAKE_SOURCE_DIR}/third_party/Ultimate-ASI-Loader/LICENSE.txt")

if (NOT EXISTS "${ASI_LOADER_DLL_PATH}")
    message(FATAL_ERROR "Could not locate bundled ASI loader: ${ASI_LOADER_DLL_PATH}")
endif()

if (NOT EXISTS "${ASI_LOADER_LICENSE_PATH}")
    message(FATAL_ERROR "Could not locate bundled ASI loader license: ${ASI_LOADER_LICENSE_PATH}")
endif()

add_library(MetaphorAudioFix SHARED
    src/config.cpp
    src/dllmain.cpp
    src/log.cpp
    external-minhook/src/buffer.c
    external-minhook/src/hde/hde64.c
    external-minhook/src/hook.c
    external-minhook/src/trampoline.c
)

target_include_directories(MetaphorAudioFix PRIVATE
    src
    external-minhook/include
    external-minhook/src
    external-minhook/src/hde
)

target_compile_definitions(MetaphorAudioFix PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_link_libraries(MetaphorAudioFix PRIVATE
    ole32
    uuid
    ${WINPTHREAD_STATIC_PATH}
)

target_link_options(MetaphorAudioFix PRIVATE
    -static-libgcc
    -static-libstdc++
    -Wl,--exclude-all-symbols
)

set_target_properties(MetaphorAudioFix PROPERTIES
    PREFIX ""
    OUTPUT_NAME "MetaphorAudioFix"
    SUFFIX ".asi"
)

add_executable(smoke_xaudio2 tests/smoke_xaudio2.cpp)

target_compile_definitions(smoke_xaudio2 PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_link_options(smoke_xaudio2 PRIVATE
    -static-libgcc
    -static-libstdc++
)

set_target_properties(smoke_xaudio2 PROPERTIES
    OUTPUT_NAME "smoke_xaudio2"
    SUFFIX ".exe"
)

add_custom_command(TARGET MetaphorAudioFix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:MetaphorAudioFix>
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:MetaphorAudioFix> ${CMAKE_BINARY_DIR}/package/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/MetaphorAudioFix.ini ${CMAKE_BINARY_DIR}/package/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${WINPTHREAD_DLL_PATH} ${CMAKE_BINARY_DIR}/package/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASI_LOADER_DLL_PATH} ${CMAKE_BINARY_DIR}/package/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASI_LOADER_LICENSE_PATH} ${CMAKE_BINARY_DIR}/package/Ultimate-ASI-Loader-LICENSE.txt
)

add_custom_command(TARGET smoke_xaudio2 POST_BUILD
    COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:smoke_xaudio2>
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:smoke_xaudio2> ${CMAKE_BINARY_DIR}/package/
)

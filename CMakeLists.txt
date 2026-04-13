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

file(REAL_PATH "${WINPTHREAD_STATIC_PATH}" WINPTHREAD_STATIC_PATH)
get_filename_component(WINPTHREAD_LIB_DIR "${WINPTHREAD_STATIC_PATH}" DIRECTORY)
get_filename_component(WINPTHREAD_ROOT_DIR "${WINPTHREAD_LIB_DIR}" DIRECTORY)
set(WINPTHREAD_DLL_PATH "${WINPTHREAD_ROOT_DIR}/bin/libwinpthread-1.dll")

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
)

add_custom_command(TARGET smoke_xaudio2 POST_BUILD
    COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:smoke_xaudio2>
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:smoke_xaudio2> ${CMAKE_BINARY_DIR}/package/
)

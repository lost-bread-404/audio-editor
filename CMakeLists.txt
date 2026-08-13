# ---- 1. 项目声明 ----------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(sound_seg
        VERSION 0.1.0            # 语义化版本，会传给库文件名和 pkg-config
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 让编辑器/静态分析工具知道每个文件是怎么编译的
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 2. 可切换的构建变体 ---------------------------------------------------
# 命令行用 -DSOUND_SEG_ASAN=ON 打开，不用改任何文件
option(SOUND_SEG_ASAN     "Build with AddressSanitizer"        OFF)
option(SOUND_SEG_BUILD_TESTS "Build unit tests"                ON)
option(SOUND_SEG_FUZZ     "Build libFuzzer targets (clang only)" OFF)
option(SOUND_SEG_BENCH    "Build benchmarks"                     OFF)

# ---- 3. 库目标 -------------------------------------------------------------
add_library(sound_seg
    src/sound_seg.c
    src/sound_seg_extra.c
)

# 给库起一个带命名空间的别名，用户写 sound_seg::sound_seg
add_library(sound_seg::sound_seg ALIAS sound_seg)

# PUBLIC  = 编译这个库时要用，*并且* 用户编译时也自动继承
# PRIVATE = 只有编译这个库时用，用户看不到
target_include_directories(sound_seg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Warning flags are compiler-specific. MSVC's driver rejects anything it
# does not recognise outright (error D8021), so the two families cannot
# share one list. Define the right list once and reuse it everywhere.
if(MSVC)
    set(SS_WARNING_FLAGS /W4)
else()
    set(SS_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

# Sanitizers, coverage and libFuzzer all use GCC/Clang-only syntax.
# Guard on this rather than on the OS: clang-cl on Windows takes GNU
# flags, and MSVC is not the only thing that runs on Windows.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(SS_GNU_STYLE_FLAGS TRUE)
else()
    set(SS_GNU_STYLE_FLAGS FALSE)
endif()

target_compile_options(sound_seg PRIVATE ${SS_WARNING_FLAGS})

set_target_properties(sound_seg PROPERTIES
    VERSION   ${PROJECT_VERSION}     # libsound_seg.so.0.1.0
    SOVERSION ${PROJECT_VERSION_MAJOR}  # libsound_seg.so.0
    POSITION_INDEPENDENT_CODE ON     # 就是你手写的 -fPIC
)

if(SOUND_SEG_ASAN)
    if(NOT SS_GNU_STYLE_FLAGS)
        message(FATAL_ERROR
                "SOUND_SEG_ASAN requires GCC or Clang, not ${CMAKE_C_COMPILER_ID}")
    endif()
    target_compile_options(sound_seg PUBLIC -fsanitize=address,undefined -g -O1)
    target_link_options(sound_seg    PUBLIC -fsanitize=address,undefined)
endif()

# The library itself must carry coverage instrumentation, or libFuzzer
# sees no feedback from it and degenerates into random input testing.
# 'fuzzer-no-link' adds the instrumentation without linking libFuzzer's
# main(); only the fuzz target links that.
if(SOUND_SEG_FUZZ)
    if(NOT SS_GNU_STYLE_FLAGS)
        message(FATAL_ERROR "SOUND_SEG_FUZZ requires Clang")
    endif()
    target_compile_options(sound_seg PUBLIC
        -fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer -g -O1)
    target_link_options(sound_seg PUBLIC -fsanitize=address)
endif()

# ---- 4. 原来 makefile 里那个打印 sizeof 的可执行文件 -----------------------
add_executable(sound_seg_cli src/sound_seg.c src/sound_seg_extra.c)
target_compile_definitions(sound_seg_cli PRIVATE SOUND_SEG_STANDALONE)
target_include_directories(sound_seg_cli PRIVATE include src)

# ---- 5. 测试 ---------------------------------------------------------------
if(SOUND_SEG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SOUND_SEG_FUZZ)
    add_subdirectory(fuzz)
endif()

if(SOUND_SEG_BENCH)
    add_subdirectory(benchmarks)
endif()

# ---- 6. 安装：让别人能 find_package(sound_seg) ------------------------------
include(GNUInstallDirs)
install(TARGETS sound_seg
        EXPORT  sound_segTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
set(SS_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/sound_seg)

# The targets file: what libraries exist and how to link them.
install(EXPORT sound_segTargets
        FILE      sound_segTargets.cmake
        NAMESPACE sound_seg::
        DESTINATION ${SS_CMAKE_DIR})

include(CMakePackageConfigHelpers)

# The config file: what find_package(sound_seg) actually loads. Kept
# separate from the targets file so it can resolve dependencies first.
configure_package_config_file(
    cmake/sound_segConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sound_segConfig.cmake
    INSTALL_DESTINATION ${SS_CMAKE_DIR})

# The version file: lets users write find_package(sound_seg 0.1 REQUIRED).
# SameMajorVersion encodes the semver promise -- 0.2 satisfies a request
# for 0.1, but 1.0 does not.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/sound_segConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/sound_segConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/sound_segConfigVersion.cmake
        DESTINATION ${SS_CMAKE_DIR})
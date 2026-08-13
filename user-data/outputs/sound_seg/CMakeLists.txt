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

target_compile_options(sound_seg PRIVATE
    -Wall -Wextra -Wpedantic
)

set_target_properties(sound_seg PROPERTIES
    VERSION   ${PROJECT_VERSION}     # libsound_seg.so.0.1.0
    SOVERSION ${PROJECT_VERSION_MAJOR}  # libsound_seg.so.0
    POSITION_INDEPENDENT_CODE ON     # 就是你手写的 -fPIC
)

if(SOUND_SEG_ASAN)
    target_compile_options(sound_seg PUBLIC -fsanitize=address,undefined -g -O1)
    target_link_options(sound_seg    PUBLIC -fsanitize=address,undefined)
endif()

if(SOUND_SEG_FUZZ)
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

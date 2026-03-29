cmake_minimum_required(VERSION 3.16)

# 定义项目名称和支持的语言
project(MyUniversalPlayer VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准为 C++17，支持更现代的多线程和智能指针特性
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==========================================
# 1. 查找依赖库：FFmpeg
# ==========================================
# 在 Linux 环境下，强烈建议使用 pkg-config 查找 FFmpeg
find_package(PkgConfig REQUIRED)

# 查找我们需要的 FFmpeg 组件，并生成 IMPORTED_TARGET
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale    # 用于视频像素格式转换
    libswresample # 用于音频重采样
)

# ==========================================
# 2. 查找依赖库：SDL3
# ==========================================
# 查找 SDL3。如果你是自己编译安装的 SDL3，CMake 通常能自动在 /usr/local 找到它
find_package(SDL3 REQUIRED)

# ==========================================
# 3. 定义源文件
# ==========================================
# 注意：thread_queue.h 通常是模板类，只需在头文件中实现，不需要对应的 .cpp
set(SOURCES
    src/main.cpp
    src/media_state.cpp
    src/demuxer.cpp
    src/decoder.cpp
    src/video_decoder.cpp
    src/audio_decoder.cpp
    src/renderer.cpp
    src/audio_output.cpp
    src/syncmanager.cpp
    src/player.cpp
)

# ==========================================
# 4. 创建可执行文件并链接
# ==========================================
add_executable(${PROJECT_NAME} ${SOURCES})

# 指定头文件搜索路径，这样在代码里可以直接 #include "media_state.h"
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 链接所有的库
target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::FFMPEG  # 链接 FFmpeg
    SDL3::SDL3         # 链接 SDL3
    pthread            # Linux 下使用 std::thread 需要显式链接 pthread
    m                  # 链接数学库
)

# （可选）开启更多的编译警告，帮助提前发现潜在的 Bug
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

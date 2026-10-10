cmake_minimum_required(VERSION 3.20)

# 定义项目名称和支持的语言（启用 C 和 CXX 以支持 glad.c）
project(MyUniversalPlayer VERSION 1.0 LANGUAGES C CXX)

# 设置 C++ 标准为 C++17，支持更现代的多线程和智能指针特性
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 全局编码配置：统一源码字符集与执行字符集为 UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ==========================================
# 1. 查找依赖库：FFmpeg (本地 SDK: G:/SDKs/ffmpeg)
# ==========================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(FFMPEG REQUIRED)

# ==========================================
# 2. 查找依赖库：SDL3 (本地 Windows MSVC SDK: G:/SDKs/SDL3)
# ==========================================
set(SDL3_DIR "G:/SDKs/SDL3/cmake" CACHE PATH "Path to SDL3 CMake config" FORCE)
find_package(SDL3 REQUIRED CONFIG)

# ==========================================
# 3. 构建 Glad 静态库
# ==========================================
add_library(glad STATIC third_party/glad/src/glad.c "src/paser/h264_paser.h" )
target_include_directories(glad PUBLIC third_party/glad/include)

# ==========================================
# 4. 定义源文件
# ==========================================
set(SOURCES
    src/main.cpp
    src/media_state.cpp
    src/demuxer.cpp
    src/decoder.cpp
    src/video_decoder.cpp
    src/audio_decoder.cpp
    src/audio_filter.cpp
    src/renderer.cpp
    src/render/modern_render.cpp
    src/audio_output.cpp
    src/syncmanager.cpp
    src/player.cpp
)

# 全局添加宏定义
add_definitions(-D__STDC_CONSTANT_MACROS)

# ==========================================
# 5. 创建可执行文件并链接
# ==========================================
add_executable(${PROJECT_NAME} ${SOURCES} "src/paser/h264_paser.h" )

# 指定头文件搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
    ${FFMPEG_INCLUDE_DIRS}
)

# 链接所有的库
target_link_libraries(${PROJECT_NAME} PRIVATE
    glad
    SDL3::SDL3
    ${FFMPEG_LIBRARIES}
    opengl32
)

# 自动将运行时动态链接库拷贝到可执行文件所在目录，避免运行时缺 DLL
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "G:/SDKs/SDL3/lib/x64/SDL3.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)

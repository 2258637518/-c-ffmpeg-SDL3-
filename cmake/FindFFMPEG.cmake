# Custom FindFFMPEG.cmake for loading pre-built FFmpeg SDK from G:/SDKs/ffmpeg
if(NOT FFMPEG_DIR OR FFMPEG_DIR STREQUAL "FFMPEG_DIR-NOTFOUND")
    set(FFMPEG_DIR "G:/SDKs/ffmpeg" CACHE PATH "Path to FFmpeg SDK" FORCE)
endif()

set(FFMPEG_FOUND TRUE)
set(FFMPEG_INCLUDE_DIRS "${FFMPEG_DIR}/include")
set(FFMPEG_LIBRARY_DIRS "${FFMPEG_DIR}/lib")

# List of libraries matching the project's requirements
set(FFMPEG_LIBRARIES
    "${FFMPEG_DIR}/lib/avcodec.lib"
    "${FFMPEG_DIR}/lib/avformat.lib"
    "${FFMPEG_DIR}/lib/swscale.lib"
    "${FFMPEG_DIR}/lib/avutil.lib"
    "${FFMPEG_DIR}/lib/swresample.lib"
    "${FFMPEG_DIR}/lib/avdevice.lib"
    "${FFMPEG_DIR}/lib/avfilter.lib"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS)

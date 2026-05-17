cmake_minimum_required(VERSION 3.24)
project(rtx_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies ----------------------------------------------------------
find_package(Vulkan REQUIRED)            # ships in LunarG SDK
find_package(JNI REQUIRED)               # Java 21+ headers

# Streamline SDK is fetched manually — point STREAMLINE_DIR at the unzipped
# release (https://github.com/NVIDIA-RTX/Streamline).
if(NOT DEFINED STREAMLINE_DIR)
    set(STREAMLINE_DIR "${CMAKE_SOURCE_DIR}/../streamline" CACHE PATH "Streamline SDK root")
endif()
if(NOT EXISTS "${STREAMLINE_DIR}/include/sl.h")
    message(WARNING "Streamline SDK not found at ${STREAMLINE_DIR}. "
                    "Download from https://github.com/NVIDIA-RTX/Streamline and unzip there, "
                    "or pass -DSTREAMLINE_DIR=<path>. Build will continue with DLSS stubbed.")
    set(STREAMLINE_FOUND FALSE)
else()
    set(STREAMLINE_FOUND TRUE)
endif()

# ---- Library ---------------------------------------------------------------
add_library(rtx_renderer SHARED
    src/jni_bridge.cpp
    src/renderer.cpp
    src/vulkan_context.cpp
    src/path_tracer.cpp
    src/voxel_bvh.cpp
    src/streamline_integration.cpp
)

target_include_directories(rtx_renderer PRIVATE
    include
    ${JNI_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(rtx_renderer PRIVATE Vulkan::Vulkan)

if(STREAMLINE_FOUND)
    target_compile_definitions(rtx_renderer PRIVATE RTXMC_HAVE_STREAMLINE=1)
    target_include_directories(rtx_renderer PRIVATE "${STREAMLINE_DIR}/include")
    target_link_libraries(rtx_renderer PRIVATE
        "${STREAMLINE_DIR}/lib/x64/sl.interposer.lib"
    )
endif()

if(WIN32)
    target_compile_definitions(rtx_renderer PRIVATE VK_USE_PLATFORM_WIN32_KHR=1 NOMINMAX=1)
    target_link_libraries(rtx_renderer PRIVATE user32 gdi32)
endif()

set_target_properties(rtx_renderer PROPERTIES
    PREFIX ""                            # match System.loadLibrary("rtx_renderer")
    OUTPUT_NAME "rtx_renderer"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
)

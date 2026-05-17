cmake_minimum_required(VERSION 3.24)
project(rtx_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies ----------------------------------------------------------
find_package(Vulkan REQUIRED)            # ships in LunarG SDK
find_package(JNI REQUIRED)               # Java 21+ headers

# Locate glslangValidator from the Vulkan SDK. Modern cmake's FindVulkan
# exposes Vulkan::glslangValidator as an imported target, but some installs
# don't ship the target alias — fall back to find_program.
if(TARGET Vulkan::glslangValidator)
    set(GLSLANG_VALIDATOR_CMD $<TARGET_FILE:Vulkan::glslangValidator>)
else()
    find_program(GLSLANG_VALIDATOR
        NAMES glslangValidator
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        REQUIRED)
    set(GLSLANG_VALIDATOR_CMD ${GLSLANG_VALIDATOR})
endif()

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

# ---- Shader compilation ----------------------------------------------------
# Compiles a GLSL source to SPIR-V, then embeds the bytes as a C uint32_t
# array via glslangValidator's --vn flag. Sets ${OUT_HEADER_VAR} in the
# parent scope to the generated header path so the caller can attach it as
# a source dependency.
set(SHADER_GEN_DIR "${CMAKE_BINARY_DIR}/generated_shaders")
file(MAKE_DIRECTORY "${SHADER_GEN_DIR}")

function(add_spirv_shader SHADER_SRC OUT_HEADER_VAR)
    get_filename_component(name "${SHADER_SRC}" NAME_WE)
    get_filename_component(ext  "${SHADER_SRC}" EXT)
    string(REPLACE "." "_" ext_clean "${ext}")
    set(out_header "${SHADER_GEN_DIR}/${name}${ext_clean}.h")
    set(var_name   "spirv_${name}${ext_clean}")
    add_custom_command(
        OUTPUT  "${out_header}"
        COMMAND ${GLSLANG_VALIDATOR_CMD}
                -V                                 # SPIR-V for Vulkan
                --target-env vulkan1.3
                --vn ${var_name}
                -o "${out_header}"
                "${SHADER_SRC}"
        DEPENDS "${SHADER_SRC}"
        VERBATIM
        COMMENT "GLSL -> SPIR-V header: ${name}${ext}"
    )
    set(${OUT_HEADER_VAR} "${out_header}" PARENT_SCOPE)
endfunction()

set(SHADER_DIR "${CMAKE_SOURCE_DIR}/../shaders")
add_spirv_shader("${SHADER_DIR}/triangle.vert" TRIANGLE_VERT_H)
add_spirv_shader("${SHADER_DIR}/triangle.frag" TRIANGLE_FRAG_H)
add_spirv_shader("${SHADER_DIR}/chunk.vert"    CHUNK_VERT_H)
add_spirv_shader("${SHADER_DIR}/chunk.frag"    CHUNK_FRAG_H)

# ---- Library ---------------------------------------------------------------
add_library(rtx_renderer SHARED
    src/jni_bridge.cpp
    src/renderer.cpp
    src/vulkan_context.cpp
    src/path_tracer.cpp
    src/voxel_bvh.cpp
    src/streamline_integration.cpp
    src/triangle.cpp
    src/chunk_renderer.cpp
    # Generated headers — listed as sources so cmake creates the dep edge.
    ${TRIANGLE_VERT_H}
    ${TRIANGLE_FRAG_H}
    ${CHUNK_VERT_H}
    ${CHUNK_FRAG_H}
)

target_include_directories(rtx_renderer PRIVATE
    include
    ${SHADER_GEN_DIR}
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

cmake_minimum_required(VERSION 3.20)
project(GameEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PASTEL_DEV_BUILD "Enable developer UI and GPU profiling tools" ON)

find_package(Vulkan REQUIRED)

# ---- GLFW + GLM: FetchContent (vcpkg 불필요) ----
include(FetchContent)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

if(PASTEL_DEV_BUILD)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.89.9
        GIT_SHALLOW    TRUE
    )
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()
endif()

add_executable(engine
    src/main.cpp
    src/game/GameState.cpp
    src/platform/Window.cpp
    src/renderer/VulkanContext.cpp
    src/renderer/VulkanContext_Init.cpp
    src/renderer/VulkanContext_Frame.cpp
    src/renderer/VulkanContext_Chunk.cpp
    src/renderer/VulkanContext_Ocean.cpp
    src/world/World.cpp
    src/world/TerrainGen.cpp
 "src/game/Camera.h" "src/platform/InputManager.h" "src/platform/InputManager.cpp")

target_include_directories(engine PRIVATE src third_party/smaa third_party/stb ${Vulkan_INCLUDE_DIRS})
target_link_libraries(engine PRIVATE Vulkan::Vulkan glfw glm::glm)
target_compile_definitions(engine PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)

if(PASTEL_DEV_BUILD)
    target_sources(engine PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(engine PRIVATE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(engine PRIVATE PASTEL_DEV_BUILD=1)
endif()

# VS 디버거 작업 디렉토리를 exe 위치로 고정 (셰이더 경로 문제 해결)
set_target_properties(engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:engine>"
)

# ---- Shader compilation ----
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK.")
endif()

set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR $<TARGET_FILE_DIR:engine>/shaders)
set(ASSET_SRC_DIR ${CMAKE_SOURCE_DIR}/assets)
set(ASSET_OUT_DIR $<TARGET_FILE_DIR:engine>/assets)

set(SHADERS
    ${SHADER_SRC_DIR}/triangle.vert
    ${SHADER_SRC_DIR}/triangle.frag
    ${SHADER_SRC_DIR}/chunk.vert
    ${SHADER_SRC_DIR}/chunk.frag
    ${SHADER_SRC_DIR}/ui.vert
    ${SHADER_SRC_DIR}/ui.frag
    ${SHADER_SRC_DIR}/object.vert
    ${SHADER_SRC_DIR}/ship.vert
    ${SHADER_SRC_DIR}/ocean.vert
    ${SHADER_SRC_DIR}/ocean.frag
    ${SHADER_SRC_DIR}/ocean_spectrum.comp
    ${SHADER_SRC_DIR}/ocean_spectrum_update.comp
    ${SHADER_SRC_DIR}/ocean_fft.comp
    ${SHADER_SRC_DIR}/grass.vert
    ${SHADER_SRC_DIR}/grass.frag
    ${SHADER_SRC_DIR}/shadow.vert
    ${SHADER_SRC_DIR}/shadow_object.vert
    ${SHADER_SRC_DIR}/shadow_player.vert
    ${SHADER_SRC_DIR}/shadow_grass.vert
    ${SHADER_SRC_DIR}/shadow_grass.frag
    ${SHADER_SRC_DIR}/post.vert
    ${SHADER_SRC_DIR}/post.frag
    ${SHADER_SRC_DIR}/smaa_edge.frag
    ${SHADER_SRC_DIR}/smaa_blend.frag
    ${SHADER_SRC_DIR}/smaa_neighborhood.frag
)

set(SPIRV_OUTPUTS)
foreach(SHADER ${SHADERS})
    get_filename_component(NAME ${SHADER} NAME)
    set(OUT ${CMAKE_BINARY_DIR}/spirv/${NAME}.spv)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/spirv
        COMMAND ${GLSLC} ${SHADER} -o ${OUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${NAME}"
    )
    list(APPEND SPIRV_OUTPUTS ${OUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_OUTPUTS})
add_dependencies(engine shaders)

# Copy compiled shaders next to the executable
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/triangle.vert.spv ${SHADER_OUT_DIR}/triangle.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/triangle.frag.spv ${SHADER_OUT_DIR}/triangle.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/chunk.vert.spv    ${SHADER_OUT_DIR}/chunk.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/chunk.frag.spv    ${SHADER_OUT_DIR}/chunk.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ui.vert.spv       ${SHADER_OUT_DIR}/ui.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ui.frag.spv       ${SHADER_OUT_DIR}/ui.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/object.vert.spv   ${SHADER_OUT_DIR}/object.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ship.vert.spv     ${SHADER_OUT_DIR}/ship.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ocean.vert.spv    ${SHADER_OUT_DIR}/ocean.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ocean.frag.spv    ${SHADER_OUT_DIR}/ocean.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ocean_spectrum.comp.spv ${SHADER_OUT_DIR}/ocean_spectrum.comp.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ocean_spectrum_update.comp.spv ${SHADER_OUT_DIR}/ocean_spectrum_update.comp.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/ocean_fft.comp.spv ${SHADER_OUT_DIR}/ocean_fft.comp.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/grass.vert.spv    ${SHADER_OUT_DIR}/grass.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/grass.frag.spv    ${SHADER_OUT_DIR}/grass.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/shadow.vert.spv  ${SHADER_OUT_DIR}/shadow.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/shadow_object.vert.spv ${SHADER_OUT_DIR}/shadow_object.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/shadow_player.vert.spv ${SHADER_OUT_DIR}/shadow_player.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/shadow_grass.vert.spv ${SHADER_OUT_DIR}/shadow_grass.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/shadow_grass.frag.spv ${SHADER_OUT_DIR}/shadow_grass.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/post.vert.spv ${SHADER_OUT_DIR}/post.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/post.frag.spv ${SHADER_OUT_DIR}/post.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/smaa_edge.frag.spv ${SHADER_OUT_DIR}/smaa_edge.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/smaa_blend.frag.spv ${SHADER_OUT_DIR}/smaa_blend.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/spirv/smaa_neighborhood.frag.spv ${SHADER_OUT_DIR}/smaa_neighborhood.frag.spv
)

# Copy authored assets next to the executable.
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASSET_OUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASSET_SRC_DIR} ${ASSET_OUT_DIR}
)

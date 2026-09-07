cmake_minimum_required(VERSION 3.20)
project(ionosphere-live-3d LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSYS2 UCRT64 paths (solo Windows; Linux/CI usa paquetes del sistema.
# NOTA: glm no esta vendido en libs/ — en Windows llega desde el paquete
# mingw de MSYS2 y en Linux desde el include path del sistema.)
if(WIN32)
    set(CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
endif()

# ── Dependencies ──────────────────────────────────────────────────────
find_package(glfw3 3.4 REQUIRED)
find_package(CURL REQUIRED)

# ── GLAD (vendored, generated for GL 4.0 core) ──────────────────────
add_library(glad STATIC libs/glad/src/glad.c)
target_include_directories(glad PUBLIC libs/glad/include)

# ── Dear ImGui (vendored) ────────────────────────────────────────────
add_library(imgui STATIC
    libs/imgui/imgui.cpp
    libs/imgui/imgui_draw.cpp
    libs/imgui/imgui_tables.cpp
    libs/imgui/imgui_widgets.cpp
    libs/imgui/imgui_demo.cpp
    libs/imgui/backends/imgui_impl_glfw.cpp
    libs/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC libs/imgui libs/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

# ── Sources ──────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/App.cpp
    src/Render/Renderer.cpp
    src/Render/Camera.cpp
    src/Render/Shader.cpp
    src/Render/Framebuffer.cpp
    src/Globe/Earth.cpp
    src/Globe/Atmosphere.cpp
    src/Globe/Terminator.cpp
    src/Globe/Stars.cpp
    src/Ionosphere/GridLayer.cpp
    src/Ionosphere/VolumeRenderer.cpp
    src/Ionosphere/IonosondeLayer.cpp
    src/Ionosphere/SolarWindLayer.cpp
    src/Ionosphere/FaradayLayer.cpp
    src/Ionosphere/RefShells.cpp
    src/Ionosphere/SliceLayer.cpp
    src/Data/DataManager.cpp
    src/Data/EsaAdapter.cpp
    src/Data/GloTecAdapter.cpp
    src/Data/GiroAdapter.cpp
    src/Data/NoaaAdapter.cpp
    src/Data/SolarIndicesAdapter.cpp
    src/Data/SolarWindAdapter.cpp
    src/Data/RadioPropagationAdapter.cpp
    src/Data/AuroraAdapter.cpp
    src/Data/EbroAdapter.cpp
    src/Data/XrayAdapter.cpp
    src/Data/DiasAdapter.cpp
    src/UI/SidePanel.cpp
    src/UI/TimelineBar.cpp
    src/UI/StationPanel.cpp
    src/Utils/CoordinateSystem.cpp
    src/Utils/SolarPosition.cpp
)

# ── Executable ───────────────────────────────────────────────────────
add_executable(ionosphere-live-3d ${SOURCES})

target_include_directories(ionosphere-live-3d PRIVATE
    libs/glad/include
    libs/glm
    libs/stb
    libs/imgui
    libs/imgui/backends
    src
)

target_link_libraries(ionosphere-live-3d PRIVATE
    glad
    imgui
    glfw
    CURL::libcurl
)
if(WIN32)
    target_link_libraries(ionosphere-live-3d PRIVATE opengl32)
else()
    # Linux: glad resuelve TODOS los entry points de GL en runtime via
    # glfwGetProcAddress -> no hace falta enlazar libGL.
    # Threads::Threads para std::thread.
    find_package(Threads REQUIRED)
    target_link_libraries(ionosphere-live-3d PRIVATE Threads::Threads)
endif()

# ── Copy shaders to build dir ───────────────────────────────────────
add_custom_command(TARGET ionosphere-live-3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:ionosphere-live-3d>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/textures $<TARGET_FILE_DIR:ionosphere-live-3d>/textures
)
# M0.3: data/ es estado en runtime (no trackeado en git, no va en los
# ZIP) -> copiar SOLO si existe, o el POST_BUILD revienta en checkout limpio.
if(EXISTS "${CMAKE_SOURCE_DIR}/data")
    add_custom_command(TARGET ionosphere-live-3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:ionosphere-live-3d>/data
    )
endif()

# Tests (logica pura: sin GL/GLFW/red — corren en cualquier CI).
# Condicion 5 de la revision MUSE. M4 anadira test_getbest_parse (fixture
# real) y M5 test_model_foF2; este target deja la infraestructura CTest lista.
enable_testing()
add_executable(test_hop_geometry tests/test_hop_geometry.cpp)
target_include_directories(test_hop_geometry PRIVATE src)
add_test(NAME hop_geometry COMMAND test_hop_geometry)

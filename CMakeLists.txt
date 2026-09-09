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
# Condicion 5 de la revision MUSE. M4: test_getbest_parse con fixture real
# (multichar EB040 capturado en vivo); M5: test_model_foF2 (clima+haversine);
# M7: test_d_region (absorcion D + LUF-SID).

enable_testing()
# M4 (7.1): parser multichar de getbest — llama a PRODUCCION
# (GiroAdapter::parseGetbest real sobre fixture real), no replica.
add_executable(test_getbest_parse tests/test_getbest_parse.cpp)
target_include_directories(test_getbest_parse PRIVATE src)
target_sources(test_getbest_parse PRIVATE src/Data/GiroAdapter.cpp)
target_link_libraries(test_getbest_parse PRIVATE CURL::libcurl)
if(NOT WIN32)
    # Linux: GiroAdapter.cpp usa std::thread -> pthread. En MSYS2 (winpthreads)
    # lo enlaza el driver de g++ solo; en Windows Threads::Threads no existe.
    target_link_libraries(test_getbest_parse PRIVATE Threads::Threads)
endif()
add_test(NAME getbest_parse COMMAND test_getbest_parse)
# M5 (plan 8.2/8.3): modelo climatologico + haversine — llama a
# PRODUCCION (inline de LayerProfile.h + densityDir de DensityVolume.h
# + SolarPosition real para el check de convencion subsolar), no replica.
add_executable(test_model_foF2 tests/test_model_foF2.cpp)
# glad: transitivo via LayerProfile.h -> IonosondeLayer.h (solo cabeceras,
# sin contexto GL - el link no cambia).
target_include_directories(test_model_foF2 PRIVATE src libs/glad/include)
target_sources(test_model_foF2 PRIVATE src/Utils/SolarPosition.cpp)
add_test(NAME model_foF2 COMMAND test_model_foF2)
# M7 (plan 10): absorcion D Sauer-Wilkinson + termino SID del LUF — llama a
# PRODUCCION (deriveLUF/flareAbsorption/dRegionAbsorptionDb de
# RadioPropagationAdapter), no replica. Sin std::thread: solo CURL.
add_executable(test_d_region tests/test_d_region.cpp)
target_include_directories(test_d_region PRIVATE src)
target_sources(test_d_region PRIVATE src/Data/RadioPropagationAdapter.cpp)
target_link_libraries(test_d_region PRIVATE CURL::libcurl)
add_test(NAME d_region COMMAND test_d_region)
add_executable(test_hop_geometry tests/test_hop_geometry.cpp)
target_include_directories(test_hop_geometry PRIVATE src)
add_test(NAME hop_geometry COMMAND test_hop_geometry)
add_executable(test_m2_sun tests/test_m2_sun.cpp src/Utils/SolarPosition.cpp)
target_include_directories(test_m2_sun PRIVATE src)
add_test(NAME m2_sun COMMAND test_m2_sun)

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
    src/Ionosphere/HFTraceLayer.cpp
    src/Data/DataManager.cpp
    src/Data/EsaAdapter.cpp
    src/Data/GloTecAdapter.cpp
    src/Data/GiroAdapter.cpp
    src/Data/Kc2gAdapter.cpp
    src/Data/NoaaAdapter.cpp
    src/Data/SolarIndicesAdapter.cpp
    src/Data/SolarWindAdapter.cpp
    src/Data/RadioPropagationAdapter.cpp
    src/Data/AuroraAdapter.cpp
    src/Data/EbroAdapter.cpp
    src/Data/XrayAdapter.cpp
    src/Data/DiasAdapter.cpp
    src/Data/TecCache.cpp  # M10: cache TEC en disco (puro, sin curl/GL)
    src/Data/Kc2gCache.cpp  # FASE B (P2): cache kc2g en disco (puro, sin curl/GL)
    src/Data/IrtamCache.cpp  # M-IRTAM (F1): cache IRTAM en disco (puro, sin curl/GL)
    src/Data/IrtamCoeffParse.cpp  # M-IRTAM-F2 (P1a): parse gambit-coeffs (puro, sin curl/GL)
    src/Data/IrtamCoeffCache.cpp  # M-IRTAM-F2 (P2): cache numerica (puro, sin curl/GL)
    src/Data/LgdcPacing.cpp  # M-IRTAM-F2 (P3): gate lgdc compartido (puro, sin curl/GL)
    src/Data/LgdcTrace.cpp  # Ciclo traza: log stderr con timestamp (solo-log, sin curl/GL)
    src/Data/ProviderStatus.cpp  # M-prov-matrix (048): estado por proveedor (puro, sin curl/GL)
    src/Data/IrtamCoeffAdapter.cpp  # M-IRTAM-F2 (P4): adaptador gambit (curl/gate; URL vive aqui)
    src/Data/IrtamGridEval.cpp  # M-IRTAM-F2 (P1b): eval Jones-Gallet 46x45 (puro, sin curl/GL)
    src/Data/IrtamState.cpp  # M-IRTAM-F2 (P5): estados honestos + replay (puro, sin curl/GL)
    src/Data/SdoImageAdapter.cpp  # M11: imagenes SDO (stb decode en worker)
    src/UI/SidePanel.cpp
    src/UI/TimelineBar.cpp
    src/UI/StationPanel.cpp
    src/UI/SunPanel.cpp  # M11: ventana Sun
    src/Utils/CoordinateSystem.cpp
    src/Utils/SolarPosition.cpp
    src/Utils/Exporter.cpp  # S-1: exportacion PNG+CSV (logica pura + stb write)
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
# M7: test_d_region (absorcion D + LUF-SID); M8: test_hf_circuit
# (circuito HF: geodesia/slerp/bandState/MUF-FOT inline en HFTraceLayer.h);
# M4R-A: test_kc2g_parse (failover kc2g: parser puro + time-filter +
# fixture real de prop.kc2g.com; segunda implementacion = generador Python).

enable_testing()
# M4 (7.1): parser multichar de getbest — llama a PRODUCCION
# (GiroAdapter::parseGetbest real sobre fixture real), no replica.
add_executable(test_getbest_parse tests/test_getbest_parse.cpp)
target_include_directories(test_getbest_parse PRIVATE src)
target_sources(test_getbest_parse PRIVATE src/Data/GiroAdapter.cpp src/Data/LgdcPacing.cpp src/Data/LgdcTrace.cpp)  # M-IRTAM-F2 (P3): GiroAdapter registra en el gate (+ traza solo-log)
target_link_libraries(test_getbest_parse PRIVATE CURL::libcurl)
if(NOT WIN32)
    # Linux: GiroAdapter.cpp usa std::thread -> pthread. En MSYS2 (winpthreads)
    # lo enlaza el driver de g++ solo; en Windows Threads::Threads no existe.
    target_link_libraries(test_getbest_parse PRIVATE Threads::Threads)
endif()
add_test(NAME getbest_parse COMMAND test_getbest_parse)
# M4R-A (FASE A): parser kc2g del failover — llama a PRODUCCION
# (Kc2gAdapter::parseStations/gridUsable/panelUsable/toSample sobre
# fixture real capturado en vivo), no replica. LayerProfile.h (check del
# sello sanitizeB0/B1) via IonosondeLayer.h: glad/glm solo cabeceras
# (patron model_foF2). std::thread -> pthread en Linux (patron getbest).
add_executable(test_kc2g_parse tests/test_kc2g_parse.cpp)
target_include_directories(test_kc2g_parse PRIVATE src libs/glad/include)
target_sources(test_kc2g_parse PRIVATE src/Data/Kc2gAdapter.cpp src/Data/Kc2gCache.cpp)
target_link_libraries(test_kc2g_parse PRIVATE CURL::libcurl)
if(NOT WIN32)
    target_link_libraries(test_kc2g_parse PRIVATE Threads::Threads)
endif()
add_test(NAME kc2g_parse COMMAND test_kc2g_parse)
# M5 (plan 8.2/8.3): modelo climatologico + haversine — llama a
# PRODUCCION (inline de LayerProfile.h + densityDir de DensityVolume.h
# + SolarPosition real para el check de convencion subsolar), no replica.
add_executable(test_model_foF2 tests/test_model_foF2.cpp)
# glad: transitivo via LayerProfile.h -> IonosondeLayer.h (solo cabeceras,
# sin contexto GL - el link no cambia).
target_include_directories(test_model_foF2 PRIVATE src libs/glad/include)
target_sources(test_model_foF2 PRIVATE src/Utils/SolarPosition.cpp)
add_test(NAME model_foF2 COMMAND test_model_foF2)
add_executable(test_layer_winner tests/test_layer_winner.cpp)
target_include_directories(test_layer_winner PRIVATE src libs/glad/include)
add_test(NAME layer_winner COMMAND test_layer_winner)
add_executable(test_exporter tests/test_exporter.cpp src/Utils/Exporter.cpp)
target_include_directories(test_exporter PRIVATE src libs/glad/include libs/stb)
add_test(NAME exporter COMMAND test_exporter)
add_executable(test_volume_shadow tests/test_volume_shadow.cpp)
target_include_directories(test_volume_shadow PRIVATE src)
add_test(NAME volume_shadow COMMAND test_volume_shadow)
add_executable(test_volume_jitter tests/test_volume_jitter.cpp)
target_include_directories(test_volume_jitter PRIVATE src)
add_test(NAME volume_jitter COMMAND test_volume_jitter)
add_executable(test_tour tests/test_tour.cpp)
target_include_directories(test_tour PRIVATE src)
add_test(NAME tour COMMAND test_tour)
# M7 (plan 10): absorcion D Sauer-Wilkinson + termino SID del LUF — llama a
# PRODUCCION (deriveLUF/flareAbsorption/dRegionAbsorptionDb de
# RadioPropagationAdapter), no replica. Sin std::thread: solo CURL.
add_executable(test_d_region tests/test_d_region.cpp)
target_include_directories(test_d_region PRIVATE src)
target_sources(test_d_region PRIVATE src/Data/RadioPropagationAdapter.cpp)
target_link_libraries(test_d_region PRIVATE CURL::libcurl)
add_test(NAME d_region COMMAND test_d_region)
# M8 (plan 11): circuito HF — llama a PRODUCCION (namespace hfc inline en
# HFTraceLayer.h sobre HfMath.h), no replica. glm/glad: solo cabeceras
# via HFTraceLayer.h (patron model_foF2), sin link y sin std::thread.
add_executable(test_hf_circuit tests/test_hf_circuit.cpp)
target_include_directories(test_hf_circuit PRIVATE src libs/glad/include libs/glm)
add_test(NAME hf_circuit COMMAND test_hf_circuit)
add_executable(test_hop_geometry tests/test_hop_geometry.cpp)
target_include_directories(test_hop_geometry PRIVATE src)
add_test(NAME hop_geometry COMMAND test_hop_geometry)
add_executable(test_m2_sun tests/test_m2_sun.cpp src/Utils/SolarPosition.cpp)
target_include_directories(test_m2_sun PRIVATE src)
add_test(NAME m2_sun COMMAND test_m2_sun)
# M10 (cache TEC en disco): logica pura, sin GL/curl. Llama a PRODUCCION
# (namespace teccache en src/Data/TecCache), no replica.
add_executable(test_tec_cache tests/test_tec_cache.cpp src/Data/TecCache.cpp)
target_include_directories(test_tec_cache PRIVATE src)
add_test(NAME tec_cache COMMAND test_tec_cache)
# M11 (SDO): matematica del disco + λs. Llama a PRODUCCION (sundisk +
# SolarPosition::getApparentEclipticLonDeg), no replica.
add_executable(test_sdo_projection tests/test_sdo_projection.cpp src/Utils/SolarPosition.cpp)
target_include_directories(test_sdo_projection PRIVATE src libs/glm)
add_test(NAME sdo_projection COMMAND test_sdo_projection)
# M11 (SDO): parseo sunspot_report con fixture real en vivo — llama a
# PRODUCCION (SolarIndicesAdapter::parseSunspotReport), no replica.
# std::thread -> pthread en Linux (patron getbest/kc2g).
add_executable(test_sdo_adapter tests/test_sdo_adapter.cpp)
target_include_directories(test_sdo_adapter PRIVATE src)
target_sources(test_sdo_adapter PRIVATE src/Data/SolarIndicesAdapter.cpp)
target_link_libraries(test_sdo_adapter PRIVATE CURL::libcurl)
if(NOT WIN32)
    target_link_libraries(test_sdo_adapter PRIVATE Threads::Threads)
endif()
add_test(NAME sdo_adapter COMMAND test_sdo_adapter)
# FASE B (P1): historia kc2g por acumulacion — llama a PRODUCCION
# (Kc2gAdapter::accumulateBatch/sampleHistoryAt), no replica.
add_executable(test_kc2g_history tests/test_kc2g_history.cpp)
target_include_directories(test_kc2g_history PRIVATE src)
target_sources(test_kc2g_history PRIVATE src/Data/Kc2gAdapter.cpp src/Data/Kc2gCache.cpp)
target_link_libraries(test_kc2g_history PRIVATE CURL::libcurl)
if(NOT WIN32)
    target_link_libraries(test_kc2g_history PRIVATE Threads::Threads)
endif()
add_test(NAME kc2g_history COMMAND test_kc2g_history)
# FASE B (P2): cache kc2g en disco — llama a PRODUCCION
# (namespace kc2gcache en src/Data/Kc2gCache), no replica.
add_executable(test_kc2g_cache tests/test_kc2g_cache.cpp src/Data/Kc2gCache.cpp)
target_include_directories(test_kc2g_cache PRIVATE src)
add_test(NAME kc2g_cache COMMAND test_kc2g_cache)
# M-IRTAM (F1): cache IRTAM en disco — llama a PRODUCCION
# (namespace irtamcache en src/Data/IrtamCache), no replica.
add_executable(test_irtam_cache tests/test_irtam_cache.cpp src/Data/IrtamCache.cpp)
target_include_directories(test_irtam_cache PRIVATE src)
add_test(NAME irtam_cache COMMAND test_irtam_cache)
# M-IRTAM-F2 (P1a): parse gambit-coeffs — llama a PRODUCCION
# (namespace irtamcoeff en src/Data/IrtamCoeffParse), no replica.
add_executable(test_irtam_coeff_parse tests/test_irtam_coeff_parse.cpp src/Data/IrtamCoeffParse.cpp)
target_include_directories(test_irtam_coeff_parse PRIVATE src)
add_test(NAME irtam_coeff_parse COMMAND test_irtam_coeff_parse)
# M-IRTAM-F2 (P2): cache numerica — llama a PRODUCCION
# (namespace irtamccache en src/Data/IrtamCoeffCache), no replica.
add_executable(test_irtamc_cache tests/test_irtamc_cache.cpp src/Data/IrtamCoeffCache.cpp src/Data/IrtamCoeffParse.cpp)
target_include_directories(test_irtamc_cache PRIVATE src)
add_test(NAME irtamc_cache COMMAND test_irtamc_cache)
# M-IRTAM-F2 (P3): gate lgdc compartido — llama a PRODUCCION
# (namespace lgdcpacing en src/Data/LgdcPacing), no replica.
add_executable(test_irtamc_gate tests/test_irtamc_gate.cpp src/Data/LgdcPacing.cpp)
target_include_directories(test_irtamc_gate PRIVATE src)
add_test(NAME irtamc_gate COMMAND test_irtamc_gate)
# M-IRTAM-F2 (P4): planificacion del backfill — llama a PRODUCCION
# (namespace irtamfetch en src/Data/IrtamCoeffAdapter), no replica.
# El adapter usa curl (twin getbest_parse): enlaza CURL::libcurl.
add_executable(test_irtam_adapter tests/test_irtam_adapter.cpp src/Data/IrtamCoeffAdapter.cpp src/Data/IrtamCoeffCache.cpp src/Data/IrtamCoeffParse.cpp src/Data/LgdcPacing.cpp src/Data/LgdcTrace.cpp)
target_include_directories(test_irtam_adapter PRIVATE src libs/glad/include)  # GridData via GridLayer.h: glad solo cabeceras (patron kc2g_parse)
target_link_libraries(test_irtam_adapter PRIVATE CURL::libcurl)
if(NOT WIN32)
    target_link_libraries(test_irtam_adapter PRIVATE Threads::Threads)
endif()
add_test(NAME irtam_adapter COMMAND test_irtam_adapter)
# M-IRTAM-F2 (P1b): evaluacion Jones-Gallet — llama a PRODUCCION
# (namespace irtameval en src/Data/IrtamGridEval), no replica.
add_executable(test_irtam_grid_eval tests/test_irtam_grid_eval.cpp src/Data/IrtamGridEval.cpp src/Data/IrtamCoeffParse.cpp)
target_include_directories(test_irtam_grid_eval PRIVATE src libs/glad/include)  # GridData via GridLayer.h (patron kc2g_parse)
add_test(NAME irtam_grid_eval COMMAND test_irtam_grid_eval)
# M-IRTAM-F2 (P5): estados honestos + replay — llama a PRODUCCION
# (namespace irtamstate en src/Data/IrtamState), no replica.
add_executable(test_irtam_state tests/test_irtam_state.cpp src/Data/IrtamState.cpp)
target_include_directories(test_irtam_state PRIVATE src)
add_test(NAME irtam_state COMMAND test_irtam_state)
# Ciclo traza: formato + sumidero — llama a PRODUCCION (lgdctrace), no replica.
add_executable(test_lgdc_trace tests/test_lgdc_trace.cpp src/Data/LgdcTrace.cpp)
target_include_directories(test_lgdc_trace PRIVATE src)
add_test(NAME lgdc_trace COMMAND test_lgdc_trace)
# M-prov-matrix (048): matriz de estado — llama a PRODUCCION (provstatus), no replica.
add_executable(test_provider_status tests/test_provider_status.cpp src/Data/ProviderStatus.cpp)
target_include_directories(test_provider_status PRIVATE src)
add_test(NAME provider_status COMMAND test_provider_status)

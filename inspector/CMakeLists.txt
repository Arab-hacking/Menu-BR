cmake_minimum_required(VERSION 3.18)
project(br_imgui_inspector_16_111_19050 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

set(IMGUI_CORE_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
)

set(BR_INSPECTOR_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/safe_memory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/arm64_hook.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/arm64_disasm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/internal_imgui_bridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game_reader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/network_catalog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/inspector_ui_helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/inspector_menu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/inspector_menu_memory_export.cpp"
)

if(ANDROID)
    list(APPEND IMGUI_ANDROID_BACKEND "${IMGUI_DIR}/imgui_impl_opengl3.cpp")
endif()

add_library(br_inspector_16111 SHARED
    ${IMGUI_CORE_SOURCES}
    ${IMGUI_ANDROID_BACKEND}
    ${BR_INSPECTOR_CORE_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main_entry.cpp"
)

target_include_directories(br_inspector_16111 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${IMGUI_DIR}"
)

target_compile_options(br_inspector_16111 PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fvisibility=hidden
    -fno-exceptions
    -fno-rtti
)

if(ANDROID)
    find_library(LOG_LIB log REQUIRED)
    find_library(GLESV3_LIB GLESv3 REQUIRED)
    find_library(EGL_LIB EGL REQUIRED)
    target_compile_definitions(br_inspector_16111 PRIVATE IMGUI_IMPL_OPENGL_ES3)
    target_link_libraries(br_inspector_16111 PRIVATE
        ${LOG_LIB}
        ${GLESV3_LIB}
        ${EGL_LIB}
        dl
    )
    target_link_options(br_inspector_16111 PRIVATE
        "-Wl,--gc-sections"
        "-Wl,--exclude-libs,ALL"
    )
else()
    target_link_libraries(br_inspector_16111 PRIVATE dl pthread)
endif()

option(BR_BUILD_HOST_SELFTEST "Build host ABI & headless ImGui verification test" OFF)
if(BR_BUILD_HOST_SELFTEST AND NOT ANDROID)
    add_executable(br_inspector_selftest
        ${IMGUI_CORE_SOURCES}
        ${BR_INSPECTOR_CORE_SOURCES}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/selftest_main.cpp"
    )
    target_include_directories(br_inspector_selftest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${IMGUI_DIR}"
    )
    target_compile_options(br_inspector_selftest PRIVATE -Wall -Wextra)
    target_link_libraries(br_inspector_selftest PRIVATE dl pthread)
endif()

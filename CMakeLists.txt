# Menu-BR — сборка libmenu-br.so через Android NDK
#
#   export NDK=$HOME/Android/Sdk/ndk/28.2.13004772
#   cmake -B build/ndk \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
#     -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ndk
#
cmake_minimum_required(VERSION 3.22)
project(MenuBR C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(menu-br SHARED
    src/main.cpp
    src/mem.cpp
    src/elf.cpp
    src/off.cpp
    src/probe.cpp
    src/cfg.cpp
    src/log.cpp
    src/detect.cpp
    src/state.cpp
    src/feat.cpp
    src/glapi.cpp
    src/hooks.cpp
    src/input.cpp
    src/ui_gl.cpp
    src/ui_menu.cpp
    src/ui_esp.cpp
    src/rt_shim.c
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
)

target_include_directories(menu-br PRIVATE src include third_party/imgui)

target_compile_options(menu-br PRIVATE
    -O2 -fno-exceptions -fno-rtti -fno-stack-protector -fvisibility=hidden
)
target_compile_definitions(menu-br PRIVATE _GNU_SOURCE)

# NDK: сами библиотеки (bionic) — ничего дополнительно линковать не нужно;
# GL/EGL/Android подгружаются через dlopen в рантайме.
if(ANDROID)
    target_link_options(menu-br PRIVATE "LINKER:--gc-sections")
endif()

set_target_properties(menu-br PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

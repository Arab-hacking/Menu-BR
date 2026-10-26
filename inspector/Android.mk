LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := br_inspector_16111
LOCAL_CPP_EXTENSION := .cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/third_party/imgui

LOCAL_SRC_FILES := \
    third_party/imgui/imgui.cpp \
    third_party/imgui/imgui_draw.cpp \
    third_party/imgui/imgui_tables.cpp \
    third_party/imgui/imgui_widgets.cpp \
    third_party/imgui/imgui_impl_opengl3.cpp \
    src/safe_memory.cpp \
    src/arm64_hook.cpp \
    src/arm64_disasm.cpp \
    src/internal_imgui_bridge.cpp \
    src/game_reader.cpp \
    src/network_catalog.cpp \
    src/inspector_ui_helpers.cpp \
    src/inspector_menu.cpp \
    src/inspector_menu_memory_export.cpp \
    src/main_entry.cpp

LOCAL_CPPFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -fno-exceptions -fno-rtti -DIMGUI_IMPL_OPENGL_ES3
LOCAL_LDFLAGS  := -Wl,--gc-sections -Wl,--exclude-libs,ALL
LOCAL_LDLIBS   := -llog -lGLESv3 -lEGL -ldl

include $(BUILD_SHARED_LIBRARY)

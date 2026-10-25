#!/usr/bin/env bash
#
# Menu-BR — сборка libmenu-br.so (Android arm64)
#
# ТЕКУЩАЯ БАЗА (3-й виток): проект br_imgui_inspector_16.111.19050 юзера
# целиком (inspector/) + наши мод-функции (inspector/src/mod_features.*):
#   ./build.sh zig        -> build/libmenu-br.so  (NEEDED: libc,libm,libdl,libGLESv3,libEGL)
#
# Старый вариант (2-й виток, src/ + imgui 1.91.9b) доступен как:
#   ./build.sh legacy-zig -> build/libmenu-br-legacy.so
#
# Zig-кросс: glibc-хедеры + наши shim-хедеры (tools/shim_include:
# android/log.h, jni.h, EGL/egl.h, GLES3/gl3.h, link.h).
# Критично: -D__ANDROID__ (код референса ветвится по нему: хуки, EGL, GL, лог).
# Лог: strong-символ __android_log_print (inspector/src/android_log_shim.cpp)
#   -> /sdcard/Android/data/com.br.top/files/brmenu.log + форвард в logcat
#      через dlopen("liblog.so") — поэтому NEEDED liblog.so НЕ нужен.
#
# NDK (если появится): cmake в inspector/ (там CMakeLists.txt) + добавить
# inspector/src/{mod_features,android_log_shim,entry_compat}.cpp в sources.
#
set -e
cd "$(dirname "$0")"

ZIG=${ZIG:-$(command -v zig || echo /tmp/zig15/lib/python3.11/site-packages/ziglang/zig)}

# ============================================================================
# Текущий билд: inspector-проект юзера + mod_features
# ============================================================================
build_inspector() {
    [ -x "$ZIG" ] || { echo "zig не найден: $ZIG (укажите через ZIG=...)"; exit 1; }
    mkdir -p build/obj

    local CFLAGS="-target aarch64-linux-gnu -O2 -fno-stack-protector -fvisibility=hidden \
-ffunction-sections -fno-exceptions -fno-rtti -std=c++17 -D_GNU_SOURCE -D__ANDROID__ \
-DIMGUI_IMPL_OPENGL_ES3 -Iinspector/include -Iinspector/third_party/imgui -Itools/shim_include"

    # Источники референса (как в его CMakeLists.txt) + наши
    local SRC
    SRC=$(ls inspector/src/*.cpp inspector/src/rt_compat_c.c | sort)
    local IMGU
    IMGU=$(ls inspector/third_party/imgui/imgui.cpp \
              inspector/third_party/imgui/imgui_draw.cpp \
              inspector/third_party/imgui/imgui_tables.cpp \
              inspector/third_party/imgui/imgui_widgets.cpp \
              inspector/third_party/imgui/imgui_impl_opengl3.cpp | sort)

    for f in $SRC $IMGU; do
        local o="build/obj/insp_$(basename "$f" | sed 's/\.\(c\|cpp\)$/.o/')"
        [ -f "$o" ] && [ "$o" -nt "$f" ] && continue
        case "$f" in
            *.c) # shellcheck disable=SC2086
                 "$ZIG" cc $CFLAGS -std=c11 -c "$f" -o "$o" ;;
            *)   # shellcheck disable=SC2086
                 "$ZIG" c++ $CFLAGS -c "$f" -o "$o" ;;
        esac
    done

    # C++ runtime shim (operator new и т.п.)
    local o="build/obj/insp_rt_shim.o"
    [ -f "$o" ] && [ "$o" -nt src/rt_shim.c ] || "$ZIG" cc -target aarch64-linux-gnu -O2 \
        -fno-stack-protector -fvisibility=hidden -c src/rt_shim.c -o "$o"

    local OBJS
    OBJS=$(ls build/obj/insp_*.o)

    local SO=build/libmenu-br.so
    link_inspector() {
        if [ -n "${FAKE:-}" ]; then
            # shellcheck disable=SC2086
            "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o "$SO" \
                -Wl,-soname,libmenu-br.so $OBJS $FAKE -Wl,--gc-sections
        else
            # shellcheck disable=SC2086
            "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o "$SO" \
                -Wl,-soname,libmenu-br.so $OBJS -Wl,--gc-sections
        fi
    }

    # pass 1: без stub'ов — собираем список UND
    link_inspector
    if command -v python3 >/dev/null 2>&1; then
        readelf --dyn-syms -W "$SO" \
            | awk '$7=="UND" && $8!~/^$/ {print $8}' | sort -u > build/und.txt
        # Классификация: bionic libc/libm/libdl + GLESv3/EGL
        python3 tools/gen_fake_libs.py build/und.txt build/ || exit 1
        FAKE=""
        for L in libc libm libdl libGLESv3 libEGL; do
            [ -s "build/fake_${L}.c" ] || continue
            "$ZIG" cc -target aarch64-linux-gnu -shared "build/fake_${L}.c" \
                -o "build/fake_${L}.so" -Wl,-soname,${L}.so
            FAKE="$FAKE build/fake_${L}.so"
        done
        # pass 2: со stub'ами -> NEEDED libc.so/libm.so/libdl.so/libGLESv3.so/libEGL.so
        FAKE="$(eval echo $FAKE)"
        link_inspector
        rm -f build/fake_*.so
    fi
    echo "→ $SO"
    echo "  NEEDED: $(readelf -d "$SO" | grep NEEDED | sed 's/.*\[/;s/\].*//' | tr '\n' ' ')"
    echo "  exports: $(nm -D --defined-only "$SO" | awk '{print $3}' | grep -E '^(MenuBR|br_inspector|JNI_OnLoad)' | tr '\n' ' ')"
}

# ============================================================================
# Legacy (2-й виток): src/ + imgui 1.91.9b -> libmenu-br-legacy.so
# ============================================================================
build_legacy() {
    [ -x "$ZIG" ] || { echo "zig не найден: $ZIG (укажите через ZIG=...)"; exit 1; }
    mkdir -p build/obj
    local SRC
    SRC=$(ls src/*.c src/*.cpp | sort)
    local IMGU
    IMGU=$(ls third_party/imgui/*.cpp | sort)

    local CFLAGS="-target aarch64-linux-gnu -O2 -fno-stack-protector -fvisibility=hidden -ffunction-sections -D_GNU_SOURCE"
    for f in $SRC; do
        local o="build/obj/$(basename "$f" | sed 's/\.c$/.o/;s/\.cpp$/.o/')"
        case "$f" in
            *.cpp) "$ZIG" c++ $CFLAGS -std=c++17 -fno-exceptions -fno-rtti -Isrc -Iinclude -Ithird_party/imgui -c "$f" -o "$o" ;;
            *.c)   "$ZIG" cc  $CFLAGS -Isrc -Iinclude -c "$f" -o "$o" ;;
        esac
    done
    for f in $IMGU; do
        local o="build/obj/$(basename "$f" | sed 's/\.cpp$/.o/')"
        "$ZIG" c++ $CFLAGS -std=c++17 -fno-exceptions -fno-rtti -Ithird_party/imgui -c "$f" -o "$o"
    done

    local SO=build/libmenu-br-legacy.so
    if command -v python3 >/dev/null 2>&1; then
        "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o "$SO" \
            -Wl,-soname,libmenu-br-legacy.so build/obj/*.o
        readelf --dyn-syms -W "$SO" | awk '$7=="UND" && $8!~/^$/ {print $8}' | sort -u > build/und.txt
        python3 tools/gen_fake_libs.py build/und.txt build/ || exit 1
        local FAKE=""
        for L in libc libm libdl; do
            [ -s "build/fake_${L}.c" ] || continue
            "$ZIG" cc -target aarch64-linux-gnu -shared "build/fake_${L}.c" \
                -o "build/fake_${L}.so" -Wl,-soname,${L}.so
            FAKE="$FAKE build/fake_${L}.so"
        done
        FAKE="$(eval echo $FAKE)"
        "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o "$SO" \
            -Wl,-soname,libmenu-br-legacy.so build/obj/*.o $FAKE
        rm -f build/fake_*.so
    else
        "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o "$SO" \
            -Wl,-soname,libmenu-br-legacy.so build/obj/*.o
    fi
    echo "→ $SO (NEEDED: $(readelf -d "$SO" | grep NEEDED | sed 's/.*\[/;s/\].*//' | tr '\n' ' '))"
}

case "${1:-}" in
    zig)        build_inspector ;;
    legacy-zig) build_legacy ;;
    *)
        echo "Использование: $0 zig | legacy-zig"
        exit 1
        ;;
esac

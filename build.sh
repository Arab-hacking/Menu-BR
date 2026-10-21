#!/usr/bin/env bash
#
# Menu-BR — сборка libmenu-br.so (Android arm64)
#
# Два способа:
#
# 1) Android NDK (рекомендуется, "правильный" путь):
#      export NDK=$HOME/Android/Sdk/ndk/28.2.13004772   # или ваш NDK
#      cmake -B build/ndk -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
#      cmake --build build/ndk
#      → build/ndk/libmenu-br.so
#
# 2) Zig (перекрёстная, без NDK) — как в песочнице этого репо:
#      ZIG=/path/to/zig ./build.sh zig
#      → build/libmenu-br.so
#    Компилируется с glibc-хедерами (zig cc -target aarch64-linux-gnu).
#    НУЖНА NEEDED "libc.so": на устройстве линкер bionic не резолвит
#    символы библиотек БЕЗ NEEDED (dlopen failed: cannot locate symbol).
#    Двухпроходная линковка: pass1 собирает UND-список, из него генерится
#    stub-библиотека (soname=libc.so) с определениями всех этих имён,
#    pass2 линкует её -> lld ставит DT_NEEDED libc.so; в рантайме
#    импорты резолвятся из bionic libc.so (он всегда в namespace приложения).
#    C++ runtime (operator new и т.п.) предоставляет rt_shim.c.
#
set -e
cd "$(dirname "$0")"

SRC=$(ls src/*.c src/*.cpp | sort)
IMGU=$(ls third_party/imgui/*.cpp | sort)

if [ "$1" = "zig" ]; then
    ZIG=${ZIG:-$(command -v zig || echo /tmp/zig15/lib/python3.11/site-packages/ziglang/zig)}
    [ -x "$ZIG" ] || { echo "zig не найден: $ZIG (укажите через ZIG=...)"; exit 1; }
    mkdir -p build/obj
    CFLAGS="-target aarch64-linux-gnu -O2 -fno-stack-protector -fvisibility=hidden -ffunction-sections -D_GNU_SOURCE"
    for f in $SRC; do
        o="build/obj/$(basename "$f" | sed 's/\.\(c\|cpp\)$/.o/')"
        case "$f" in
            *.cpp) "$ZIG" c++ $CFLAGS -std=c++17 -fno-exceptions -fno-rtti -Isrc -Iinclude -Ithird_party/imgui -c "$f" -o "$o" ;;
            *.c)   "$ZIG" cc  $CFLAGS -Isrc -Iinclude -c "$f" -o "$o" ;;
        esac
    done
    for f in $IMGU; do
        o="build/obj/$(basename "$f" | sed 's/\.cpp$/.o/')"
        "$ZIG" c++ $CFLAGS -std=c++17 -fno-exceptions -fno-rtti -Ithird_party/imgui -c "$f" -o "$o"
    done
    # -nostdlib: без авто-стубов glibc (их soname'ы libc.so.6 не существуют
    # на Android и тащат версионированные ссылки @GLIBC)
    link_so() {
        if [ -n "${FAKE:-}" ]; then
            # shellcheck disable=SC2086
            "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o build/libmenu-br.so \
                -Wl,-soname,libmenu-br.so build/obj/*.o $FAKE
        else
            "$ZIG" cc -target aarch64-linux-gnu -shared -nostdlib -o build/libmenu-br.so \
                -Wl,-soname,libmenu-br.so build/obj/*.o
        fi
    }

    # pass 1: без stub'ов — собираем список UND
    link_so
    if command -v python3 >/dev/null 2>&1; then
        readelf --dyn-syms -W build/libmenu-br.so \
            | awk '$7=="UND" && $8!~/^$/ {print $8}' | sort -u > build/und.txt
        # классификация имён по бьониковским таблицам (libc/libm/libdl)
        python3 tools/gen_fake_libs.py build/und.txt build/ || exit 1
        FAKE=""
        for L in libc libm libdl; do
            [ -s "build/fake_${L}.c" ] || continue
            "$ZIG" cc -target aarch64-linux-gnu -shared "build/fake_${L}.c" \
                -o "build/fake_${L}.so" -Wl,-soname,${L}.so
            FAKE="$FAKE build/fake_${L}.so"
        done
        # pass 2: со stub'ами -> NEEDED libc.so/libm.so/libdl.so
        FAKE="$(eval echo $FAKE)"
        link_so
        rm -f build/fake_*.so
    fi
    echo "→ build/libmenu-br.so (NEEDED: $(readelf -d build/libmenu-br.so | grep NEEDED | sed 's/.*\[//;s/\].*//' | tr '\n' ' '))"
    exit 0
fi

# NDK-путь (см. шапку) — просто подсказка
echo "Используйте CMake + NDK (см. начало файла) или: $0 zig"
exit 1

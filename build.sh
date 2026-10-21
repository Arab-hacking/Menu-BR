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
#    Компилируется с glibc-хедерами (zig cc -target aarch64-linux-gnu),
#    но БЕЗ линковки libc: в .so нет NEEDED-записей, все символы
#    резолвятся в рантайме из bionic (libc.so) процесса игры.
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
    "$ZIG" build-lib build/obj/*.o -target aarch64-linux-gnu -dynamic -fsoname=libmenu-br.so -femit-bin=build/libmenu-br.so
    echo "→ build/libmenu-br.so"
    exit 0
fi

# NDK-путь (см. шапку) — просто подсказка
echo "Используйте CMake + NDK (см. начало файла) или: $0 zig"
exit 1

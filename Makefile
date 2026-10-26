# Menu-BR — обёртка над build.sh
#
#   make          — собрать .so через NDK (нужен NDK в $NDK) или zig
#   make zig      — перекрёстная сборка через zig (ZIG=/путь/к/zig)
#   make test     — хост-тест (x86_64) логики: ELF/PLT/скан/пробы
#                   (нужен файл игры: GAME_SO=/путь/libblackrussia-client.so)
#
ZIG ?= $(shell command -v zig 2>/dev/null || echo /tmp/zig15/lib/python3.11/site-packages/ziglang/zig)
export ZIG
GAME_SO ?= reference/libblackrussia-client.so

all:
	./build.sh zig

zig:
	./build.sh zig

test:
	ZIG=$(ZIG) make -C tools test GAME_SO=$(abspath $(GAME_SO))

clean:
	rm -rf build tools/build

.PHONY: all zig test clean

#!/usr/bin/env python3
"""gen_fake_libs.py — генерит stub-библиотеки для корректных DT_NEEDED.

Аргументы:
  <und.txt>                  список UND-имён (по одному на строку)
  <out_dir>                  куда писать fake_libc.c / fake_libm.c / fake_libdl.c

Каждое UND-имя классифицируется по таблицам экспортов bionic
(tools/bionic_lib{c,m,dl}_syms.txt) в «свою» библиотеку. Если имя
не найдено ни в одной — ОШИБКА (такой dlopen упадёт на устройстве).
Заглушки линкуются в финальный .so -> lld ставит DT_NEEDED
libc.so/libm.so/libdl.so; в рантайме символы резолвятся из bionic.
"""
import os
import sys

DATA = {"stdout", "stderr", "stdin", "environ"}


def load(path):
    with open(path) as f:
        return {l.strip() for l in f if l.strip() and not l.startswith("#")}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    libs = {
        "libc": load(os.path.join(here, "bionic_libc_syms.txt")),
        "libm": load(os.path.join(here, "bionic_libm_syms.txt")),
        "libdl": load(os.path.join(here, "bionic_libdl_syms.txt")),
    }

    names = []
    with open(sys.argv[1]) as f:
        for line in f:
            n = line.strip().split("@")[0]
            if n and n not in names:
                names.append(n)

    out_dir = sys.argv[2]
    buckets = {lib: [] for lib in libs}
    bad = []
    for n in names:
        # приоритет: libm (математика специфичнее) -> libdl -> libc
        for lib in ("libm", "libdl", "libc"):
            if n in libs[lib]:
                buckets[lib].append(n)
                break
        else:
            bad.append(n)

    if bad:
        print("ОШИБКА: имена, которых нет в bionic libc/libm/libdl:", file=sys.stderr)
        for n in bad:
            print("  ", n, file=sys.stderr)
        return 1

    for lib, syms in buckets.items():
        path = os.path.join(out_dir, f"fake_{lib}.c")
        with open(path, "w") as f:
            f.write("/* сгенерировано gen_fake_libs.py — НЕ править */\n")
            f.write(f"/* stub для DT_NEEDED {lib}.so; в рантайме символы */\n")
            f.write(f"/* резолвятся из bionic {lib}.so */\n")
            for n in syms:
                if n in DATA:
                    f.write(f"int {n} = 0;\n")
                else:
                    f.write(f"int {n}(void) {{ return 0; }}\n")
        print(f"{lib}: {len(syms)} символов -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""audit_bionic.py — статическая проверка .so под bionic-линкер.

Проверяет ровно то, что ломает dlopen на устройстве:
  1. все UND-символы .so экспортируются bionic (libc ∪ libm  libdl, AOSP map'ы);
  2. DT_NEEDED ⊆ {libc.so, libm.so, libdl.so} (есть в любом процессе игры).

Использование:  python3 tools/audit_bionic.py build/libmenu-br.so
Выход: 0 = ок, 1 = есть проблемы (списывается), 2 = ошибка парсинга.
"""
import re
import subprocess
import sys
import os


def dynsyms(path):
    r = subprocess.run(["readelf", "--dyn-syms", "-W", path],
                       capture_output=True, text=True)
    und, defined = set(), set()
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) < 8 or p[0].startswith("Num") or line.startswith("  ["):
            continue
        if not re.match(r"^\d+:$", p[0]):
            continue
        name = p[7].split("@")[0]
        if p[6] == "UND":
            und.add(name)
        elif p[5] == "DEFAULT" and p[6].isdigit():
            defined.add(name)
    return und, defined


def needed(path):
    r = subprocess.run(["readelf", "-d", path], capture_output=True, text=True)
    out = []
    for line in r.stdout.splitlines():
        m = re.search(r"\(NEEDED\).*\[(.*)\]", line)
        if m:
            out.append(m.group(1))
    return out


def main():
    if len(sys.argv) != 2:
        print("использование: audit_bionic.py <path-to-.so>")
        return 2
    so = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    bionic = set()
    for lib in ("libc", "libm", "libdl"):
        with open(os.path.join(here, f"bionic_{lib}_syms.txt")) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    bionic.add(line)

    und, defined = dynsyms(so)
    need = needed(so)
    fail = 0

    missing = sorted(n for n in und if n not in bionic and n not in defined)
    if missing:
        fail = 1
        print("FAIL: UND-символы, которых НЕТ в bionic libc/libm/libdl (dlopen упадёт):")
        for n in missing:
            print("   ", n)
    else:
        print(f"OK: все {len(und)} UND-символов экспортируются bionic (libc/libm/libdl)")

    bad_need = [n for n in need if n not in ("libc.so", "libm.so", "libdl.so")]
    if bad_need:
        fail = 1
        print("FAIL: NEEDED вне {libc.so, libm.so, libdl.so}:")
        for n in bad_need:
            print("   ", n)
    else:
        print("OK: NEEDED = " + (", ".join(need) if need else "(нет)"))

    return fail


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""gen_fake_libc.py — генерит C-файл-заглушку с определениями всех UND-символов .so.

Заглушка линкуется в финальный .so (soname=libc.so): lld добавляет
DT_NEEDED "libc.so" (символы реально используются), а в рантайме
bionic резолвит импорты из своего libc.so (он всегда в namespace приложения).
Аргумент: файл с именами символов (по одному на строку). Вывод: C-код.
"""
import sys

# данные, а не функции (FILE* и подобие)
DATA = {"stdout", "stderr", "stdin", "environ"}

def main():
    names = []
    seen = set()
    with open(sys.argv[1]) as f:
        for line in f:
            n = line.strip().split('@')[0]  # без версионных суффиксов
            if n and n not in seen:
                seen.add(n)
                names.append(n)
    print("/* сгенерировано gen_fake_libc.py — НЕ править */")
    print("/* stub-определения: только чтобы линкер поставил NEEDED libc.so; */")
    print("/* в рантайме символы резолвятся из bionic libc.so */")
    for n in names:
        if n in DATA:
            print(f"int {n} = 0;")
        else:
            print(f"int {n}(void) {{ return 0; }}")

if __name__ == "__main__":
    main()

# Menu-BR

Инжектируемый ImGui-меню (`.so`) для **Black Russia**
(`libblackrussia-client.so`, Android ARM64).

**База (текущая, 3-й виток): проект `br_imgui_inspector_16.111.19050`**
(полностью автономный Dear ImGui 1.90.9, Standalone OpenGL ES 3.0)
скопирован в `inspector/` без изменений + добавлен модуль функций мода
(`inspector/src/mod_features.*`) — вкладка **«0. MOD (действия)»**:
телепорт, лечилка, посадка в машину, турбо.

| Сборка | build-id | Статус |
|---|---|---|
| **16.111.19050** | `2b301eb3b52fe709056c59e22e214775027674dd` | активная база (inspector-проект) |
| 16.110.18323 / 16.106.17587 | — | legacy-вариант в `src/` (см. `./build.sh legacy-zig`), поддержка отложена |

Инспектор настраивается по build-id: точное совпадение — полный режим,
отличие — диагностический режим (меню работает, офсеты валидируются).

---

## Возможности

Вкладки инспектора (база — проект `br_imgui_inspector_16.111.19050`):

| Вкладка | Что показывает |
|---|---|
| **0. MOD (действия)** | **Наши функции мода**: лечилка (HP/Armor=100), телепорт по X/Y/Z (или «тек. позиция»), турбо (множитель 1.5–8x, скорость фиксируется при включении), список машин VehiclePool (2000) с кнопками **«Въехать»**/«Телепорт», список игроков PlayerPool с «Телепорт». |
| 1. Модуль и ImGui | Base модуля, Build-ID (OK/отличается), версии встроенного/собственного ImGui, хуки. |
| 2. LocalPlayer и CPed | Полная разкладка `CLocalPlayer`/`CPed` (id, ник, ped/veh указатели, HP-like, позиция, кэш пакетов 30/33). |
| 3. PlayerPool (1504) | Все слоты игроков: id, ник, состояние, машина, aux-поля; поиск и фильтры. |
| 4. VehiclePool (2000) | Все машины: net/game указатели, id, класс, позиция, скорость, backref; фильтры. |
| 5. Сеть: Пакеты и RPC | **Живой net-монитор** (пассивные хуки `RakClient Send`/`RPC`): ping/MTU/статус, последние события с hex, справочник raw-пакетов (27–39) и RPC (314–441). |
| 6. Память и Карта VA | Read-only hex-инспектор памяти + карта VA модуля. |

Навигация: pill-бейдж (плавающая кнопка) — открыть/закрыть меню, мини-HUD;
ввод — касания через хук `JNILib_multiTouchEvent` (тап/перетаскивание;
клики по окнам UI не проходят в игру, UP всегда проходит).

Все действия мода пишут только в игровые структуры (`CPed`/`CVehicle`/
`CLocalPlayer`) через `process_vm_writev` (ядро отвечает EFAULT вместо
SIGSEGV при невалидной странице); все чтения — read-only.

## Управление

- Ввод — через хук `JNILib_multiTouchEvent` (`0x01902270`): тап/движение
  первого пальца = мышь; клики по окнам меню не проходят в игру (capture),
  UP/CANCEL всегда проходят (не залипает управление).
- **Pill-бейдж** (перетаскивается) — открыть/закрыть меню, вкл/выкл мини-HUD.
- Slider «Опрос (мс)» — интервал обновления снимков пулов (по умолчанию 150).

## Как устроено

1. **Загрузка и self-start.** `dlopen("libmenu-br.so")` →
   `__attribute__((constructor))` в `inspector/src/main_entry.cpp` стартует
   фоновый тред (или `br_inspector_init()`/`MenuBR_Init()` вручную —
   идемпотентно). Тред ждёт появления `libblackrussia-client.so`
   (`dl_iterate_phdr`, 150×100 мс), читает build-id из `.note.gnu.build-id`.
2. **Хуки** (`inspector/src/internal_imgui_bridge.cpp`, ARM64 inline-hook с
   релокацией ADRP/ADR/B/BL и RWX-трамплином, `arm64_hook.cpp`):
   - `JNILib_step` (`0x01901D44`, dlsym → fallback VA) — точка кадра:
     оригинал → рендер собственного ImGui;
   - `eglSwapBuffers` (libEGL, резервный триггер) — рендер перед презентацией;
   - `JNILib_multiTouchEvent` (`0x01902270`) — ввод + capture-стейт-машина;
   - пассивные хуки `RakClient Send`/`RPC` для net-монитора (вкладка Сеть).
   **Если inline-хук не встаёт** (например, SELinux запрещает RWX-память):
   - трамплин создаётся как **file-backed** маппинг exec-сегмента самого
     `.so` игры (RW → запись → RX) вместо анонимного `mmap`;
   - рендер-триггер падает на **GOT/PLT-хук `eglSwapBuffers`**
     (`inspector/src/got_hook.cpp`): в слот GOT игры записывается адрес
     детура, оригинал — прежнее значение слота. RWX не нужен вовсе
     (страница `.got` уже RW). Все отказы хуков логируются с errno
     (теги `BRHook`/`BRGOT`) — смотри `brmenu.log`.
3. **Рендер** (Dear ImGui **1.90.9** + официальный `imgui_impl_opengl3`,
   ES3, **собственный изолированный ImGui-контекст** — без зависимости от
   встроенного в игру GImGui): шрифт — системный Roboto/Noto с кириллицей
   (`OversampleH=2`), размер от `eglQuerySurface` (fallback — BSS-глобал
   `0x03C0FAD8`), save/restore FBO + color/depth writemask вокруг отрисовки,
   принудительный `glBindFramebuffer(0)`, reinit GL-ресурсов при смене
   EGL-контекста, reentrancy-guard.
4. **Чтение** (`inspector/src/game_reader.cpp` + `safe_memory.cpp`):
   `process_vm_readv` (ядро — EFAULT вместо SIGSEGV), снимок мира каждые
   N мс (локальный игрок, пулы 1504/2000, RakClient-статус при валидном
   vtable `0x02140BF0`).
5. **Функции мода** (`inspector/src/mod_features.cpp`, наша надстройка):
   - **запись** — `process_vm_writev` с той же проверкой адресов
     (Top-Byte-Ignore, диапазон user-памяти);
   - **телепорт** — позиция машины (`LocalPlayer+0x40`), если игрок за
     рулём, иначе `CPed+0x38..0x40`;
   - **лечилка** — `CPed+0x4B0/+0x4B8 = 100`;
   - **посадка** — `ped+0x540=veh`, `LocalPlayer+0x40=veh`, флаг
     `ped+0x548=1`, backref `veh+0xB8=ped`, подстановка пед к машине;
   - **турбо** — per-frame тик из рендер-колбэка: `CVehicle+0x180`
     (vel) масштабируется к `base_speed × mult` в текущем направлении.
6. **Лог.** Strong-символ `__android_log_print`
   (`inspector/src/android_log_shim.cpp`) — пишет
   `/sdcard/Android/data/com.br.top/files/brmenu.log` (вращение 1 МБ →
   `.log.old`) и best-effort форвардит в logcat через `dlopen("liblog.so")`.
   Поэтому NEEDED `liblog.so` не нужен.
7. **C++ runtime.** `-fno-exceptions -fno-rtti`; `operator new/delete`,
   `__cxa_atexit` — `src/rt_shim.c`; out-of-line std::thread/mutex/chrono/
   string-методы и `__cxa_guard_*` — `inspector/src/rt_compat.cpp`
   (над pthread), sized delete — `inspector/src/rt_compat_c.c`.

## Сборка

### 1) Zig (кросс-компиляция без NDK — как в этом репо)

```sh
ZIG=/path/to/zig ./build.sh zig          # → build/libmenu-br.so (aarch64)
ZIG=/path/to/zig ./build.sh legacy-zig   # → build/libmenu-br-legacy.so (старый src/)
```

Зиг 0.15.2 (pip `ziglang`). Компиляция — glibc-хедеры + shim-хедеры
Android API (`tools/shim_include`: `android/log.h`, `jni.h`, `EGL/egl.h`,
`GLES3/gl3.h`, `link.h`, `xlocale.h`, `android/api-level.h`), критично
`-D__ANDROID__` (ветвление кода инспектора) и `-DIMGUI_IMPL_OPENGL_ES3`.
Двухпроходная линковка: pass1 собирает UND-список, `tools/gen_fake_libs.py`
классифицирует его по таблицам экспортов bionic (libc/libm/libdl) +
OpenGL ES 3.0/EGL, pass2 линкует stub'и → **NEEDED = {libc.so, libm.so,
libdl.so, libGLESv3.so, libEGL.so}** (всё есть в namespace процесса игры).

### 2) Android NDK (альтернатива)

```sh
export NDK=$HOME/Android/Sdk/ndk/28.2.13004772
cd inspector
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release
# + добавить в CMakeLists: src/mod_features.cpp, src/android_log_shim.cpp,
#   src/entry_compat.cpp (и rt_shim для operator new, если без libc++)
cmake --build build
```

### 3) Хост-тест (x86_64, без телефона)

```sh
make test   # ELF/PLT/сигнатуры на reference/libblackrussia-client.so (16/16)
GAME_SO=/другой/путь/libblackrussia-client.so make test   # другая сборка
python3 tools/audit_bionic.py build/libmenu-br.so  # аудит NEEDED/UND
```

## Использование на устройстве

```c
// в любой точке процесса игры
dlopen("/sdcard/brmenu/libmenu-br.so", RTLD_NOW);
int (*init)(void) = dlsym(h, "MenuBR_Init");
init();
```

Или любой загрузщик (smali-хук): self-start срабатывает сам —
`__attribute__((constructor))` в `main_entry.cpp` стартует фоновый тред при
`dlopen`, `MenuBR_Init()` нужен только для ручной/повторной инициализации.

Остановка: `MenuBR_Shutdown()` — снимает хуки (`Arm64Hook::RemoveAll`),
ставит флаг остановки фоновых тредам.

## Структура

```
inspector/                  БАЗА: проект br_imgui_inspector_16.111.19050 (без изменений)
  include/
    br_offsets_16_111_19050.hpp  все VA/офсеты 16.111 (build-id 2b301eb3…)
    arm64_hook.hpp           ARM64 inline-hook API
    game_reader.hpp          read-only снимки (LocalPlayer/Remote/Vehicle/RakNet)
    internal_imgui_bridge.hpp свой ImGui-контекст + хуки кадра/тача
    network_catalog.hpp      каталог пакетов/RPC + пассивный observer
    inspector_menu.hpp       7 вкладок UI
    safe_memory.hpp          SafeMemory (process_vm_readv)
  src/
    main_entry.cpp           self-start (constructor), br_inspector_* C-API
    internal_imgui_bridge.cpp  AttachStandaloneRenderer, хуки, touch-очередь
    game_reader.cpp          CaptureSnapshot
    inspector_menu.cpp       рендер вкладок
    network_catalog.cpp      каталог + observer Send/RPC
    arm64_hook.cpp           трэмплины с релокацией
    safe_memory.cpp          process_vm_readv
    mod_features.cpp         НАШЕ: телепорт/лечилка/посадка/турбо (+SafeWrite)
    android_log_shim.cpp     НАШЕ: __android_log_print → brmenu.log + logcat
    entry_compat.cpp         НАШЕ: MenuBR_* обёртки (smali-совместимость)
    rt_compat.cpp            НАШЕ: std::thread/mutex/chrono/string/cxa_guard
    rt_compat_c.c            НАШЕ: sized operator delete (hidden)
  third_party/imgui/         Dear ImGui v1.90.9 + impl_opengl3 (их копия)
src/                        LEGACY (2-й виток): свой imgui 1.91.9b + PLT-хуки
                            (./build.sh legacy-zig; история git — полный путь)
tools/shim_include/         shim-хедеры Android API для zig (log/jni/EGL/GL/link)
tools/gen_fake_libs.py      классификация UND → bionic/GLESv3/EGL stub'и
tools/audit_bionic.py       аудит NEEDED/UND под bionic-линкер
tools/host_test.cpp         хост-тест ELF/PLT/сигнатур
build.sh                    zig-сборка (zig | legacy-zig)
reference/                  бинарник 16.110.18323 + RE-документация
```

## Адреса (VA внутри .so; base + VA = адрес в процессе)

### 16.111.19050 — из RE-анализа (build-id guard + пробы в рантайме)

| Что | VA / офсет |
|---|---|
| `g_pPlayerPool` (local `+0x00`, remote[1504] `+0x08+id*8`) | `0x036130B0` |
| `g_pVehiclePool` (2000 × 8B, CNetVehicle) | `0x036130B8` |
| `g_pObjectPool` (4096 × 8B) | `0x036130D0` |
| `g_pRakClient` (RakClientInterface) | `0x036130A0` |
| `GetLocal` / `GetRemote(id)` | `0x017B179C` / `0x017B17A4` |
| `GetGameVeh(slot)` | `0x017DE01C` |
| `JNILib_step` (кадр GLThread) | `0x01901D44` |
| `JNILib_multiTouchEvent` (ввод) | `0x01902270` |
| `RakClient Send` thunk / `RPC` thunk | `0x01A513B8` / `0x01A51AC8` |
| CLocalPlayer: id / ник / `CPed*` / `CVehicle*` | `+0x00` / `+0x02` / `+0x38` / `+0x40` |
| CRemotePlayer: `CPed*` / ник / id / state | `+0x10` / `+0x18` / `+0x4A` / `+0x120` |
| CPed: pos / HP / Armor / `CVehicle*` / флаг в авто | `+0x38..0x40` / `+0x4B0` / `+0x4B8` / `+0x540` / `+0x548` |
| CNetVehicle: `gameVeh*` / substate | `+0x00` / `+0x08` |
| CVehicle: pos / vel[3] / backref-пед / класс / id | `+0x38` / `+0x180` / `+0xB8` / `+0x430` / `+0x434` |
| RakClient vtable: ping avg / IsConnected / MTU | слоты `+0x68` / `+0x90` / `+0xF0` (vtable `0x02140BF0`) |
| BitStream: bitsUsed / data | `+0x00` / `+0x10` |

Сеть (16.111): raw-пакеты 27, 29, 30, 31, 32, 33, 34, 35, 37, 38, 39
(30 — player-state 73B, 34 — InCar vehicle-sync 66B); RPC 314, 320, 324, 342,
355, 356, 357, 361, 371, 374, 378, 403, 413, 414, 423, 436, 437, 441.

### 16.110.18323 — сверено с бинарником

| Что | VA |
|---|---|
| `VehiclePool*` (2000 слотов × 8B) | `0x37FBEC8` |
| `PlayerManager*` (слоты `mgr+8+id*8`, id ≤ 1503) | `0x37FBEC0` |
| таблица педов (шаг `0xF8`, поз `+0x38/+0x3C/+0x40`) | `0x35C7A28` |
| таблица цветов (16B/игрок, RPC 401) | `0x37FC174` |
| `GetByNetId(u16)` | `0x1A1C1C8` |
| `GetPlayerById(id)` | `0x19F0E48` |
| диспетчер синхро-пакетов `f(w0,w1)` | `0x19D4664` |
| глобал `packetId` (у 16.110 — для монитора) | `0x49C698` |
| игрок: `CVehicle*` / флаг «в машине» / состояние (19=в машине) / u16 id машины | `+0x540` / `+0x548` / `+0x118` / `+0x150` |
| машина: пассажир / класс | `+0xB8` / `+0x430` |
| пед: u16 id машины | `+0x54A` |
| PLT: `glDrawElements` / `glClear` / `glDrawArrays` (стаб / GOT) | `0x22EA7E0/0x2415D28` / `0x22EA8A0/0x2415D58` / `0x22EA9A0/0x2415D98` |

Сигнатуры: `GetByNetId` — `28 3c 00 12 1f 3d 1f 71` (8B); диспетчер —
36-байтный пролог (единственный в `.text`), обе в `src/off.cpp`.

### 16.106.17587 — из RE-документации

Глобалы: pool `0x37809C8`, mgr `0x37809C0`, педы `0x354C5D8`, цвета
`0x3780C74`. Функции: `GetByNetId 0x19D2FDC`, `GetPlayerById 0x19AADA4`,
диспетчер `0x1990A78` (арг-регистры не подтверждены — монитор для 16.106
недоступен), `CreateVehicle 0x1B815A0`, метод двигателя `0x1B7F444`
(гипотеза). Поля игрока: `CVehicle* +0x148`.

## Ограничения и риски

- **16.111**: спавн машин и toggle двигателя недоступны (методы не
  установлены — UI их скрывает). Бинарника 16.111 в репо нет: таблица
  подтверждена device-инспектором + RE-анализом, в рантайме проверяется
  build-id `2b301eb3…` и структурные пробы (валидность пулов).
- **Двигатель / спавн** работают только в 16.106 (в 16.110/16.111 методы не
  выделены). Спавн в 16.106 — экспериментальный.
- **Телепорт/посадка/лечение** пишут значения напрямую — без отправки
  синхро-пакетов, сервер может откатить (особенно посадку).
- **Монитор 16.106** недоступен (глобал packetId не выделен в таблице).
- **16.111**: function-хуки требуют «простой» 16-байтовый пролог в
  `JNILib_step`/`multiTouchEvent` (без B.cond/CBZ/CBNZ/TBZ/LDR-literal) —
  при обновлении игры с изменением пролога хук откажется вставаться (лог).
- **16.110/16.106**: хук — PLT-стаб `glDrawElements/glClear/glDrawArrays`;
  если игра перестанет вызывать их из своего .so, оверлей не появится
  (Rescan помогает).
- При обновлении игры до новой сборки — добавьте её в `src/off.cpp`
  (build-id + адреса) или используйте Custom; автоопределение
  переключится само по build-id.
- Библиотека читает чужую память в рантайме — используйте на свой риск
  (нарушение ToS игры, риск бана аккаунта).

## Лицензия

Dear ImGui — MIT (`third_party/imgui/LICENSE`). Остальной код —
приватный, создан для исследовательских целей.

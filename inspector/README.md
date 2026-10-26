# BLACK RUSSIA 16.111.19050 — Android NDK ImGui Inspector (arm64-v8a)

Готовый C++17 модуль под **Android NDK (`arm64-v8a`)**, реализующий полнофункциональное **Read-Only диагностическое меню-инспектор** на базе встроенного в `libblackrussia-client.so` контекста **Dear ImGui 1.90.9 (`19090`)** и материалов статического анализа сборки `16.111.19050` (patch 1580).

---

## 1. Ключевые результаты реверс-инжиниринга `libblackrussia-client.so` (16.111.19050)

При анализе самого бинаря `libblackrussia-client.so` (`SHA-256: 44e5b7f5e22a2fa7fb16268bf043d25f1feeb1d600dd6b56feb3b43dd236f1fd`, `GNU Build-ID: 2b301eb3b52fe709056c59e22e214775027674dd`) дополнительно к таблицам пакета анализа установлены точные адреса встроенного рендера ImGui и глобальных пулов:

### Автономный (Standalone) Dear ImGui 1.90.9 + OpenGL ES 3.0
Меню использует **полностью собственный изолированный контекст `ImGui::CreateContext()`** и собственный рендер-бэкенд **`ImGui_ImplOpenGL3` (`#version 300 es`)**, вообще не завися от клиентского `GImGui` (который в релизной сборке игры может оставаться неактивным):
- **Собственный рендер поверх `FBO 0`**: перехватывается `Java_com_blackhub_bronline_game_core_JNILib_step` (`dlsym` / `VA 0x01901D44`, резервный вариант — `eglSwapBuffers` из `libEGL.so`) прямо на `GLThread` с полным сохранением и восстановлением состояния OpenGL ES 3.0 (`FBO`, `Viewport`, `Scissor`, `Program`, `VAO`, `VBO`, `Blend`, `ColorMask`, `DepthMask`) и автоматическим пересозданием ресурсов при смене `EGLContext`.
- **Собственный кириллический шрифт**: загружается системный шрифт Android (`/system/fonts/Roboto-Medium.ttf` / `Roboto-Regular.ttf` / `NotoSans-Regular.ttf`) с полным диапазоном `GetGlyphRangesCyrillic()`.
- **Собственный обработчик касаний**: перехватывается `Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent` (`dlsym` / `VA 0x01902270`), передаёт события в `ImGuiIO` (`ImGuiMouseSource_TouchScreen`) и блокирует прокликивание игры сквозь открытое окно меню.
- **Корректная релокация `ADRP` в прологах**: встроенный `Arm64Hook` декодирует и релоцирует `ADRP`/`ADR`/`B`/`BL` в 64-битную загрузку `MOVZ`/`MOVK` в трамплине.

### Карта проверенных статических VA (`runtime_addr = module_base + VA`)

| Подсистема / Символ | Статический VA | Назначение и доказательство |
|---|---:|---|
| `.note.gnu.build-id` (desc) | `0x000002E0` | 20 байт `2b301eb3b52fe709056c59e22e214775027674dd` для мгновенной проверки версии |
| `GImGui` (`ImGuiContext*`) | `0x03C9D9D0` | Глобальный указатель контекста встроенного Dear ImGui 1.90.9 (`0x62B8` Б) |
| `GImAllocatorAlloc / Free` | `0x0229C350` / `58` | Указатели аллокаторов ImGui (`-> 0x019D73AC` / `0x019D73B0`) |
| `g_ImGuiFontScale` | `0x0363D528` | Масштабированный размер шрифта клиента (`float`) |
| `g_ImGuiLoadedFonts[6]` | `0x0363D530` | 6 предзагруженных шрифтов игры (`ImFont*[6]`) с кириллицей |
| `g_bImGuiFrameActive` | `0x0363D560` | Флаг активного кадра между `BeginFrame` и `EndFrameAndRender` |
| `ImGuiWrapper::BeginFrame` | `0x01885450` | Вызывает `ImGui_ImplOpenGL3_NewFrame` (`0x18E0E18`) + `ImGui::NewFrame` (`0x19C6CF4`) |
| `ImGuiWrapper::EndFrameAndRender` | `0x01885490` | Вызывает `EndFrame` (`0x19D06C8`) + `Render` (`0x19D15C4`) + `RenderDrawData` (`0x18E0E84`) |
| `UpdateTouchInput` | `0x00E77D88` | Передача координат и нажатий тачскрина в `ImGuiIO` на RenderThread |
| `g_pRakClient` | `0x036130A0` | `RakClientInterface*` (`vtable = 0x02140BF0`, `Send = +0x38`, `RPC = +0xB0`) |
| `g_pPlayerPool` | `0x036130B0` | `CPlayerPool*` (`+0x00` `CLocalPlayer*`, `+0x08 + id*8` `CRemotePlayer*[1504]`) |
| `g_pVehiclePool` | `0x036130B8` | `CVehiclePool*` (`2000` слотов `CNetVehicle*` по 8 байт) |
| `g_pObjectPool` | `0x036130D0` | Пул объектов (`4096` слотов по 8 байт, цикл в `0x017DC978`) |

---

## 2. Адаптивный интерфейс и 7 разделов меню (v2 UI, 100% Read-Only)

- **Адаптивная навигация и 4 темы**: автоматическое масштабирование под разрешение экрана + `Safe Area` отступы под вырезы камер Android, боковая панель с векторными `ImDrawList`-иконками на широком окне и авто-переключение в компактную верхнюю панель на узких окнах (`Split Left` / `Split Right` / смартфоны). 4 темы оформления: **Cyber Obsidian**, **Crimson Carbon**, **Emerald Matrix**, **Daylight Frost** + кинетическая прокрутка списков пальцем (`Touch Drag-Scroll`).
- **Плавающий виджет (`BR 16.111`) и Мини-HUD**: магнитное удержание в границах экрана, живые счётчики FPS/Ping/пулов, курс компаса, скорость `км/ч` и переключатель `LIVE / ПАУЗА`.
- **Раздел 1 — Обзор и Живые Графики**: 4 карточки с плавными спарклайн-графиками (`FPS`, `RakNet Ping`, `Скорость км/ч`, `Пакеты/с`), шкалы заполнения пулов (`1504` / `2000` / `4096`) и переходы к живым структурам в 1 клик.
- **Раздел 2 — LocalPlayer, CPed и Матрица**: разбор `CLocalPlayer`, векторов ориентации `CMatrix (+0x48)` и курса в градусах, скорости `м/с` и `км/ч`, одометра сессии, прогресс-баров `+0x4B0`/`+0x4B8`, подсветки изменившихся полей кэша пакета 33 (`+0xF0..+0x10B`) и карточки текущего `CVehicle*`.
- **Раздел 3 — PlayerPool (1504)**: закрепление избранных игроков звездой `★` наверху таблицы, сортировка по клику на столбцы (`ID`, `Никнейм`, `Дистанция`, `+4B0`), слайдер радиуса и адаптивная карточка `CRemotePlayer` с расчётом `ΔXYZ`.
- **Раздел 4 — VehiclePool (2000)**: быстрые чип-фильтры (`Все`, `< 150 м`, `В движении`, `Подтип 11`, `С CVehicle*`), сортировка по скорости/дистанции/ID и подробный инспектор `CNetVehicle` + `CVehicle` (`MoveSpeed`, `TurnSpeed`, `BackRef +0xB8`, `+0x790`).
- **Раздел 5 — RakNet, BitStream и Byte-Diff**: статус/пинг/MTU, счётчик `пкт/с` и `RPC/с`, справочники 11 raw-пакетов и 26 RPC с переходом к коду сериализаторов в дизассемблер (`ASM`), плюс Live-монитор `Send`/`RPC` с **подсветкой изменившихся байт (`Byte-Diff`)** и разбором полей пакетов `27, 30, 33, 34, 35`.
- **Раздел 6 — Лаборатория Памяти (5 режимов)**:
  1. **Hex + Типы** (`process_vm_readv` без риска `SIGSEGV`, история переходов `<- Назад`, разыменование указателей в 1 тап);
  2. **ARM64 Дизассемблер (`.text`)** — встроенный декодер инструкций AArch64 с кнопкой перехода по ветвлениям `B` / `BL`;
  3. **Живой Watchlist** — закладки любых адресов (`u8/u16/i32/u32/float/ptr64/vec3/str`) с подсветкой изменений;
  4. **Read-Only Сканер диапазона** — поиск Hex-сигнатур с маской `??` (`AA BB ?? DD`), чисел `int32`/`float` и `ASCII`-строк;
  5. **Справочник VA `16.111.19050`** с открытием как в `Hex`, так и сразу в `ASM`.
- **Раздел 7 — Темы, Адаптация и Экспорт**: пресеты окна (`Стандарт`, `Компакт`, `Весь экран`, `Сплит влево/вправо`), настройка масштаба/прозрачности/`Safe Area`, диагностика мультитача и экспорт отчётов на Android в **`.JSON`**, **`.CSV`** и **`.TXT`**.

### Подключение в `JNILib.smali` (`<clinit>`)
Поместите `libbr_inspector_16111.so` в `lib/arm64-v8a/libbr_inspector_16111.so` и добавьте вызов `loadLibrary` перед `return-void` в `com/blackhub/bronline/game/core/JNILib.smali`:
```smali
    const-string/jumbo v0, "br_inspector_16111"
    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
```

---

## 3. Сборка под Android (`arm64-v8a`)

### Вариант A: CMake + Android NDK (рекомендуется)
```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26d
./build_android.sh
```
Собранная библиотека появится по пути:
`build-android-arm64/libbr_inspector_16111.so`

### Вариант B: `ndk-build`
```bash
$ANDROID_NDK_HOME/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk
```

### Подключение внешнего хук-фреймворка (опционально)
По умолчанию модуль содержит собственный AArch64 трамплин-движок `br::hook::Arm64Hook` с поддержкой релокации `ADRP`. Если в вашем проекте уже используется `Dobby` или `ShadowHook`, можно передать его до вызова `br_inspector_init()`:
```cpp
extern "C" void br_inspector_set_hook_backend(bool (*fn)(void* target, void* detour, void** orig));
extern "C" void br_inspector_init();

bool MyDobbyAdapter(void* target, void* detour, void** orig) {
    return DobbyHook(target, reinterpret_cast<dobby_dummy_func_t>(detour),
                     reinterpret_cast<dobby_dummy_func_t*>(orig)) == 0;
}
```

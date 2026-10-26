# Дополнение к статическому анализу `libblackrussia-client.so` (`16.111.19050`)

## 1. Идентификаторы бинаря
- **Файл**: `libblackrussia-client.so` (`36 296 864` байт)
- **SHA-256**: `44e5b7f5e22a2fa7fb16268bf043d25f1feeb1d600dd6b56feb3b43dd236f1fd`
- **GNU Build-ID (`VA 0x000002E0`, 20 байт)**: `2b301eb3b52fe709056c59e22e214775027674dd`
- **Тулчейн сборки**: `Android NDK r28c` (`Clang 19.0.1` / `LLD 19.0.1`, `API 26`, `PT_LOAD Align = 0x4000`, `PT_LOAD[0].p_vaddr = 0x0`)

## 2. Точки входа JNI рендера и мультитача (`com.blackhub.bronline.game.core.JNILib`)
| Символ | Статический VA | Размер | Назначение |
|---|---:|---:|---|
| `Java_com_blackhub_bronline_game_core_JNILib_resize` | `0x01901D18` | `16 Б` | Записывает `{width, height}` (`int32[2]`) в `.bss` по `VA 0x03C0FAD8` |
| `Java_com_blackhub_bronline_game_core_JNILib_step` | `0x01901D44` | `292 Б` | Кадр рендера `GLSurfaceView.onDrawFrame` на `GLThread` (`-> 0x0171DF30 -> 0x01725CA4`) |
| `Java_com_blackhub_bronline_game_core_JNILib_multiTouchEvent` | `0x01902270` | `176 Б` | Мультитач `(env, thiz, action, count, x1, y1, x2, y2, x3, y3)` |
| `g_SurfaceSizeWH` (`.bss`) | `0x03C0FAD8` | `8 Б` | Текущий размер поверхности `{int32 width, int32 height}` |
| `g_RenderThreadTouchArray` | `0x022968F0` | `16 Б * N` | Буфер касаний `{int32 x, int32 y, int32 state, int32 pad}` |

## 3. Глобальные указатели пулов и сети (`.bss`)
| Глобальная переменная | Статический VA | Описание |
|---|---:|---|
| `g_pRakClient` | `0x036130A0` | `RakClientInterface*` (`vtable = 0x02140BF0`, `Send = +0x38` -> `0x01A513B8`, `RPC = +0xB0` -> `0x01A51AC8`) |
| `g_pPlayerPool` | `0x036130B0` | `CPlayerPool*` (`+0x00` `CLocalPlayer*`, `+0x08 + id*8` `CRemotePlayer*[1504]`, `+0x2F30`/`+0x46B0` `int32[1504]`) |
| `g_pVehiclePool` | `0x036130B8` | `CVehiclePool*` (`2000` слотов `CNetVehicle*` по 8 байт) |
| `g_pAuxPool_C8` | `0x036130C8` | Вспомогательный пул клиента |
| `g_pObjectPool` | `0x036130D0` | `CObjectPool*` (`4096` слотов по 8 байт, обход в `0x017DC978`) |
| `g_pAuxPool_F8` | `0x036130F8` | Вспомогательный пул клиента |
| `g_pAuxPool_100` | `0x03613100` | Вспомогательный пул клиента |

## 4. Встроенный в клиент Dear ImGui `1.90.9` (`19090`)
- `GImGui` (`ImGuiContext*`, размер контекста `0x62B8`): `VA 0x03C9D9D0`
- `GImAllocatorAllocFunc` / `FreeFunc`: `VA 0x0229C350` (`-> 0x019D73AC`) / `VA 0x0229C358` (`-> 0x019D73B0`)
- `g_ImGuiFontScale`: `VA 0x0363D528` (`float`), `g_ImGuiLoadedFonts[6]`: `VA 0x0363D530` (`ImFont*[6]`), `g_bImGuiFrameActive`: `VA 0x0363D560`
- `ImGuiWrapper::Init`: `0x01884C04`, `BeginFrame`: `0x01885450`, `EndFrameAndRender`: `0x01885490` (в релизе вызываются условно при ненулевом указателе `0x02479668`, проверка в `0x00E77E50`).

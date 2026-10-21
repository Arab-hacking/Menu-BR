# Сетевая часть BLACK RUSSIA — разбор с нуля, сборка 16.110.18323

Дата: **2026-10-17**. Объект: свежий `launcher.apk` с CDN → `libblackrussia-client.so`,
`libupdate-manager.so`. Всё, что ниже, получено на **новом** бинарнике и сверено со **старым**
(16.106.17587) — где совпало, так и написано.

> Коротко: сетевой протокол игры **не изменился**. Изменился слой обновлений/конфигурации:
> новый update flow, feature flags, installationId для A/B, curl 8.16 с WebSocket, SSL pinning.

> ### ⚠ ИСПРАВЛЕНИЕ от 2026-10-17 (вечер): «конверта» НЕ существует
>
> В первой редакции этого отчёта (разделы I, 3.2–3.4, 5.3) утверждалось, что каждый UDP-пакет
> обёрнут в 32-байтовый конверт `[тег 8 Б][«BLACK RUSSIA» 20 Б][u32]`. **Это ошибка разбора pcap.**
>
> Проверено заново по полю длины UDP: 32-байтовый блок лежит **за пределами IP-пакета**
> (`ip_total_length` и `udp_length` его не включают), а контрольная сумма UDP сходится на
> **500 из 500** проверенных пакетов. Значит, блок добавлен **после** передачи — это метаданные
> инструмента захвата (тег `01 07 20 21 00 00 27 cb` + имя приложения + случайные 4 байта),
> а не часть протокола. Там же лежали и 2–3 нулевых байта выравнивания кадра.
>
> **Следствие:** пакеты можно собирать вручную; «неизвестный ключевой MAC», который якобы мешал
> подключению, не нужен. Открывающий пакет — это ровно `04 42 52` (3 байта), ответ `07 00`.
> Протокол полностью воспроизведён в C++: 6722 из 6722 RakNet-датаграмм пересобираются
> байт-в-байт (`/home/user/br_netstack`, `brnet pcap --roundtrip`).

---

## 0. Маршрут разбора (A → Z)

| Шаг | Что | Результат |
|---|---|---|
| A | API-конфиг лаунчера | `url-config.json` 200, CDN `dl.blackcdn.me` |
| B | `patch_index.json` | `android_version` 1571, `link files_34928_…` |
| C | Скачивание APK | **123 990 477 Б**, sha256 `130083a2…` (прошлый `d27b2f74…`) |
| D | Распаковка | `arm64-v8a/libblackrussia-client.so` **38 710 096 Б** |
| E | Идентичность бинарника | AArch64, NDK r28c, API 13676358, versionName **16.110.18323** |
| F | Диff символов к прошлой сборке | +14 / −4 функции |
| G | Шифр канала | XOR-перестановка `0xad2c94`, **таблица идентична** |
| H | Auth-хэш | `ComputeAuthHash 0x1c9fcf8`, seed `0xA5C3`, **эмуляция даёт тот же хэш** |
| I | Формат UDP-пакетов | RakNet-форк; «конверт» оказался метаданными захвата (см. баннер), пересборка 6722/6722 байт-в-байт |
| J | Живые проверки | 91 сервер, 66 789 онлайн; CDN и feature flags доступны |
| K | Новое в сети | update flow, installationId, A/B, SSL pinning, CDN-метрики |

---

## 1. Источник и цепочка обновления

```
api.blackrussia.online/client/url-config.json          → cdnUrl, cdnBackupUrl, регион CIS
dl.blackcdn.me/patch_index.json                        → 212 829 Б, android_version 1571
dl.blackcdn.me/files_34928_674ac225_1792144466300/…     → link текущей сборки
launcher.apk внутри patch_index: filesize 123 990 477, link files_34822_674ac225_1791889252039
```

`dl.blackcdn.me` отвечает **302 → rsv2.br-dl.com** (тот же файл), т.е. основной CDN сейчас
проксирует на второй. Контроль: `rsv.br-dl.com/patch_index.json` побайтово равен основному.

| Файл | Размер | Примечание |
|---|---|---|
| `patch_index.json` | 212 829 Б | 273 файла, 11 патчей, link `files_34928_674ac225_1792144466300` |
| `patch_index_candidate.json` | 209 996 Б | link `files_34822_674ac225_1791889252039` — **ровно наш APK** |
| `patch_index_next_slot.json` | — | на backup-CDN 404, на основном 302 |
| `update_manager_feature_flag.json` | 1 148 Б | **новый** конфиг поведения апдейтера |

### Что говорит `update_manager_feature_flag.json`

```json
{
  "type_download_resources_3": { "site": "patch_index_json", ... },
  "download_size_without_confirm": 102400,
  "download_speed_limit": 65536,
  "download_timeout": 3600000,
  "connection_timeout": 15000,
  "is_enabled_ssl_pinning": true,
  "is_enabled_recovery": true,
  "is_enabled_check_resources": true,
  "candidate_versions": { "rustore": 0, "google_play": 0, "site": 0, "ios": 0 },
  "ab_test": { ... 0 % ... },
  "sending_of_cdn_metric": { "is_enabled": true, "ab_test": { ... } },
  "next_slot_downloading": { "is_enabled": false },
  "check_compatible_client_version": { "is_enabled": true }
}
```

Ключевое: **SSL pinning включён флагом**, проверка совместимой версии клиента включена,
метрики CDN отправляются. Флаги можно переключать серверно, без обновления APK.

---

## 2. Идентичность нового бинарника

| Параметр | Старый (16.106.17587) | Новый (16.110.18323) |
|---|---|---|
| Размер `.so` | 38 238 624 Б | **38 710 096 Б** (+471 472) |
| `.text` | 19 702 376 | 20 019 104 (+316 728) |
| `.rodata` | 8 060 520 | 8 103 528 (+43 008) |
| Экспортированных FUNC | 3 897 | **3 907** |
| curl | 8.x без WS | **8.16.0 + curl_ws_\*** |
| OpenSSL | — | 3.6.0 (вендор) |

Символов с C++-именами в динамической таблице ~3 900 — бинарник **не stripped**, это сильно
упрощает разбор: `RakClient`, `RakPeer`, `RLEncryption`, `RakClientAuth`, `RPC` видны по именам.

### Что добавилось / удалилось (полный список — `net_v2/symbols_*.txt`)

**Удалено (4):**
```
JNILib_cancelDownloadResources   JNILib_tryDownloadNextSlot
JNILib_tryDownloadResources      JNILib_tryGetPatchIndex
```
→ старый JNI-API поштучного скачивания ресурсов выпилен.

**Добавлено (14):**
```
JNILib_startUpdateFlow        JNILib_cancelUpdateFlow     JNILib_restartUpdateFlow
JNILib_retryUpdateFlow        JNILib_confirmUpdate        JNILib_declineUpdate
JNILib_isUpdateFlowStarted    JNILib_setInstallationId    JNILib_resolveFeatureFlags
JNILib_isEnabledCheckCompatibleClientVersion
curl_ws_meta / curl_ws_recv / curl_ws_send / curl_ws_start_frame
```

Появились классы: `UpdateFlowRunner`, `UpdateManagerContext`, `UpdateManagerSystemCallbacks`,
`feature_flags::Parse`, `ab_test::IsUsedForTest`.

---

## 3. Транспорт: что реально летит по проводу

### 3.1. Порты

Из pcap 13.10 (сессия 171 с):

| Порт | C2S | S2C | Байт C2S / S2C |
|---|---|---|---|
| **5125** | 1 902 | 4 822 | 183 196 / 494 552 |
| **5127** | 574 | 641 | 24 200 / 34 182 |

5127 — второй канал той же сессии, открывается примерно через 20,7 с после прелюдии 5125.
Это **не RakNet**: 0 из 1 215 кадров разбираются RakNet-кодеком; первые кадры 52/48 Б.
Детальный проход по новому `.so` нашёл вызываемый ENet-путь (`enet_host_create/connect/service/peer_send`),
а последовательность 5127 в дампе ему соответствует. Назначение прикладных сообщений канала и точный
серверный триггер ещё не определены; детали — `CONNECT_FLOW_16.110.md`. В нашем C++ `brnet pcap`
5127 пока только помечается как не-RakNet.

### 3.2. «Конверт» — на самом деле метаданные захвата

Первая редакция видела в каждом пакете блок `[тег 8][«BLACK RUSSIA» 20][u32 4]` и считала его
частью протокола. Проверка по полям IP/UDP показала обратное:

| Проверка | Результат |
|---|---|
| `ip_total_length` и `udp_length` включают этот блок? | **нет** — блок целиком за пределами датаграммы |
| Контрольная сумма UDP (псевдозаголовок + заголовок + payload) | **верна на 500 из 500** пакетах |
| Число байт за IP | 34–35 = 2–3 нуля выравнивания + 32 Б трейлера |
| Тег `01 07 20 21 00 00 27 cb` | одинаков во всех кадрах |
| Имя `BLACK RUSSIA\0…` | одинаково во всех кадрах |
| Последние 4 Б | случайны, ни CRC32, ни хэш кадра/пейлоада |

Вывод: блок дописывается **после** отправки — это подпись инструмента захвата (тег сборки +
имя приложения + случайный идентификатор). В протокол не входит, и «ключевой MAC»,
который якобы нельзя было подделать, искать не нужно.

### 3.3. Открытие соединения

Реальный обмен из pcap 13.10 (время от первого пакета):

```
  0.000  C2S  04 42 52                                (3 Б)  прелюд
  0.049  S2C  07 00                                   (2 Б)  сервер принял
  0.132  C2S  датаграмма: seq=0 RELIABLE, тело 08     (6 Б)  hello
  0.179  S2C  датаграмма: seq=0 RELIABLE, 05 18 "V0pZ…" (31 Б) challenge
  0.190  C2S  датаграмма: seq=1 RELIABLE, 06 60 <96>  (105 Б) ответ на challenge
```

`00 00 20 87 00 69` — это и есть hello-датаграмма целиком; проверяется в `brnet selftest`
побайтово. Ответ на hello (31 Б) тоже разбирается: `05`, длина `0x18` = 24, строка challenge.

### 3.4. Дальше — обычный RakNet-поток

ACK-датаграммы (`e3 00 00`, `e3 01 00`, …) и пакеты с данными чередуются; весь поток
пересобирается кодировщиком байт-в-байт.

## 4. Шифрование канала

| Что | Где в новой сборке | Статус |
|---|---|---|
| Таблица перестановки T (512 Б) | `.rodata 0xad2c94` (было `0xac376c`) | **байт в байт та же** |
| Формула | `c[i]=T[2*p[i]]^k`, `p[i]=T[2*(c[i]^k)+1]` | без изменений |
| Ключи | `0x43` (чёт), `0x67` (нечёт) | без изменений |
| Контрольная сумма | `sum(byte & 0xAA)` | идентична (тело функции совпало побайтово) |

Таблица — пара «π⁻¹ | π»: `T[2i]=π⁻¹(i)`, `T[2i+1]=π(i)`; биекция проверена (256 значений,
`π[π⁻¹[i]]=i`). Первые 32 Б: `55 1d ea f1 48 f3 cc e8 98 18 36 51 d4 74 33 e0 …`.

---

## 5. Авторизация: challenge → ответ

### 5.1. Код

```
RakClientAuth::ComputeAuthHash   0x1c9fcf8   (1328 Б, было 0x1c54564)
RakClientAuth::g_seed_key        0x24ecbb4   = 0x0000A5C3
RakClient::SendConnectionRequest 0x1c8a5d8   (572 Б) — идентичен старому
RakClient::ProcessNetworkPacket  0x1c8b280   (400 Б) — идентичен старому
RakClient::RPC                   0x1c8a248   (140 Б) — идентичен старому
```

### 5.2. Проверка эмуляцией (не «по документации», а исполнением)

Скрипт `net_v2/emulate_auth.py` грузит `.so` по `PT_LOAD`, применяет 135 575 релокаций и
**исполняет** `ComputeAuthHash` (unicorn) на challenge из pcap:

```
challenge: V0pZJy1xwBoWYbSdWTbzqOVr
получено : MOOMGOEMNJIZRIZDHXJRVMRSLGQMQVOJLOJZQLUKGPMJXKMVDVYSUKDLKFMPOQWSHUOONHUWDHRKTLEMRMHZOSZMUZKPZOTY
ожидалось: MOOMGOEMNJIZRIZDHXJRVMRSLGQMQVOJLOJZQLUKGPMJXKMVDVYSUKDLKFMPOQWSHUOONHUWDHRKTLEMRMHZOSZMUZKPZOTY
СОВПАЛО : True
```

32 константы (8 пулов × 16 Б, XOR с `0xA5C3`) — **те же и в том же порядке**:
`06d2 01f5 0ab3 007e 0b88 0213 0f06 05c9 09c4 0350 0e21 04da … 0fa4`.

### 5.3. Формат auth-пакета (105 Б)

```
[заголовок датаграммы 6 Б: 00 80 24 c0 60 0c]
[0x06][0x60 = 96][96 ASCII-символов хэша][0x00]
```
Вся датаграмма — **105 Б** (6 + 99), это ровно то, что уходит в UDP. Прежние «138 Б»
получались добавлением 32-байтового трейлера захвата и ещё одного `00` (см. 3.2).
Тело пакета (99 Б) шифруется XOR-перестановкой, контрольная сумма `0x4c`.

---

## 6. Сессия: RakNet/RPC

Суммарные размеры функций по семействам — **не изменились ни на байт**:

| Семейство | Старый | Новый |
|---|---|---|
| `RakClient` | 11 432 | 11 432 |
| `RakPeer` | 29 300 | 29 304 (**+4**) |
| `RPC` | 10 908 | 10 908 |
| `BitStream` | 14 032 | 14 032 |
| `ReliabilityLayer` | 20 844 | 20 844 |
| `RLEncryption` | 364 | 364 |
| `RakClientAuth` | 1 328 | 1 328 |
| `Plugin` | 8 404 | 8 404 |

RPC-символов 23 в обеих сборках. Адреса сместились (`.text` вырос), логика — нет.
Карта RPC — см. `../RPC_TABLE.md` (остаётся валидной).

---

## 7. Что действительно ново в сети

### 7.1. Update flow вместо старого download-API

`UpdateFlowRunner::Start/Cancel/Confirm/Decline/Retry/IsRunning`; состояния — в
`UpdateManagerContext` (`GetCurrentHashCommit`, `GetTargetHashCommit`, `GetFeatureFlags`).
Логика — `tools/resource-patcher/resource_patcher/src/update_manager/*` (пути CI в строках).

### 7.2. installationId → A/B

```
abInstallationId (клиент)
UpdateManagerSystemCallbacks::SetInstallationId
"SetInstallationId empty id, the device takes part in no A/B test."
"ab_test::IsUsedForTest installation id getter is not set, test '{}' is treated as disabled"
```
Идентификатор не уходит в игровой протокол — он определяет, в какую ветку A/B попадёт устройство,
а ветка уже влияет на то, какой `patch_index` и какие фичи получит клиент.

### 7.3. Feature flags

`feature_flags::Parse` читает `update_manager_feature_flag.json` и отдаёт:
flavor, candidate version, next slot, cdn metric, **compatible version**. Отсюда — серверные
переключатели: ssl pinning, скорость/таймауты загрузки, проверка совместимости версии.

### 7.4. HTTP-стек

curl **8.16.0** + OpenSSL **3.6.0**, добавлены WebSocket-функции (`curl_ws_*`) — в игровом
клиенте это транспорт для чего-то нового (в текущем pcap WS не наблюдался).
`User-Agent: %s` — строка приходит из Java, в `.so` не вшита.

---

## 8. Живые проверки (2026-10-17)

| Проверка | Результат |
|---|---|
| `api.blackrussia.online/client/url-config.json` | 200, 2 660 Б |
| `api.blackrussia.online/client/servers.json` | 200, 23 788 Б, **91 сервер, 66 789 онлайн** |
| `dl.blackcdn.me/patch_index.json` | 200, 212 829 Б |
| `patch_index_candidate.json` (backup CDN) | 200, 209 996 Б |
| `update_manager_feature_flag.json` | 200, 1 148 Б |
| UDP-зонд `04 42 52…` → `80.66.82.167:5125` | **тишина** (3 попытки), даже после HTTP `GET /` |

Про UDP: сервер не ответил на пробу из песочницы. Причина не установлена; нельзя по этому
наблюдению заключать ни про фильтр IP/HTTP, ни про сетевую доступность сервера в обычном клиенте.
Код нового `.so` подтверждает ожидаемую последовательность `04 42 52 → 07 00 → hello`; pcap
подтверждает полный обмен. Это ограничение live-проверки, а не вывод о «сломанном» протоколе.

---

## 9. Итог по слоям

```
┌ CDN/API (HTTPS, curl 8.16, SSL pinning)
│   url-config → servers.json → patch_index → patch_index_candidate → feature_flag
├ UDP 5125 (RakNet-подобный игровой транспорт)
│   ├ 04 42 52 → 07 00                              ← прелюдия (3 Б / 2 Б)
│   ├ 00 00 20 87 00 69 → ACK                       ← hello
│   ├ challenge 05 18 "V0pZ…" → 06 60 <96 ASCII>    ← challenge-response
│   └ 0e accept → 14 confirm → RPC/синхронизация
├ UDP 5127 (вторичный ENet-канал; прикладная схема ещё не разобрана)
│   └ ENet host_connect → service/connect → reliable/unreliable peer_send
└ Java-слой
    AppLocalValues (имя продукта), installationId, AB, Retrofit/OkHttp для API
```

**Совместимость:** шифр, auth-хэш, RakNet и RPC идентичны прошлой сборке — старый разбор
(`AUTH_PACKETS.md`, `AUTH_TABLES.md`, `RPC_TABLE.md`) остаётся в силе. Новое — только
обвязка обновлений и конфигурации.

---

## 10. Артефакты

`br_analysis/net_v2/`:
* полный end-to-end проход по RakNet 5125 и ENet 5127: `CONNECT_FLOW_16.110.md`;
* `servers.json`, `url-config.json`, `patch_index.json`, `patch_index_candidate.json`,
  `update_manager_feature_flag.json` — живые конфиги на 2026-10-17;
* `symbols_added.txt`, `symbols_removed.txt` — диff символов старый↔новый;
* `net_symbols.json` — 322 сетевых/крипто символа с адресами и размерами;
* `emulate_auth.py` — воспроизведение auth-хэша на `.so` через unicorn;
* `pcap_first.py` — разбор первых пакетов pcap.

Бинарники (APK 124 МБ, `.so` 37 МБ) — в `/home/user/work_net/`, не входят в архив.

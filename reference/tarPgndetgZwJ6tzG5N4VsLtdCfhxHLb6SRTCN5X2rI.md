# RPC-таблица BLACK RUSSIA — реконструкция

**Объект анализа:** `libblackrussia-client.so` (ELF AArch64, DYN, stripped, NDK r28c)
**SHA-256:** `12a05ede387bffde13eeef13d81b51365507e36c45076b2e14558df97c17f73f` (38 238 624 Б)
**CI-путь сборки:** `/home/gitlab-runner/builds/UnbZc3tLv/0/client/BRClient/src/network/RPCs.cpp`

> Внимание: этот бинарник — не тот же самый, что в APK из Play (CI-id `UnbZc3tLv` против `bbBjo2Hey`),
> поэтому адреса не переносятся между сборками.

---

## 1. Краткий итог

* В бинарнике найден **статический массив регистраций RPC — 125 записей** (идентификаторы **302–457**).
* Формат записи — 16 байт: `{ uint32 id; uint32 pad; void (*handler)(RPCParameters*); }`.
* Таблица лежит в `.data.rel.ro` по адресу **0x230C218** (125 × 16 = 0x7D0 → конец 0x230C9E8).
* Все 125 указателей совпадают с началами функций (проверено по `.eh_frame`, 82 460 функций).
* **Строковых имён RPC в бинарнике нет.** В этой сборке RakNet переделан на **числовые идентификаторы**:
  `RPCMap::AddIdentifierWithFunction(int, std::function<void(RPCParameters*)>)` — вместо имени строка не передаётся.
  Поэтому таблица `id → обработчик` — это и есть протокол, а имена восстанавливаются только по строкам внутри обработчиков.

## 2. Механика

### 2.1 Структура таблицы и её регистрация

```c
struct RpcReg {                              // 16 байт, 125 записей

    uint32_t rpc_id;                          // +0   идентификатор (на проводе — 9 бит)

    uint32_t _pad;                            // +4

    void   (*handler)(RPCParameters*);        // +8   R_AARCH64_RELATIVE

};
```

Регистрация — один цикл внутри функции **0x19C0BA4** (300 Б), вызываемой из **0x198FE0C**:

```
0x19c0c08  add   x9, x22, x21        ; x22 = 0x230C218, x21 = i*16
0x19c0c10  ldr   x8, [x9, #8]        ; обработчик
0x19c0c14  ldr   w1, [x9]            ; rpc_id  -> аргумент 1
0x19c0c2c  mov   x2, sp              ; std::function(handler) -> аргумент 2
0x19c0c28  ldr   x8, [x8] / ldr x8,[x8,#152] / blr x8   ; виртуальный RegisterAsRemoteProcedureCall
0x19c0bfc  add   x21, x21, #0x10 ; cmp x21, #0x7D0 ; b.eq -> ровно 125 записей
```

Цепочка вызовов: `RakClient::RegisterAsRemoteProcedureCall` (0x1C3E860) → `RakPeer::RegisterAsRemoteProcedureCall` (0x1C432D0) → `RPCMap::AddIdentifierWithFunction` (0x1C48204).

### 2.2 Приём пакета (`RakPeer::HandleRPCPacket`, 0x1C42C5C)

```
IgnoreBits(8)                        ; ID_RPC
if (data[0] == 0x13) IgnoreBits(40)  ; вариант с таймстампом
ReadBits(&id, 9, true)               ; <-- 9-битный числовой идентификатор RPC
RPCMap::GetIndexFromFunctionName(this+0x300, id)
ReadCompressed(&payloadBits, 32, true)
ReadBits(buf, payloadBits, false)    ; тело RPC
node = RPCMap::GetNodeFromIndex(index); handler = node->function (+48)
handler(&RPCParameters{ input = buf, numberOfBits = payloadBits, ... })
```

Обработчики читают поля так: `BitStream bs(p->input /*+0*/, p->numberOfBits /*+8*/, …); bs.ReadBits/Read/ReadCompressed(...)`
 (проверено по всем 125 обработчикам: 123 обращения к `[x0]`, 111 — к `[x0,#8]`).

### 2.3 Отправка (клиент → сервер)

Исходящие вызовы идут через виртуальный слот **0xB0** объекта RakClient (глобальный указатель `0x37809B0`):

```
ldr x0,[x8,#0x9B0]  ; RakClient*
ldr x8,[x0] ; ldr x8,[x8,#0xB0]           ; RakClient::RPC(int id, BitStream*, priority, reliability, channel)
mov w1,#id ; mov x2,&bs ; mov w3,#1 ; mov w4,#2 ; mov w5,#0
blr x8
```

## 3. Входящие RPC (обработчики, зарегистрированные клиентом)

Всего **125** записей. Суммарный размер обработчиков — 76644 Б, медиана 292 Б.

| ID | hex | Обработчик | Размер, Б | Имя | Формат (чтения из BitStream) | Исх. |
| | | | | | `→` — чтение выполняется в вызываемом парсере (второй уровень) | |
|---:|---:|---|---:|---|---|:--:|
| 302 | `0x12e` | `0x19b0f18` | 56 | — | — |  |
| 303 | `0x12f` | `0x19af3f0` | 228 | — | ReadBits(8) ReadBits(16) |  |
| 304 | `0x130` | `0x19b811c` | 116 | — | — |  |
| 305 | `0x131` | `0x19bf658` | 424 | — | ReadBits(32) |  |
| 306 | `0x132` | `0x19b4540` | 208 | — | ReadBits(16) |  |
| 307 | `0x133` | `0x19b8190` | 392 | — | ReadBits(8) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) ReadBits(32) ReadBits(8) |  |
| 308 | `0x134` | `0x19b528c` | 272 | — | ReadBits(16) ReadBits(8) |  |
| 309 | `0x135` | `0x19afed0` | 200 | — | ReadBits(16) |  |
| 310 | `0x136` | `0x19b0748` | 256 | — | ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 311 | `0x137` | `0x19b7530` | 328 | — | ReadBits(16) |  |
| 312 | `0x138` | `0x19b9f90` | 988 | — | ReadBits(32) ReadBits(8) ReadBits(8) ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBit |  |
| 313 | `0x139` | `0x19b9bb4` | 988 | — | ReadBits(32) ReadBits(8) Read ReadBits(32) ReadBits(32) ReadBits(8) ReadBits(16) ReadBits(16) Re |  |
| 315 | `0x13b` | `0x19b10fc` | 200 | — | ReadBits(32) |  |
| 316 | `0x13c` | `0x19be988` | 576 | — | ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadB |  |
| 318 | `0x13e` | `0x19b21a8` | 188 | — | ReadBits(16) |  |
| 319 | `0x13f` | `0x19ad6e0` | 2076 | — | ReadBits(8) |  |
| 320 | `0x140` | `0x19b5838` | 1072 | — | ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) | 1 |
| 321 | `0x141` | `0x19b6384` | 88 | — | — |  |
| 322 | `0x142` | `0x19afa18` | 188 | — | ReadBits(16) |  |
| 323 | `0x143` | `0x19ba9bc` | 10308 | — | ReadBits(8) ReadBits(8) ReadBits(16) ReadBits(8) ReadBits(8) ReadBits(32) ReadBits(32) ReadBits( |  |
| 325 | `0x145` | `0x19ad404` | 732 | — | ReadBits(16) |  |
| 326 | `0x146` | `0x19acd48` | 220 | — | ReadBits(16) ReadBits(8) |  |
| 327 | `0x147` | `0x19b1830` | 244 | — | ReadBits(16) ReadBits(32) |  |
| 328 | `0x148` | `0x19bf564` | 244 | — | ReadBits(16) ReadBits(32) |  |
| 329 | `0x149` | `0x19adefc` | 2540 | — | Read(172) ReadBits(8) ReadBits(16) ReadBits(8) Read ReadBits(8) ReadBits(8) Read ReadBits(8) Rea |  |
| 330 | `0x14a` | `0x19b51b8` | 212 | — | ReadBits(8) |  |
| 331 | `0x14b` | `0x19b152c` | 232 | — | ReadBits(16) ReadBits(32) |  |
| 333 | `0x14d` | `0x19b7b9c` | 288 | — | ReadBits(16) |  |
| 334 | `0x14e` | `0x19b1924` | 276 | — | ReadBits(8) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 335 | `0x14f` | `0x19b1a38` | 552 | — | ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 336 | `0x150` | `0x19b7678` | 1316 | — | ReadBits(16) ReadBits(8) Read ReadBits(8) Read ReadBits(32) |  |
| 337 | `0x151` | `0x19afad4` | 1020 | — | ReadBits(32) ReadBits(8) Read ReadBits(8) Read ReadBits(8) ReadBits(16) ReadBits(16) ReadBits(32 |  |
| 338 | `0x152` | `0x19b2740` | 300 | — | ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 339 | `0x153` | `0x19b1478` | 180 | — | — |  |
| 340 | `0x154` | `0x19b1d28` | 196 | — | ReadBits(8) |  |
| 341 | `0x155` | `0x19b47c0` | 2552 | ScrSetObjectMaterial | ReadBits(16) ReadBits(8) ReadBits(8) ReadBits(16) ReadBits(8) Read ReadBits(8) Read ReadBits(32) |  |
| 343 | `0x157` | `0x19b63dc` | 248 | — | ReadBits(32) |  |
| 344 | `0x158` | `0x19acfd4` | 36 | — | — |  |
| 345 | `0x159` | `0x19b6f54` | 1500 | — | ReadBits(16) ReadBits(8) Read ReadBits(8) Read ReadBits(32) ReadBits(32) |  |
| 346 | `0x15a` | `0x19bed04` | 204 | — | ReadBits(16) |  |
| 347 | `0x15b` | `0x19b6618` | 564 | — | ReadBits(16) ReadBits(8) ReadBits(8) Read ReadBits(8) Read ReadBits(8) Read ReadBits(32) Read |  |
| 348 | `0x15c` | `0x19b1374` | 28 | — | — |  |
| 349 | `0x15d` | `0x19b0364` | 272 | — | ReadBits(16) ReadBits(16) Read |  |
| 350 | `0x15e` | `0x19b2264` | 240 | — | ReadBits(16) ReadBits(32) |  |
| 351 | `0x15f` | `0x19bf2b8` | 232 | — | ReadBits(16) ReadBits(16) |  |
| 353 | `0x161` | `0x19b11c4` | 228 | — | ReadBits(16) ReadBits(8) |  |
| 354 | `0x162` | `0x19b0298` | 204 | — | ReadBits(16) |  |
| 358 | `0x166` | `0x19ac9a0` | 36 | — | — |  |
| 359 | `0x167` | `0x19bedd0` | 228 | — | ReadBits(16) ReadBits(32) |  |
| 360 | `0x168` | `0x19ace24` | 432 | — | ReadBits(32) ReadBits(32) Read |  |
| 362 | `0x16a` | `0x19b12a8` | 204 | — | ReadBits(32) |  |
| 363 | `0x16b` | `0x19bfc14` | 188 | — | ReadBits(16) |  |
| 364 | `0x16c` | `0x19b847c` | 1488 | — | ReadBits(8) ReadBits(8) ReadBits(8) Read ReadBits(8) Read |  |
| 365 | `0x16d` | `0x19b0944` | 324 | — | ReadBits(32) |  |
| 366 | `0x16e` | `0x19bf3a0` | 208 | — | ReadBits(16) |  |
| 368 | `0x170` | `0x19b684c` | 1800 | — | ReadBits(16) ReadBits(8) ReadBits(8) Read ReadBits(8) Read ReadBits(8) Read ReadBits(32) Read |  |
| 369 | `0x171` | `0x19beeb4` | 204 | — | ReadBits(16) |  |
| 372 | `0x174` | `0x19b0608` | 320 | ScrSetSpawnInfo | →ReadBits(8) →ReadBits(32) →ReadBits(8) →ReadBits(96) →ReadBits(32) →ReadBits(96) |  |
| 373 | `0x175` | `0x19af61c` | 200 | — | ReadBits(32) |  |
| 374 | `0x176` | `0x19b919c` | 2584 | — | ReadBits(16) ReadBits(32) ReadBits(32) | 1 |
| 375 | `0x177` | `0x19b64d4` | 324 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) Read |  |
| 376 | `0x178` | `0x19b0474` | 404 | — | ReadBits(8) ReadBits(32) |  |
| 377 | `0x179` | `0x19af4d4` | 328 | Pickup | ReadBits(32) Read(20) |  |
| 379 | `0x17b` | `0x19b1c60` | 200 | — | ReadBits(8) |  |
| 380 | `0x17c` | `0x19bf8dc` | 824 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadB |  |
| 382 | `0x17e` | `0x19aee0c` | 284 | — | ReadBits(16) ReadBits(16) ReadBits(8) | 1 |
| 383 | `0x17f` | `0x19b1390` | 232 | — | ReadBits(16) ReadBits(8) |  |
| 384 | `0x180` | `0x19b2b64` | 260 | — | ReadBits(16) ReadBits(8) Read |  |
| 385 | `0x181` | `0x19b7eec` | 560 | — | ReadBits(8) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBi |  |
| 386 | `0x182` | `0x19b8bc8` | 1492 | — | →ReadBits →ReadBits(32) →ReadBits(32) →ReadBits(32) →ReadBits(32) →ReadBits(32) →ReadBits(32) →R |  |
| 387 | `0x183` | `0x19b1034` | 200 | — | ReadBits(32) |  |
| 388 | `0x184` | `0x19b41b4` | 576 | — | ReadBits(16) |  |
| 389 | `0x185` | `0x19b539c` | 272 | — | ReadBits(16) ReadBits(8) |  |
| 391 | `0x187` | `0x19b2ed8` | 4828 | ScrCreateObject | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadB |  |
| 393 | `0x189` | `0x19bebc8` | 316 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 394 | `0x18a` | `0x19b1dec` | 232 | — | ReadBits(8) ReadBits(8) |  |
| 395 | `0x18b` | `0x19b1738` | 248 | — | ReadBits(16) ReadBits(16) |  |
| 397 | `0x18d` | `0x19aed3c` | 208 | — | ReadBits(16) |  |
| 398 | `0x18e` | `0x19b2c68` | 344 | — | ReadBits(8) |  |
| 399 | `0x18f` | `0x19be6a8` | 736 | — | ReadBits(8) ReadBits(16) ReadBits(8) ReadBits(32) ReadBits(32) ReadBits(8) ReadBits(32) |  |
| 400 | `0x190` | `0x19b0a88` | 992 | — | ReadBits(32) ReadBits(32) |  |
| 401 | `0x191` | `0x19acb64` | 484 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) ReadBits(8) Read |  |
| 402 | `0x192` | `0x19b54ac` | 264 | — | ReadBits(16) ReadBits(8) ReadBits(8) |  |
| 403 | `0x193` | `0x19b5750` | 232 | — | ReadBits(16) ReadBits(32) | 1 |
| 404 | `0x194` | `0x19af21c` | 244 | — | — |  |
| 405 | `0x195` | `0x19b0848` | 252 | — | ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 407 | `0x197` | `0x19b5d58` | 464 | — | ReadBits(8) |  |
| 408 | `0x198` | `0x19bef80` | 488 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadB |  |
| 409 | `0x199` | `0x19aef28` | 244 | — | ReadBits(16) ReadBits(16) | 1 |
| 410 | `0x19a` | `0x19b8384` | 248 | — | ReadBits(8) |  |
| 411 | `0x19b` | `0x19b55b4` | 412 | — | ReadBits(16) ReadBits(8) ReadBits(8) ReadBits(8) ReadBits(8) ReadBits(8) ReadBits(8) ReadBits(8) |  |
| 412 | `0x19c` | `0x19b1614` | 292 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 415 | `0x19f` | `0x19b25c0` | 384 | — | ReadBits(32) ReadBits(32) ReadBits(32) Read |  |
| 416 | `0x1a0` | `0x19b2dc0` | 280 | — | ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 417 | `0x1a1` | `0x19af8b8` | 352 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadB |  |
| 418 | `0x1a2` | `0x19aff98` | 768 | ScrShowTextDraw | ReadBits(16) Read(65) ReadBits(16) Read |  |
| 419 | `0x1a3` | `0x19acff8` | 976 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) ReadBi |  |
| 421 | `0x1a5` | `0x19ac434` | 1388 | — | ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(16) ReadBits(32) ReadBits(8) ReadBits(8) ReadBit |  |
| 422 | `0x1a6` | `0x19b2444` | 380 | — | ReadBits(16) ReadBits(32) |  |
| 424 | `0x1a8` | `0x19b2018` | 400 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 425 | `0x1a9` | `0x19bf168` | 336 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 426 | `0x1aa` | `0x19b2354` | 240 | — | ReadBits(16) Read(12) |  |
| 427 | `0x1ab` | `0x19bf800` | 220 | — | ReadBits(16) ReadBits(8) |  |
| 428 | `0x1ac` | `0x19b286c` | 384 | — | ReadBits(8) |  |
| 429 | `0x1ad` | `0x19ba36c` | 1616 | — | ReadBits(16) ReadBits(8) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) ReadBit |  |
| 430 | `0x1ae` | `0x19b7e78` | 116 | — | — |  |
| 431 | `0x1af` | `0x19b1ed4` | 324 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(16) ReadBits(16) |  |
| 432 | `0x1b0` | `0x19af6e4` | 468 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) ReadBi |  |
| 433 | `0x1b1` | `0x19af310` | 224 | — | ReadBits(32) ReadBits(32) |  |
| 434 | `0x1b2` | `0x19b0f50` | 228 | — | ReadBits(8) |  |
| 435 | `0x1b3` | `0x19af01c` | 208 | — | ReadBits(16) |  |
| 439 | `0x1b7` | `0x19bf470` | 244 | — | ReadBits(16) ReadBits(32) |  |
| 440 | `0x1b8` | `0x19b7cbc` | 444 | — | ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(8) |  |
| 442 | `0x1ba` | `0x19b5f28` | 1056 | — | →ReadBits →ReadBits(32) →ReadBits(32) →ReadBits(32) →ReadBits(32) →ReadBits(32) →ReadBits(32) →R |  |
| 443 | `0x1bb` | `0x19b43f4` | 332 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 444 | `0x1bc` | `0x19b5c68` | 240 | — | ReadBits(16) ReadBits(8) |  |
| 446 | `0x1be` | `0x19b4610` | 432 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 447 | `0x1bf` | `0x19b29ec` | 376 | — | ReadBits(16) ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 448 | `0x1c0` | `0x19af0ec` | 304 | — | ReadBits(32) ReadBits(32) ReadBits(32) |  |
| 450 | `0x1c2` | `0x19ac9c4` | 416 | — | ReadBits(32) |  |
| 452 | `0x1c4` | `0x19bfdcc` | 1864 | — | ReadBits(16) ReadBits(8) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBits(32) ReadBi |  |
| 453 | `0x1c5` | `0x19c0514` | 228 | — | ReadBits(16) |  |
| 455 | `0x1c7` | `0x19c05f8` | 704 | IPLLoad | ReadBits(8) ReadBits(8) Read |  |
| 456 | `0x1c8` | `0x19c08b8` | 748 | IPLUnload | ReadBits(8) ReadBits(8) Read |  |
| 457 | `0x1c9` | `0x19bfcd0` | 252 | — | ReadBits(16) ReadBits(32) ReadBits(32) |  |

_Имена восстановлены для 7 из 125: только те обработчики, внутри которых есть
строковые константы (например `ScrSetSpawnInfo`, `ScrShowTextDraw`, `IPLLoad`). У остальных строк нет — имя
выводится только нативным разбором логики в Ghidra/IDA._

### 3.1 Именованные RPC — доказательства

Имена взяты из строк assert-сообщений и имён переменных внутри самих обработчиков (`src/network/RPCs.cpp`):

| ID | Имя | Строка-доказательство |
|---:|---|---|
| 341 | ScrSetObjectMaterial | `ScrSetObjectMaterial RPC: material object type isn't supported. type: %u` |
| 372 | ScrSetSpawnInfo | `Assertion failed! ... optSpawnInfo.has_value()` |
| 377 | Pickup | `Assertion failed! ... optPickup.has_value()` |
| 391 | ScrCreateObject | `ScrCreateObject RPC: material object type isn't supported. object model id: %u; type: %u` |
| 418 | ScrShowTextDraw | `Assertion failed! ... optTextDrawTransmit.has_value()` |
| 455 | IPLLoad | `IPLLoad RPC - name length (%d) is too long` |
| 456 | IPLUnload | `IPLUnload RPC - name length (%d) is too long` |

Побочно из этих строк видно, что разбор полезной нагрузки делегирован вспомогательным функциям-парсерам
(`std::optional`-паттерн), поэтому у части обработчиков «собственная» последовательность чтений пуста —
в таблице она помечена знаком `→`.

## 4. Исходящие RPC (клиент → сервер)

Найдено **68 проверенных сайтов отправки**, уникальных идентификаторов — **26**.

| ID | Сайтов | Приоритет | Надёжность | Обработчик входящего (если есть) |
|---:|---:|---|---|---|
| 67 | 1 | HIGH | RELIABLE_ORDERED | — |
| 69 | 1 | HIGH | RELIABLE_ORDERED | — |
| 314 | 1 | HIGH | RELIABLE | — |
| 320 | 1 | HIGH | RELIABLE | `0x19b5838` |
| 324 | 1 | HIGH | RELIABLE | — |
| 342 | 1 | HIGH | RELIABLE | — |
| 355 | 1 | HIGH | RELIABLE | — |
| 356 | 1 | HIGH | RELIABLE | — |
| 357 | 1 | HIGH | RELIABLE | — |
| 361 | 1 | HIGH | RELIABLE | — |
| 367 | 1 | HIGH | RELIABLE_ORDERED | — |
| 371 | 1 | HIGH | RELIABLE | — |
| 374 | 1 | HIGH | RELIABLE | `0x19b919c` |
| 378 | 1 | HIGH | RELIABLE | — |
| 382 | 1 | HIGH | RELIABLE_SEQUENCED | `0x19aee0c` |
| 396 | 2 | MEDIUM | RELIABLE_ORDERED | — |
| 403 | 1 | HIGH | RELIABLE | `0x19b5750` |
| 409 | 1 | HIGH | RELIABLE_SEQUENCED | `0x19aef28` |
| 413 | 41 | HIGH,MEDIUM | UNRELIABLE_SEQUENCED,RELIABLE,RELIABLE_SEQUENCED | — |
| 414 | 1 | HIGH | RELIABLE | — |
| 423 | 2 | HIGH | RELIABLE | — |
| 436 | 1 | HIGH | RELIABLE | — |
| 437 | 1 | HIGH | RELIABLE | — |
| 441 | 1 | HIGH | RELIABLE | — |
| 451 | 1 | HIGH | RELIABLE_SEQUENCED | — |
| 454 | 1 | HIGH | RELIABLE_ORDERED | — |

* Идентификаторы, встречающиеся в обе стороны (клиент и принимает, и отправляет): 320, 374, 382, 403, 409.
* Только исходящие (у клиента нет обработчика): 67, 69, 314, 324, 342, 355, 356, 357, 361, 367, 371, 378, 396, 413, 414, 423, 436, 437, 441, 451, 454 — эти RPC адресованы серверу.
* Самый «горячий» исходящий RPC — **413**: 41 сайтов вызова (сетевая синхронизация).

## 5. Методика и артефакты

| Шаг | Скрипт | Результат |
|---|---|---|
| Релокации `.rela.dyn` (132 665 RELATIVE) | `readelf -r` | указатели таблицы |
| Карта функций (82 460) | `scripts/16_ehframe_funcs.py` | `.eh_frame` → `[start, size]` |
| Разбор таблицы 0x230C218 | `scripts/17/18_rpc_*.py` | 125 записей, строки, последовательности чтений |
| Исходящие RPC | `scripts/19/20_*.py` | 92 кандидата → 68 проверенных сайтов |
| Сводка | `scripts/21_final_report.py` | этот отчёт + CSV/JSON |
Файлы: `evidence/rpc_table_final.csv`, `evidence/rpc_table_final.json`, `evidence/outbound_rpc_sites.json`, `evidence/rpc_summary.json`.

## 6. Ограничения

* Имена 117 из 125 обработчиков не восстановлены: строковых констант в них нет, а таблицы имён не существует
  (проверено: дамп `.rodata`, скан всех релокаций и скан 39 массивов строковых указателей).
* «Формат» обработчика — это последовательность вызовов чтения из BitStream внутри функции, а не гарантированная
  схема полей: часть полей может читаться inline (компилятор встраивает короткие чтения).
* 9-битное поле id ограничивает пространство 0–511; занято 125 значений.

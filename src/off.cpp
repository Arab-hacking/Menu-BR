/*
 * off.cpp — таблицы адресов
 */
#include "br_common.h"

const br_build_table_t BR_TABLES[] = {
    /* [0] — placeholder для BR_BUILD_AUTO */
    { "auto", {0}, 0,0,0,0, 0,0,0,0,0, 0x118, 0, 0x118, 0x150, 0xB8, 0x430, 0x54A, 0,
      {0},{0},{0},{0} },

    /* [1] 16.106.17587 — из документации (VEHICLE_STRUCTURE.md, PLAYER_STRUCTURE.md) */
    {
        "16.106.17587",
        /* build-id неизвестен */ {0},
        /* глобалы */
        0x37809C8,   /* pool_glob (VehiclePool*) */
        0x37809C0,   /* player_mgr_glob (PlayerManager*) */
        0x354C5D8,   /* ped_table_glob (шаг 0xF8) */
        0x3780C74,   /* color_table_glob (16B/игрок) */
        /* функции */
        0x1990A78,   /* dispatcher (NetGame.cpp) */
        0x19D2FDC,   /* GetByNetId */
        0x19AADA4,   /* GetPlayerById */
        0x1B815A0,   /* CreateVehicle(modelIndex) */
        0x1B7F444,   /* CVehicle::метод(bool) — двигатель/свет (гипотеза из доков) */
        /* поля */
        0x148, 0x000, 0x118, 0x150, 0xB8, 0x430, 0x54A,
        /* sync_id_glob неизвестен → монитор отключён */
        0,
        /* сигнатура GetByNetId: общий признак — лимит id <= 1999 (cmp w8,#0x7cf) */
        { 0x1f, 0x3d, 0x1f, 0x71, 0x00, 0x00, 0x00, 0x00 },
        { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
        /* сигнатура диспетчера неизвестна */
        { 0 }, { 0 },
    },

    /* [2] 16.110.18323 — сверено с реальным бинарником (см. комментарии в off.h) */
    {
        "16.110.18323",
        /* build-id */
        { 0x80,0x5b,0xe0,0x86,0x54,0x8e,0xb2,0x36,0xee,0x46,
          0x9e,0x88,0xc1,0x1d,0x5d,0x3e,0x66,0xcf,0xe1,0xba },
        /* глобалы (подтверждены .rela.dyn) */
        0x37FBEC8,   /* pool_glob */
        0x37FBEC0,   /* player_mgr_glob */
        0x35C7A28,   /* ped_table_glob */
        0x37FC174,   /* color_table_glob */
        /* функции */
        0x19D4664,   /* dispatcher (найдён по ассерт-строке, switch по id) */
        0x1A1C1C8,   /* GetByNetId (дизассемблировано, тело совпадает) */
        0x19F0E48,   /* GetPlayerById (по докам сравнения) */
        0x00000000,  /* CreateVehicle — не выделен для этой сборки */
        0x00000000,  /* метод двигателя — не выделен */
        /* поля (перепроверены кодом новой сборки) */
        0x540, 0x548, 0x118, 0x150, 0xB8, 0x430, 0x54A,
        /* packetId = *(0x49B7E0 + 0xEB8) — из кода диспетчера */
        0x49C698,
        /* сигнатура GetByNetId: and w8,w1,#0xffff; cmp w8,#0x7cf */
        { 0x28, 0x3c, 0x00, 0x12, 0x1f, 0x3d, 0x1f, 0x71 },
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
        /* диспетчер, 36B — единственное вхождение в .text 16.110:
         * sub sp,#0x60; stp x29,x30,[sp,#0x20]; str x23,[sp,#0x30];
         * stp x22,x21,[sp,#0x40]; stp x20,x19,[sp,#0x50]; mov x29,sp;
         * nop; mov w19,w0; mov w20,w1 */
        { 0xff,0x83,0x01,0xd1, 0xfd,0x7b,0x02,0xa9,
          0xf7,0x1b,0x00,0xf9, 0xf6,0x57,0x04,0xa9,
          0xf4,0x4f,0x05,0xa9, 0xfd,0x83,0x00,0x91,
          0x57,0xd0,0x3b,0xd5, 0xf3,0x03,0x01,0x2a,
          0xf4,0x03,0x00,0x2a },
        { 0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
          0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
          0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
          0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
          0xff,0xff,0xff,0xff },
    },

    /* [3] 16.111.19050 — карта офсетов, подтверждённая рабочим модулем
     * на устройстве (br_imgui_inspector) + статический анализ:
     *   build-id 2b301eb3b52fe709056c59e22e214775027674dd (VA 0x2E0),
     *   глобалы .bss 0x036130A0..0x03613108 (GOT-ссылки из 0x0E77FA0..),
     *   PlayerPool: local +0x00, remote +0x08+id*8 (1504),
     *   CLocalPlayer: ID +0x00, ник +0x02, CPed* +0x38, CVehicle* +0x40,
     *   CRemotePlayer: CPed* +0x10, ник +0x18, ID +0x4A (sizeof 0x288),
     *   CPed: pos +0x38..40, HP +0x4B0, Armor +0x4B8, CVehicle* +0x540,
     *         флаг в авто +0x548,
     *   CVehicle: pos +0x38..40, vel +0x180 (float[3]), backref +0xB8,
     *         класс +0x430, netID +0x434,
     *   RakClient: g_pRakClient 0x036130A0, vtable 0x02140BF0,
     *         Send thunk 0x01A513B8 (slot +0x38), RPC thunk 0x01A51AC8 (+0xB0),
     *   JNI: JNILib_step 0x01901D44, JNILib_multiTouchEvent 0x01902270.
     * Бинарника в песочнице нет — сигнатуры нулевые, детект по build-id
     * + структурным провам пулов. */
    {
        "16.111.19050",
        { 0x2b,0x30,0x1e,0xb3,0xb5,0x2f,0xe7,0x09,0x05,0x6c,
          0x59,0xe2,0x2e,0x21,0x47,0x75,0x02,0x76,0x74,0xdd },
        /* глобалы */
        0x036130B8,  /* pool_glob (VehiclePool* 2000×8) */
        0x036130B0,  /* player_mgr_glob (CPlayerPool*) */
        0,           /* ped_table_glob — глобальной таблицы нет, пед через объект */
        0,           /* color_table_glob — неизвестен (цвет — UI-функция) */
        /* функции */
        0,           /* dispatcher — монитор идёт хуком Send (rak_send_va) */
        0,           /* get_by_net_id — не нужен (скан пулов) */
        0x017B17A4,  /* get_player_by_id (PlayerPool::GetRemote, VA) */
        0,           /* create_vehicle — не выделен */
        0,           /* veh_engine_method — не выделен */
        /* поля (относительно ПЕДА — см. ped-офсеты ниже) */
        0x540, 0x548, 0, 0, 0xB8, 0x430, 0,
        /* sync_id_glob — монитор через хук Send */
        0,
        /* сигнатуры — нет бинарника для снятия */
        { 0 }, { 0 }, { 0 }, { 0 },
        /* новое (16.111) */
        0x38,        /* player_ped_off_local: CLocalPlayer+0x38 CPed* */
        0x10,        /* player_ped_off_remote: CRemotePlayer+0x10 CPed* */
        0x036130A0,  /* rak_client_glob */
        0x01901D44,  /* jni_step_va */
        0x01902270,  /* jni_touch_va */
        0x01A513B8,  /* rak_send_va */
        0x01A51AC8,  /* rak_rpc_va */
        0x036130D0,  /* obj_pool_glob (4096×8) */
    },
};

const int BR_TABLE_COUNT = (int)(sizeof(BR_TABLES) / sizeof(BR_TABLES[0]));

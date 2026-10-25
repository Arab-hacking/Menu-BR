#pragma once

#include <cstddef>
#include <cstdint>

namespace br::offsets {

// ============================================================================
// Метаданные целевой библиотеки: BLACK RUSSIA 16.111.19050 (Android arm64-v8a)
// Все VA ниже — статические виртуальные адреса ELF до прибавления ASLR base.
// Первая секция PT_LOAD имеет p_vaddr == 0x0, поэтому:
//     runtime_address = module_base + VA
// ============================================================================

inline constexpr const char* kTargetModuleName   = "libblackrussia-client.so";
inline constexpr const char* kTargetBuildVersion = "16.111.19050 (patch 1580)";
inline constexpr uint64_t    kTargetElfFileSize  = 36296864ULL;
inline constexpr const char* kTargetSha256 =
    "44e5b7f5e22a2fa7fb16268bf043d25f1feeb1d600dd6b56feb3b43dd236f1fd";

// .note.gnu.build-id (VA 0x2D0, длина дескриптора 20 байт по смещению 0x2E0)
inline constexpr uintptr_t kGnuBuildIdDescVA = 0x000002E0ULL;
inline constexpr size_t    kGnuBuildIdLength = 20;
inline constexpr uint8_t   kExpectedGnuBuildId[kGnuBuildIdLength] = {
    0x2b, 0x30, 0x1e, 0xb3, 0xb5, 0x2f, 0xe7, 0x09, 0x05, 0x6c,
    0x59, 0xe2, 0x2e, 0x21, 0x47, 0x75, 0x02, 0x76, 0x74, 0xdd
};

// Секции ELF (VA)
namespace sections {
inline constexpr uintptr_t kRodataVA    = 0x003B6EC0ULL; // 3 895 040
inline constexpr size_t    kRodataSize  = 5191048ULL;
inline constexpr uintptr_t kTextVA      = 0x00D2E1B0ULL; // 13 820 096
inline constexpr size_t    kTextSize    = 20312752ULL;
inline constexpr uintptr_t kDataRelRoVA = 0x0209A700ULL;
inline constexpr uintptr_t kGotVA       = 0x021BAC80ULL;
inline constexpr uintptr_t kDataVA      = 0x021C63C0ULL;
inline constexpr uintptr_t kBssVA       = 0x022A4EF0ULL;
inline constexpr size_t    kBssSize     = 27798432ULL;
} // namespace sections

// ============================================================================
// Встроенный в libblackrussia-client.so Dear ImGui 1.90.9 (19090) + OpenGL ES 3
// Подтверждено дизассемблированием 0x1884C04..0x18854DC и 0x19BE8F8..0x19E0650
// ============================================================================
namespace imgui_va {

// Ожидаемые размеры структур из вызова DebugCheckVersionAndDataLayout (0x1884C18)
// и CreateContext (0x19C1BB0)
inline constexpr const char* kEmbeddedImGuiVersion      = "1.90.9";
inline constexpr int         kEmbeddedImGuiVersionNum   = 19090;
inline constexpr size_t      kExpectedSizeofImGuiIO     = 0x38E0; // 14 560 Б
inline constexpr size_t      kExpectedSizeofImGuiStyle  = 0x0470; // 1 136 Б
inline constexpr size_t      kExpectedSizeofImGuiCtx    = 0x62B8; // 25 272 Б
inline constexpr size_t      kExpectedSizeofImVec2      = 0x0008;
inline constexpr size_t      kExpectedSizeofImVec4      = 0x0010;
inline constexpr size_t      kExpectedSizeofImDrawVert  = 0x0014;
inline constexpr size_t      kExpectedSizeofImDrawIdx   = 0x0002;

// Ключевые смещения внутри ImGuiContext в бинаре (проверено по 0x19C1C68 / 0x19C58E8)
inline constexpr size_t      kCtxOffsetIO               = 0x0008;
inline constexpr size_t      kCtxOffsetStyle            = 0x38E8;
inline constexpr size_t      kCtxOffsetFrameCount       = 0x3F88;
inline constexpr size_t      kCtxOffsetCurrentWindow    = 0x4040;
inline constexpr size_t      kCtxOffsetViewportsData    = 0x4B58; // Viewports (0x4B50) + 8

// Глобальные переменные встроенного ImGui и клиентской обёртки
inline constexpr uintptr_t kGImGuiPtr                   = 0x03C9D9D0ULL; // ImGuiContext*
inline constexpr uintptr_t kGImAllocatorUserData        = 0x03C9D9E0ULL; // void*
inline constexpr uintptr_t kGImAllocatorAllocFuncPtr    = 0x0229C350ULL; // ImGuiMemAllocFunc
inline constexpr uintptr_t kGImAllocatorFreeFuncPtr     = 0x0229C358ULL; // ImGuiMemFreeFunc

inline constexpr uintptr_t kWrapperFontScaleFloat       = 0x0363D528ULL; // float
inline constexpr uintptr_t kWrapperLoadedFontsArray     = 0x0363D530ULL; // ImFont*[6]
inline constexpr size_t    kWrapperLoadedFontCount      = 6;
inline constexpr uintptr_t kWrapperFrameActiveBool      = 0x0363D560ULL; // uint8_t (0/1)
inline constexpr uintptr_t kRenderThreadTouchArray      = 0x022968F0ULL; // struct { int32_t x, y, state, pad; }

// Внутренние функции Dear ImGui 1.90.9 в .text целевой библиотеки
inline constexpr uintptr_t kDefaultAllocWrapper         = 0x019D73ACULL;
inline constexpr uintptr_t kDefaultFreeWrapper          = 0x019D73B0ULL;
inline constexpr uintptr_t kDebugCheckVersionLayout     = 0x019D55E4ULL;
inline constexpr uintptr_t kCreateContext               = 0x019C1B84ULL;
inline constexpr uintptr_t kInitialize                  = 0x019C277CULL;
inline constexpr uintptr_t kGetForegroundDrawList       = 0x019C58E8ULL;
inline constexpr uintptr_t kGetIO                       = 0x019C59F8ULL;
inline constexpr uintptr_t kGetDrawData                 = 0x019C5A08ULL;
inline constexpr uintptr_t kNewFrame                    = 0x019C6CF4ULL;
inline constexpr uintptr_t kBegin                       = 0x019CBF18ULL;
inline constexpr uintptr_t kEndFrame                    = 0x019D06C8ULL;
inline constexpr uintptr_t kRender                      = 0x019D15C4ULL;
inline constexpr uintptr_t kStyleColorsDark             = 0x019DB8A8ULL;
inline constexpr uintptr_t kImDrawList_AddText          = 0x019DF418ULL;
inline constexpr uintptr_t kAddFontFromMemoryTTF        = 0x019E0650ULL;
inline constexpr uintptr_t kIO_AddMousePosEvent         = 0x019BEBD8ULL;
inline constexpr uintptr_t kIO_AddMouseButtonEvent      = 0x019BED9CULL;
inline constexpr uintptr_t kIO_AddMouseSourceEvent      = 0x019BF1F0ULL;

// Клиентский бэкенд OpenGL ES 3 (src/rw/imgui_impl_opengl3.cpp) и обёртка рендера
inline constexpr uintptr_t kImplOpenGL3_Init            = 0x018E0C64ULL;
inline constexpr uintptr_t kImplOpenGL3_PostRender      = 0x018E0DA8ULL;
inline constexpr uintptr_t kImplOpenGL3_NewFrame        = 0x018E0E18ULL;
inline constexpr uintptr_t kImplOpenGL3_RenderDrawData  = 0x018E0E84ULL;

inline constexpr uintptr_t kWrapper_Init                = 0x01884C04ULL;
inline constexpr uintptr_t kWrapper_LoadFontSlot        = 0x01884D30ULL;
inline constexpr uintptr_t kWrapper_BeginFrame          = 0x01885450ULL;
inline constexpr uintptr_t kWrapper_EndFrameAndRender   = 0x01885490ULL;
inline constexpr uintptr_t kWrapper_IsFrameActive       = 0x018854D4ULL;
inline constexpr uintptr_t kWrapper_UpdateTouchInput    = 0x00E77D88ULL;

} // namespace imgui_va

// ============================================================================
// Точки входа JNI рендера и мультитача в libblackrussia-client.so (16.111.19050)
// Используются для полностью автономного (собственного) рендера Dear ImGui
// поверх OpenGL ES 3.0 без зависимости от внутреннего GImGui клиента.
// ============================================================================
namespace jni_bridge {
inline constexpr uintptr_t kJni_JNILib_resize           = 0x01901D18ULL; // stp w2, w3, [0x03C0FAD8]
inline constexpr uintptr_t kJni_JNILib_step             = 0x01901D44ULL; // кадр GLThread (GLSurfaceView.onDrawFrame)
inline constexpr uintptr_t kJni_JNILib_multiTouchEvent  = 0x01902270ULL; // ввод касаний экрана (action, count, x1, y1...)
inline constexpr uintptr_t kBss_SurfaceSizeWH           = 0x03C0FAD8ULL; // int32_t[2]: {width, height}
} // namespace jni_bridge

// ============================================================================
// Глобальные указатели сетевой подсистемы и пулов (.bss 0x036130A0..0x03613108)
// Разрешены через .rela.dyn GOT-ссылки из 0x0E77FA0..0x0E780B8 и 0x01794918
// ============================================================================
namespace globals {
inline constexpr uintptr_t kRakClientInterfacePtr       = 0x036130A0ULL; // RakClientInterface*
inline constexpr uintptr_t kPlayerPoolPtr               = 0x036130B0ULL; // CPlayerPool*
inline constexpr uintptr_t kVehiclePoolPtr              = 0x036130B8ULL; // CVehiclePool*
inline constexpr uintptr_t kPickupOrAuxPoolPtr          = 0x036130C8ULL;
inline constexpr uintptr_t kObjectPoolPtr               = 0x036130D0ULL; // 4096 слотов по 8 Б
inline constexpr uintptr_t kTextLabelOrAuxPoolPtr       = 0x036130F8ULL;
inline constexpr uintptr_t kGangZoneOrAuxPoolPtr        = 0x03613100ULL;
} // namespace globals

// ============================================================================
// Смещения PlayerPool, LocalPlayer, RemotePlayer, CPed (16.111.19050)
// ============================================================================
namespace player {
// PlayerPool (проверено по 0x17B179C..0x17B1A54)
inline constexpr uint16_t  kMaxPlayerId                 = 1503;   // 0x05DF
inline constexpr size_t    kPlayerPoolSlotCount         = 1504;   // 0x05E0 (0..1503)
inline constexpr size_t    kPoolLocalPlayerPtrOffset    = 0x0000; // CLocalPlayer*
inline constexpr size_t    kPoolRemoteSlotsBaseOffset   = 0x0008; // CRemotePlayer*[1504], шаг 8
inline constexpr size_t    kPoolSlotStride              = 8;
inline constexpr size_t    kPoolAuxInt32ArrayAOffset    = 0x2F30; // int32_t[1504] (score/ping A)
inline constexpr size_t    kPoolAuxInt32ArrayBOffset    = 0x46B0; // int32_t[1504] (score/ping B)

inline constexpr uintptr_t kFnPlayerPoolGetLocal        = 0x017B179CULL;
inline constexpr uintptr_t kFnPlayerPoolGetRemote       = 0x017B17A4ULL;
inline constexpr uintptr_t kFnPlayerPoolGetPed          = 0x017B17C4ULL;
inline constexpr uintptr_t kFnPlayerPoolCreateSlot      = 0x017B18D4ULL;

// CLocalPlayer (проверено по 0x178BB3C, 0x178BB44, 0x178BC0C, 0x178BDB0, 0x178E904, 0x178EE04)
inline constexpr size_t    kLocalIdOffset               = 0x0000; // uint16_t
inline constexpr size_t    kLocalNameOffset             = 0x0002; // char[] (до 0x38)
inline constexpr size_t    kLocalNameMaxRead            = 48;
inline constexpr size_t    kLocalPedPtrOffset           = 0x0038; // CPed*
inline constexpr size_t    kLocalVehiclePtrOffset       = 0x0040; // CVehicle*
inline constexpr size_t    kLocalCacheBlockOffset       = 0x00F0; // 0xF0..0x108 (кэш пакета 33)
inline constexpr size_t    kLocalCacheBlockSize         = 0x001C;
inline constexpr size_t    kLocalSyncField1F4Offset     = 0x01F4; // uint32_t / float (пакет 30)
inline constexpr size_t    kLocalSyncField1F8Offset     = 0x01F8;
inline constexpr size_t    kLocalSyncField200Offset     = 0x0200;

// CRemotePlayer (sizeof = 0x288, проверено по 0x17B1A14, 0x17D281C, 0x17D3524, 0x17D4188)
inline constexpr size_t    kRemoteObjectSize            = 0x0288; // 648 Б
inline constexpr size_t    kRemotePedPtrOffset          = 0x0010; // CPed*
inline constexpr size_t    kRemoteNameOffset            = 0x0018; // char[49] (strncpy 0x31)
inline constexpr size_t    kRemoteNameMaxRead           = 49;
inline constexpr size_t    kRemoteIdOffset              = 0x004A; // uint16_t
inline constexpr size_t    kRemoteSyncBlockOffset       = 0x004C; // 0x4C..0x8A
inline constexpr size_t    kRemoteState120Offset        = 0x0120; // uint32_t / state
inline constexpr size_t    kRemoteField150Offset        = 0x0150; // pointer/value
inline constexpr size_t    kRemoteByte15BOffset         = 0x015B; // uint8_t
inline constexpr size_t    kRemoteU16_15COffset         = 0x015C; // uint16_t

inline constexpr uintptr_t kFnRemotePlayerApplySync     = 0x017D4188ULL;

// CPed (проверено по 0x17D4188, 0x178EE04, 0x178E904)
inline constexpr size_t    kPedPosXOffset               = 0x0038; // float X
inline constexpr size_t    kPedPosYOffset               = 0x003C; // float Y
inline constexpr size_t    kPedPosZOffset               = 0x0040; // float Z
inline constexpr size_t    kPedMatrixPtrOffset          = 0x0048; // void*
inline constexpr size_t    kPedFloat4B0Offset           = 0x04B0; // float -> u16 в пакетах 30/33 (HP-like)
inline constexpr size_t    kPedFloat4B8Offset           = 0x04B8; // float -> u16 в пакетах 30/33 (Armour-like)
inline constexpr size_t    kPedVehiclePtrOffset         = 0x0540; // CVehicle*
inline constexpr size_t    kPedInVehicleFlagOffset      = 0x0548; // uint8_t
} // namespace player

// ============================================================================
// Смещения VehiclePool, CNetVehicle, CVehicle (16.111.19050)
// ============================================================================
namespace vehicle {
inline constexpr size_t    kVehiclePoolSlotCount        = 2000;   // 0x07D0 (0..1999)
inline constexpr size_t    kPoolSlotStride              = 8;      // base + index * 8 -> CNetVehicle*

inline constexpr uintptr_t kFnVehiclePoolGetGameVeh     = 0x017DE01CULL;
inline constexpr uintptr_t kFnVehiclePoolGetNetVeh      = 0x017DE044ULL;
inline constexpr uintptr_t kFnVehiclePoolFindByGameVeh  = 0x017DEE34ULL;
inline constexpr uintptr_t kFnNetVehicleBranchField     = 0x017A4928ULL;

// CNetVehicle (слот VehiclePool)
inline constexpr size_t    kNetVehGameVehPtrOffset      = 0x0000; // CVehicle*
inline constexpr size_t    kNetVehSubStatePtrOffset     = 0x0008; // void* (+0x308 float, +0x30C flag)

// CVehicle (игровой объект транспорта, также в LocalPlayer+0x40)
inline constexpr size_t    kVehPosXOffset               = 0x0038; // float X
inline constexpr size_t    kVehPosYOffset               = 0x003C; // float Y
inline constexpr size_t    kVehPosZOffset               = 0x0040; // float Z
inline constexpr size_t    kVehMatrixPtrOffset          = 0x0048; // void*
inline constexpr size_t    kVehBackRefB8Offset          = 0x00B8; // void* (связь в apply path)
inline constexpr size_t    kVehMoveSpeedOffset          = 0x0180; // float[3] (0x180..0x188)
inline constexpr size_t    kVehTurnSpeedOffset          = 0x018C; // float[3] (0x18C..0x194)
inline constexpr size_t    kVehClassSubTypeOffset       = 0x0430; // uint32_t (ветвление 0x17A4928)
inline constexpr size_t    kVehIdOffset                 = 0x0434; // uint16_t (читается sender 31/33/34)
inline constexpr size_t    kVehSpecialFloat790Offset    = 0x0790; // float (для подтипов 11/12)
} // namespace vehicle

// ============================================================================
// RakClientInterface и RakNet::BitStream (подтверждено .dynsym и _ZTV9RakClient)
// ============================================================================
namespace raknet {
inline constexpr uintptr_t kRakClientInterfaceVtableVA  = 0x02140BF0ULL;

// Слоты виртуальной таблицы RakClientInterface (база g_pRakClient = RakClient + 0x690)
inline constexpr size_t    kVtableSlotSendRaw           = 0x0030;
inline constexpr size_t    kVtableSlotSendBitStream     = 0x0038; // Send(BitStream*, prio, rel, chan)
inline constexpr size_t    kVtableSlotGetAveragePing    = 0x0068; // int GetAveragePing()
inline constexpr size_t    kVtableSlotGetLastPing       = 0x0070; // int GetLastPing() const
inline constexpr size_t    kVtableSlotGetLowestPing     = 0x0078; // int GetLowestPing() const
inline constexpr size_t    kVtableSlotIsConnected       = 0x0090; // bool IsConnected() const
inline constexpr size_t    kVtableSlotRpcRaw            = 0x00A8;
inline constexpr size_t    kVtableSlotRpcBitStream      = 0x00B0; // RPC(int id, BitStream*, prio, rel, chan)
inline constexpr size_t    kVtableSlotGetServerId       = 0x00C8; // PlayerID GetServerID() const
inline constexpr size_t    kVtableSlotGetMtuSize        = 0x00F0; // int GetMTUSize() const
inline constexpr size_t    kVtableSlotGetStatistics     = 0x0100; // RakNetStatisticsStruct* GetStatistics() const

// VA реализаций thunk-методов RakClientInterface (для пассивного read-only логгера)
inline constexpr uintptr_t kFnRakClientSendThunkVA      = 0x01A513B8ULL;
inline constexpr uintptr_t kFnRakClientRpcThunkVA       = 0x01A51AC8ULL;

// Смещения RakNet::BitStream (проверено по _ZNK6RakNet9BitStream8CopyDataEPPh 0x01A50D18)
inline constexpr size_t    kBitStreamBitsUsedOffset     = 0x0000; // int32_t numberOfBitsUsed
inline constexpr size_t    kBitStreamBitsAllocOffset    = 0x0004; // int32_t numberOfBitsAllocated
inline constexpr size_t    kBitStreamReadOffset         = 0x0008; // int32_t readOffset
inline constexpr size_t    kBitStreamDataPtrOffset      = 0x0010; // uint8_t* data
} // namespace raknet

} // namespace br::offsets

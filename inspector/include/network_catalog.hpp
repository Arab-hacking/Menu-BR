#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace br::net {

struct PacketCatalogEntry {
    uint8_t     packet_id;
    int         logical_size_bytes; // -1 = переменная длина
    const char* send_vas_csv;
    const char* serializer_va;
    const char* layout_summary;
    const char* role_and_confidence;
};

struct RpcCatalogEntry {
    int         rpc_id;
    int         strict_callsite_count;
    int         wide_callsite_count;
    int         unique_func_count;
    bool        is_strict_candidate;
    const char* sample_callsites_va;
    const char* confidence_note;
};

enum class NetEventKind : uint8_t {
    RawPacket = 0,
    RpcCall   = 1
};

struct CapturedNetEvent {
    uint64_t               seq_no        = 0;
    uint64_t               timestamp_ms  = 0;
    NetEventKind           kind          = NetEventKind::RawPacket;
    int                    id            = 0;   // packet_id (u8) или rpc_id (int)
    int                    total_bits    = 0;
    int                    total_bytes   = 0;
    int                    priority      = 0;
    int                    reliability   = 0;
    int                    channel       = 0;
    uintptr_t              caller_va     = 0;   // LR - module_base (для сверки с каталогом)
    size_t                 captured_len  = 0;
    std::array<uint8_t, 96> preview_bytes{};
};

struct CounterSnapshotEntry {
    int      id          = 0;
    uint64_t count       = 0;
    uint64_t total_bytes = 0;
    uint64_t last_seen_ms = 0;
};

class NetworkCatalogAndObserver {
public:
    // Статический каталог из ANALYSIS_16.111.19050
    static const std::vector<PacketCatalogEntry>& GetPacketCatalog();
    static const std::vector<RpcCatalogEntry>&    GetRpcCatalog();

    static const PacketCatalogEntry* FindPacketInfo(uint8_t packet_id) noexcept;
    static const RpcCatalogEntry*    FindRpcInfo(int rpc_id) noexcept;

    // Разбор подтверждённых числовых полей пакета по спецификации 16.111.19050 (Read-Only)
    static std::string DissectPacketPayload(uint8_t packet_id,
                                            const uint8_t* data,
                                            size_t len,
                                            int total_bits);

    // Управление пассивным Read-Only наблюдателем (перехват 0x01A513B8 / 0x01A51AC8 без модификации трафика)
    static bool InstallPassiveObserver(uintptr_t module_base);
    static bool IsObserverInstalled() noexcept;

    static void SetCapturePaused(bool paused) noexcept;
    static bool IsCapturePaused() noexcept;
    static void ClearCapturedEvents() noexcept;

    // Запись события вручную (используется также в модульных тестах)
    static void RecordEventForTestOrHook(NetEventKind kind,
                                         int id,
                                         const uint8_t* bytes,
                                         int total_bits,
                                         int priority,
                                         int reliability,
                                         int channel,
                                         uintptr_t caller_va) noexcept;

    static void GetCapturedSnapshot(std::vector<CapturedNetEvent>& out_events,
                                    std::vector<CounterSnapshotEntry>& out_pkt_counters,
                                    std::vector<CounterSnapshotEntry>& out_rpc_counters,
                                    uint64_t& out_total_packets,
                                    uint64_t& out_total_rpcs);
};

} // namespace br::net

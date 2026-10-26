#include "network_catalog.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "arm64_hook.hpp"
#include "br_offsets_16_111_19050.hpp"
#include "safe_memory.hpp"

namespace br::net {

namespace {

constexpr size_t kRingBufferCapacity = 256;

uintptr_t                  g_module_base = 0;
std::atomic<bool>          g_installed{false};
std::atomic<bool>          g_paused{false};

std::mutex                 g_ring_mutex;
std::array<CapturedNetEvent, kRingBufferCapacity> g_ring{};
size_t                     g_ring_head   = 0;
size_t                     g_ring_count  = 0;
uint64_t                   g_seq_counter = 0;
uint64_t                   g_total_pkts  = 0;
uint64_t                   g_total_rpcs  = 0;

std::unordered_map<int, CounterSnapshotEntry> g_pkt_stats;
std::unordered_map<int, CounterSnapshotEntry> g_rpc_stats;

using FnRakSendThunk = bool (*)(void* self, void* bitstream, int prio, int rel, char chan);
using FnRakRpcThunk  = bool (*)(void* self, int rpc_id, void* bitstream, int prio, int rel, char chan);

FnRakSendThunk g_orig_rak_send = nullptr;
FnRakRpcThunk  g_orig_rak_rpc  = nullptr;

uint64_t NowMonotonicMs() noexcept {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

template <typename T>
T ReadLE(const uint8_t* p) noexcept {
    T val{};
    std::memcpy(&val, p, sizeof(T));
    return val;
}

// Пассивный Read-Only перехватчик RakClient::Send(BitStream*, prio, rel, chan) [VA 0x01A513B8]
// НЕ изменяет и НЕ блокирует пакеты — только копирует первые 96 байт для отображения в инспекторе.
bool Detour_RakClientSendThunk(void* self, void* bitstream, int prio, int rel, char chan) {
    if (!g_paused.load(std::memory_order_relaxed) && bitstream != nullptr) {
        const uintptr_t bs_addr = reinterpret_cast<uintptr_t>(bitstream);
        int32_t bits_used = 0;
        uintptr_t data_ptr = 0;
        if (mem::SafeMemory::Read(bs_addr + offsets::raknet::kBitStreamBitsUsedOffset, bits_used) &&
            bits_used >= 8 &&
            mem::SafeMemory::Read(bs_addr + offsets::raknet::kBitStreamDataPtrOffset, data_ptr) &&
            mem::SafeMemory::IsPlausibleAddress(data_ptr)) {
            const size_t byte_len = static_cast<size_t>((bits_used + 7) / 8);
            const size_t copy_len = std::min<size_t>(byte_len, 96);
            uint8_t local_buf[96]{};
            if (mem::SafeMemory::ReadBytes(data_ptr, local_buf, copy_len)) {
                const uintptr_t ret_addr =
                    reinterpret_cast<uintptr_t>(__builtin_return_address(0));
                const uintptr_t caller_va =
                    (g_module_base != 0 && ret_addr >= g_module_base) ? (ret_addr - g_module_base) : ret_addr;

                NetworkCatalogAndObserver::RecordEventForTestOrHook(
                    NetEventKind::RawPacket,
                    static_cast<int>(local_buf[0]),
                    local_buf,
                    bits_used,
                    prio,
                    rel,
                    static_cast<int>(chan),
                    caller_va);
            }
        }
    }

    return g_orig_rak_send ? g_orig_rak_send(self, bitstream, prio, rel, chan) : false;
}

// Пассивный Read-Only перехватчик RakClient::RPC(int id, BitStream*, prio, rel, chan) [VA 0x01A51AC8]
bool Detour_RakClientRpcThunk(void* self, int rpc_id, void* bitstream, int prio, int rel, char chan) {
    if (!g_paused.load(std::memory_order_relaxed)) {
        int32_t bits_used = 0;
        uint8_t local_buf[96]{};
        const uint8_t* ptr_to_pass = nullptr;

        if (bitstream != nullptr) {
            const uintptr_t bs_addr = reinterpret_cast<uintptr_t>(bitstream);
            uintptr_t data_ptr = 0;
            if (mem::SafeMemory::Read(bs_addr + offsets::raknet::kBitStreamBitsUsedOffset, bits_used) &&
                bits_used > 0 &&
                mem::SafeMemory::Read(bs_addr + offsets::raknet::kBitStreamDataPtrOffset, data_ptr) &&
                mem::SafeMemory::IsPlausibleAddress(data_ptr)) {
                const size_t byte_len = static_cast<size_t>((bits_used + 7) / 8);
                const size_t copy_len = std::min<size_t>(byte_len, 96);
                if (mem::SafeMemory::ReadBytes(data_ptr, local_buf, copy_len)) {
                    ptr_to_pass = local_buf;
                }
            }
        }

        const uintptr_t ret_addr =
            reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        const uintptr_t caller_va =
            (g_module_base != 0 && ret_addr >= g_module_base) ? (ret_addr - g_module_base) : ret_addr;

        NetworkCatalogAndObserver::RecordEventForTestOrHook(
            NetEventKind::RpcCall,
            rpc_id,
            ptr_to_pass,
            bits_used,
            prio,
            rel,
            static_cast<int>(chan),
            caller_va);
    }

    return g_orig_rak_rpc ? g_orig_rak_rpc(self, rpc_id, bitstream, prio, rel, chan) : false;
}

} // namespace

const std::vector<PacketCatalogEntry>& NetworkCatalogAndObserver::GetPacketCatalog() {
    static const std::vector<PacketCatalogEntry> kCatalog = {
        {27, -1, "0x1470300; 0x150C660; 0x156025C; 0x179C4F8", "0x179C110 / inline",
         "ID (u8) + подтип u16 (10 / 13 / 24 или рег.) + длина + строка",
         "Динамические строковые запросы/команды (4 callsite)"},
        {29, 41, "0x17945F8", "0x1794390",
         "ID (u8) + 40 байт тела",
         "Событие с проверкой invalid victim type (точное имя не установлено)"},
        {30, 73, "0x178F068", "0x17DB1EC (из 0x178EE04)",
         "ID + 3*u16 + 96b + 4*f32 + 2*u16 + 2*u8 + 2*96b + u16 + u32 + 2*u8",
         "Основная state/sync структура LocalPlayer (73 Б)"},
        {31, 55, "0x178C294", "inline в 0x178C100",
         "ID + u16 (veh+0x434) + 96b (pos) + 4*f32 (quat) + 2*96b (move/turn)",
         "LocalPlayer sync helper (55 Б)"},
        {32, 68, "0x178F828", "0x17DC1CC (из 0x178F488)",
         "ID + u16 + u8 + 5*96b + f32",
         "Многовекторное/vehicle-связанное состояние по изменению (68 Б)"},
        {33, 27, "0x178EAD4", "0x17DC41C (из 0x178E904)",
         "ID + 26 байт структуры (сравнение с кэшем +0xF0..+0x108)",
         "Дифференциальное обновление кэшированного состояния игрока/авто (27 Б)"},
        {34, 66, "0x178C040", "0x17DBAF0 (из 0x178BDB0)",
         "ID + 4*u16 + 4*f32 + 2*96b + f32 + 2*u16 + 3*u8 + 3*u16",
         "Подтверждённый LocalPlayer Vehicle Sync (66 Б)"},
        {35, -1, "0x178D540", "0x178D398",
         "ID + 2*u16(0xFFFF) + записи изменённых слотов (slot u8, val u8, u16)",
         "Дифференциальный пакет по слотам (вероятно оружие/инвентарь)"},
        {37, 19, "0x178D058", "0x178CF98",
         "ID + 18 байт (интервал отправки >= 101 мс)",
         "Периодическая структура с rate-limit 101 мс (19 Б)"},
        {38, 32, "0x178E844", "0x17DC054 (из 0x178E5B4)",
         "ID + 31 байт (квантизованные/битовые поля + byteWeaponState)",
         "LocalPlayer sync с проверкой aimSync.byteWeaponState (32 Б)"},
        {39, 57, "0x178C3F8", "0x178C300",
         "ID + 56 байт",
         "Локальное транспортное / крановое состояние (57 Б)"}
    };
    return kCatalog;
}

const std::vector<RpcCatalogEntry>& NetworkCatalogAndObserver::GetRpcCatalog() {
    static const std::vector<RpcCatalogEntry> kRpcCatalog = {
        {67,  0, 1,  1, false, "0x1799608",                      "Широкая эвристика (ниже уверенность)"},
        {69,  0, 1,  1, false, "0x1799484",                      "Широкая эвристика (ниже уверенность)"},
        {314, 1, 1,  1, true,  "0x178CE20",                      "Строгий кандидат (vtable +0xB0)"},
        {320, 1, 1,  1, true,  "0x17BD730",                      "Строгий кандидат (vtable +0xB0)"},
        {324, 1, 1,  1, true,  "0x17AF32C",                      "Строгий кандидат (vtable +0xB0)"},
        {342, 1, 1,  1, true,  "0x199DE0C",                      "Строгий кандидат (vtable +0xB0)"},
        {355, 1, 1,  1, true,  "0x178FE5C",                      "Строгий кандидат (vtable +0xB0)"},
        {356, 1, 1,  1, true,  "0x178FD04",                      "Строгий кандидат (vtable +0xB0)"},
        {357, 1, 1,  1, true,  "0x178CEA8",                      "Строгий кандидат (vtable +0xB0)"},
        {361, 1, 1,  1, true,  "0x17B40C0",                      "Строгий кандидат (vtable +0xB0)"},
        {367, 0, 1,  1, false, "0x18039A0",                      "Широкая эвристика (ниже уверенность)"},
        {371, 1, 1,  1, true,  "0x178C738",                      "Строгий кандидат (vtable +0xB0)"},
        {374, 1, 1,  1, true,  "0x179CA08",                      "Строгий кандидат (vtable +0xB0)"},
        {378, 1, 1,  1, true,  "0x179CDB4",                      "Строгий кандидат (vtable +0xB0)"},
        {382, 0, 1,  2, false, "0x178F330",                      "Широкая эвристика (ниже уверенность)"},
        {396, 0, 2,  2, false, "0x1776BB0; 0x1776C9C",           "Широкая эвристика (ниже уверенность)"},
        {403, 1, 1,  1, true,  "0x194EFC0",                      "Строгий кандидат (vtable +0xB0)"},
        {409, 0, 1,  1, false, "0x178F42C",                      "Широкая эвристика (ниже уверенность)"},
        {413, 18, 42, 34, true, "0xE41B7C; 0x1387924; 0x179CF8C (+15 strict)", "Самый частый строгий RPC (43 strict / 89 raw)"},
        {414, 1, 1,  1, true,  "0x179CEA0",                      "Строгий кандидат (vtable +0xB0)"},
        {423, 2, 2,  2, true,  "0x0E4D8BC; 0x140D91C",           "Строгий кандидат (2 callsite)"},
        {436, 1, 1,  1, true,  "0x17AFE64",                      "Строгий кандидат (vtable +0xB0)"},
        {437, 1, 1,  1, true,  "0x18B7940",                      "Строгий кандидат (vtable +0xB0)"},
        {441, 1, 1,  1, true,  "0x178B86C",                      "Строгий кандидат (LocalPlayer spawn/init путь)"},
        {451, 0, 1,  1, false, "0x1794874",                      "Широкая эвристика (ниже уверенность)"},
        {454, 0, 1,  1, false, "0x0DFD7DC",                      "Широкая эвристика (ниже уверенность)"}
    };
    return kRpcCatalog;
}

const PacketCatalogEntry* NetworkCatalogAndObserver::FindPacketInfo(uint8_t packet_id) noexcept {
    for (const auto& entry : GetPacketCatalog()) {
        if (entry.packet_id == packet_id) {
            return &entry;
        }
    }
    return nullptr;
}

const RpcCatalogEntry* NetworkCatalogAndObserver::FindRpcInfo(int rpc_id) noexcept {
    for (const auto& entry : GetRpcCatalog()) {
        if (entry.rpc_id == rpc_id) {
            return &entry;
        }
    }
    return nullptr;
}

std::string NetworkCatalogAndObserver::DissectPacketPayload(uint8_t packet_id,
                                                            const uint8_t* data,
                                                            size_t len,
                                                            int total_bits) {
    char buf[512]{};
    if (!data || len == 0) {
        std::snprintf(buf, sizeof(buf), "Пустой буфер (%d бит)", total_bits);
        return std::string(buf);
    }

    switch (packet_id) {
        case 27: {
            if (len >= 3) {
                const uint16_t subtype = ReadLE<uint16_t>(data + 1);
                std::snprintf(buf, sizeof(buf),
                              "ID=27 (0x1B) | Подтип u16=%u | Всего %zu Б (%d бит)",
                              subtype, len, total_bits);
            } else {
                std::snprintf(buf, sizeof(buf), "ID=27 (0x1B) | Короткий кадр (%zu Б)", len);
            }
            break;
        }
        case 30: {
            if (len >= 35) {
                const uint16_t u0 = ReadLE<uint16_t>(data + 1);
                const uint16_t u1 = ReadLE<uint16_t>(data + 3);
                const uint16_t u2 = ReadLE<uint16_t>(data + 5);
                const float px    = ReadLE<float>(data + 7);
                const float py    = ReadLE<float>(data + 11);
                const float pz    = ReadLE<float>(data + 15);
                std::snprintf(buf, sizeof(buf),
                              "ID=30 LocalSync (73 Б ож., факт %zu Б) | u16=[%u, %u, %u] | Vec96=(%.2f, %.2f, %.2f)",
                              len, u0, u1, u2, px, py, pz);
            } else {
                std::snprintf(buf, sizeof(buf), "ID=30 LocalSync | %zu Б (%d бит)", len, total_bits);
            }
            break;
        }
        case 31: {
            if (len >= 31) {
                const uint16_t veh_id = ReadLE<uint16_t>(data + 1);
                const float px        = ReadLE<float>(data + 3);
                const float py        = ReadLE<float>(data + 7);
                const float pz        = ReadLE<float>(data + 11);
                std::snprintf(buf, sizeof(buf),
                              "ID=31 SyncHelper (55 Б ож., факт %zu Б) | vehID=%u | Pos=(%.2f, %.2f, %.2f)",
                              len, veh_id, px, py, pz);
            } else {
                std::snprintf(buf, sizeof(buf), "ID=31 SyncHelper | %zu Б", len);
            }
            break;
        }
        case 34: {
            if (len >= 37) {
                const uint16_t veh_id = ReadLE<uint16_t>(data + 1);
                const uint16_t k1     = ReadLE<uint16_t>(data + 3);
                const uint16_t k2     = ReadLE<uint16_t>(data + 5);
                const uint16_t k3     = ReadLE<uint16_t>(data + 7);
                const float vx        = ReadLE<float>(data + 25);
                const float vy        = ReadLE<float>(data + 29);
                const float vz        = ReadLE<float>(data + 33);
                std::snprintf(buf, sizeof(buf),
                              "ID=34 VehicleSync (66 Б ож., факт %zu Б) | vehID=%u keys=[%u,%u,%u] | Vec96=(%.2f, %.2f, %.2f)",
                              len, veh_id, k1, k2, k3, vx, vy, vz);
            } else {
                std::snprintf(buf, sizeof(buf), "ID=34 VehicleSync | %zu Б", len);
            }
            break;
        }
        case 35: {
            if (len >= 5) {
                const uint16_t m0 = ReadLE<uint16_t>(data + 1);
                const uint16_t m1 = ReadLE<uint16_t>(data + 3);
                std::snprintf(buf, sizeof(buf),
                              "ID=35 SlotDiff | header=[0x%04X, 0x%04X] | tail=%zu Б",
                              m0, m1, len - 5);
            } else {
                std::snprintf(buf, sizeof(buf), "ID=35 SlotDiff | %zu Б", len);
            }
            break;
        }
        default: {
            const PacketCatalogEntry* info = FindPacketInfo(packet_id);
            if (info && info->logical_size_bytes > 0) {
                std::snprintf(buf, sizeof(buf),
                              "ID=%u (0x%02X) | Ожидаемый размер %d Б, факт %zu Б (%d бит)",
                              packet_id, packet_id, info->logical_size_bytes, len, total_bits);
            } else {
                std::snprintf(buf, sizeof(buf),
                              "ID=%u (0x%02X) | Размер %zu Б (%d бит)",
                              packet_id, packet_id, len, total_bits);
            }
            break;
        }
    }
    return std::string(buf);
}

bool NetworkCatalogAndObserver::InstallPassiveObserver(uintptr_t module_base) {
    if (g_installed.load()) {
        return true;
    }
    if (module_base == 0) {
        return false;
    }
    g_module_base = module_base;

    void* orig_send = nullptr;
    void* orig_rpc  = nullptr;

    const bool ok_send = hook::Arm64Hook::Install(
        reinterpret_cast<void*>(module_base + offsets::raknet::kFnRakClientSendThunkVA),
        reinterpret_cast<void*>(&Detour_RakClientSendThunk),
        &orig_send);

    const bool ok_rpc = hook::Arm64Hook::Install(
        reinterpret_cast<void*>(module_base + offsets::raknet::kFnRakClientRpcThunkVA),
        reinterpret_cast<void*>(&Detour_RakClientRpcThunk),
        &orig_rpc);

    if (ok_send) {
        g_orig_rak_send = reinterpret_cast<FnRakSendThunk>(orig_send);
    }
    if (ok_rpc) {
        g_orig_rak_rpc = reinterpret_cast<FnRakRpcThunk>(orig_rpc);
    }

    if (ok_send || ok_rpc) {
        g_installed.store(true);
        return true;
    }
    return false;
}

bool NetworkCatalogAndObserver::IsObserverInstalled() noexcept {
    return g_installed.load(std::memory_order_relaxed);
}

void NetworkCatalogAndObserver::SetCapturePaused(bool paused) noexcept {
    g_paused.store(paused, std::memory_order_relaxed);
}

bool NetworkCatalogAndObserver::IsCapturePaused() noexcept {
    return g_paused.load(std::memory_order_relaxed);
}

void NetworkCatalogAndObserver::ClearCapturedEvents() noexcept {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    g_ring_head   = 0;
    g_ring_count  = 0;
    g_total_pkts  = 0;
    g_total_rpcs  = 0;
    g_pkt_stats.clear();
    g_rpc_stats.clear();
}

void NetworkCatalogAndObserver::RecordEventForTestOrHook(NetEventKind kind,
                                                         int id,
                                                         const uint8_t* bytes,
                                                         int total_bits,
                                                         int priority,
                                                         int reliability,
                                                         int channel,
                                                         uintptr_t caller_va) noexcept {
    std::lock_guard<std::mutex> lock(g_ring_mutex);

    const uint64_t now_ms    = NowMonotonicMs();
    const int      byte_size = (total_bits > 0) ? ((total_bits + 7) / 8) : 0;

    CapturedNetEvent ev{};
    ev.seq_no       = ++g_seq_counter;
    ev.timestamp_ms = now_ms;
    ev.kind         = kind;
    ev.id           = id;
    ev.total_bits   = total_bits;
    ev.total_bytes  = byte_size;
    ev.priority     = priority;
    ev.reliability  = reliability;
    ev.channel      = channel;
    ev.caller_va    = caller_va;

    if (bytes && byte_size > 0) {
        ev.captured_len = std::min<size_t>(static_cast<size_t>(byte_size), ev.preview_bytes.size());
        std::memcpy(ev.preview_bytes.data(), bytes, ev.captured_len);
    }

    g_ring[g_ring_head] = ev;
    g_ring_head = (g_ring_head + 1) % kRingBufferCapacity;
    if (g_ring_count < kRingBufferCapacity) {
        ++g_ring_count;
    }

    if (kind == NetEventKind::RawPacket) {
        ++g_total_pkts;
        auto& st        = g_pkt_stats[id];
        st.id           = id;
        st.count       += 1;
        st.total_bytes += static_cast<uint64_t>(byte_size);
        st.last_seen_ms = now_ms;
    } else {
        ++g_total_rpcs;
        auto& st        = g_rpc_stats[id];
        st.id           = id;
        st.count       += 1;
        st.total_bytes += static_cast<uint64_t>(byte_size);
        st.last_seen_ms = now_ms;
    }
}

void NetworkCatalogAndObserver::GetCapturedSnapshot(
    std::vector<CapturedNetEvent>& out_events,
    std::vector<CounterSnapshotEntry>& out_pkt_counters,
    std::vector<CounterSnapshotEntry>& out_rpc_counters,
    uint64_t& out_total_packets,
    uint64_t& out_total_rpcs) {

    std::lock_guard<std::mutex> lock(g_ring_mutex);

    out_total_packets = g_total_pkts;
    out_total_rpcs    = g_total_rpcs;

    out_events.clear();
    out_events.reserve(g_ring_count);
    // Выдаём от новых к старым
    for (size_t i = 0; i < g_ring_count; ++i) {
        const size_t idx = (g_ring_head + kRingBufferCapacity - 1 - i) % kRingBufferCapacity;
        out_events.push_back(g_ring[idx]);
    }

    out_pkt_counters.clear();
    out_pkt_counters.reserve(g_pkt_stats.size());
    for (const auto& kv : g_pkt_stats) {
        out_pkt_counters.push_back(kv.second);
    }
    std::sort(out_pkt_counters.begin(), out_pkt_counters.end(),
              [](const CounterSnapshotEntry& a, const CounterSnapshotEntry& b) { return a.id < b.id; });

    out_rpc_counters.clear();
    out_rpc_counters.reserve(g_rpc_stats.size());
    for (const auto& kv : g_rpc_stats) {
        out_rpc_counters.push_back(kv.second);
    }
    std::sort(out_rpc_counters.begin(), out_rpc_counters.end(),
              [](const CounterSnapshotEntry& a, const CounterSnapshotEntry& b) { return a.id < b.id; });
}

} // namespace br::net

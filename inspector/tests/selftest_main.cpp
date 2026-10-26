#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "imgui.h"
#include "arm64_disasm.hpp"
#include "arm64_hook.hpp"
#include "br_offsets_16_111_19050.hpp"
#include "game_reader.hpp"
#include "inspector_menu.hpp"
#include "internal_imgui_bridge.hpp"
#include "network_catalog.hpp"
#include "safe_memory.hpp"

namespace {

struct MockElfBuffer {
    uintptr_t            fake_base = 0x7000000000ULL;
    std::vector<uint8_t> elf_head;
};

bool MockMemoryReader(uintptr_t addr, void* out_buf, size_t size, void* user_data) {
    auto* ctx = static_cast<MockElfBuffer*>(user_data);
    if (!ctx) return false;
    if (addr >= ctx->fake_base && (addr - ctx->fake_base) + size <= ctx->elf_head.size()) {
        std::memcpy(out_buf, ctx->elf_head.data() + (addr - ctx->fake_base), size);
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("=== BR 16.111.19050 ImGui Inspector Self-Test ===\n");

    // 1. Проверка таблиц пакетов и RPC
    const auto& pkts = br::net::NetworkCatalogAndObserver::GetPacketCatalog();
    const auto& rpcs = br::net::NetworkCatalogAndObserver::GetRpcCatalog();
    assert(pkts.size() == 11);
    assert(rpcs.size() == 26);
    assert(br::net::NetworkCatalogAndObserver::FindPacketInfo(30)->logical_size_bytes == 73);
    assert(br::net::NetworkCatalogAndObserver::FindPacketInfo(34)->logical_size_bytes == 66);
    assert(br::net::NetworkCatalogAndObserver::FindRpcInfo(413)->is_strict_candidate);
    std::printf("[OK] Каталоги пакетов (11 ID) и RPC (26 ID) проверены.\n");

    // 2. Если передан путь к реальной libblackrussia-client.so — проверяем Build-ID и релокатор AArch64 прологов
    MockElfBuffer mock_elf{};
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        assert(f.is_open());

        mock_elf.elf_head.resize(4096);
        f.read(reinterpret_cast<char*>(mock_elf.elf_head.data()), 4096);
        br::mem::SafeMemory::SetCustomReader(&MockMemoryReader, &mock_elf);

        br::ui::InternalImGuiRuntimeTable rt{};
        const bool ok_resolve = br::ui::InternalImGuiBridge::ResolveSymbols(mock_elf.fake_base, rt, true);
        assert(ok_resolve && rt.build_id_verified);
        std::printf("[OK] ELF GNU Build-ID из реального .so верифицирован: %s\n",
                    br::ui::InternalImGuiBridge::FormatBuildIdHex(rt.observed_build_id).c_str());

        // Проверим генерацию трамплинов для реальных прологов из .so
        struct HookCheckTarget {
            const char* name;
            uintptr_t   va;
        };
        const HookCheckTarget targets[] = {
            {"JNILib::step (GLThread)",         br::offsets::jni_bridge::kJni_JNILib_step},
            {"JNILib::multiTouchEvent",         br::offsets::jni_bridge::kJni_JNILib_multiTouchEvent},
            {"ImGuiWrapper::BeginFrame",        br::offsets::imgui_va::kWrapper_BeginFrame},
            {"ImGuiWrapper::EndFrameAndRender", br::offsets::imgui_va::kWrapper_EndFrameAndRender},
            {"RakClient::Send Thunk",           br::offsets::raknet::kFnRakClientSendThunkVA},
            {"RakClient::RPC Thunk",            br::offsets::raknet::kFnRakClientRpcThunkVA},
        };

        for (const auto& t : targets) {
            uint32_t insns[4]{};
            f.seekg(static_cast<std::streamoff>(t.va));
            f.read(reinterpret_cast<char*>(insns), sizeof(insns));

            std::vector<uint32_t> tramp;
            const bool ok_tramp = br::hook::Arm64Hook::BuildRelocatedTrampoline(
                insns, mock_elf.fake_base + t.va, tramp);
            assert(ok_tramp && tramp.size() >= 8);
            std::printf("[OK] AArch64 трамплин для %-32s (VA 0x%07lx) -> %zu инструкций\n",
                        t.name, static_cast<unsigned long>(t.va), tramp.size());
        }
    }

    // 3. Полноценный headless прогон Dear ImGui 1.90.9 по всем 8 разделам меню (7 инспектора + MOD)
    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io       = ImGui::GetIO();
    io.IniFilename    = nullptr;
    io.LogFilename    = nullptr;
    io.DisplaySize    = ImVec2(1920.0f, 1080.0f);
    io.DeltaTime      = 1.0f / 60.0f;

    unsigned char* pixels = nullptr;
    int tex_w = 0, tex_h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &tex_w, &tex_h);
    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(1)));

    // Подготовим синтетический снимок игрового мира и пакетов
    br::inspector::WorldDiagnosticSnapshot snap{};
    snap.module_base           = mock_elf.fake_base;
    snap.player_pool_ptr       = 0x7100100000ULL;
    snap.vehicle_pool_ptr      = 0x7100200000ULL;
    snap.object_pool_ptr       = 0x7100300000ULL;
    snap.active_object_slots   = 342;
    snap.rakclient.rakclient_ptr        = 0x7100050690ULL;
    snap.rakclient.vtable_ptr           = mock_elf.fake_base + br::offsets::raknet::kRakClientInterfaceVtableVA;
    snap.rakclient.vtable_verified      = true;
    snap.rakclient.queried_live_methods = true;
    snap.rakclient.is_connected         = true;
    snap.rakclient.last_ping            = 42;
    snap.rakclient.average_ping         = 45;
    snap.rakclient.lowest_ping          = 38;
    snap.rakclient.mtu_size             = 1492;

    snap.local_player.valid               = true;
    snap.local_player.local_player_ptr    = 0x7100400000ULL;
    snap.local_player.player_id           = 77;
    snap.local_player.name                = "Test_Researcher";
    snap.local_player.ped_ptr             = 0x7100500000ULL;
    snap.local_player.ped_valid           = true;
    snap.local_player.pos[0]              = 1245.5f;
    snap.local_player.pos[1]              = -890.25f;
    snap.local_player.pos[2]              = 14.8f;
    snap.local_player.ped_float_4b0       = 100.0f;
    snap.local_player.ped_float_4b8       = 50.0f;
    snap.local_player.local_vehicle_ptr   = 0x7100600000ULL;
    snap.local_player.local_vehicle_id    = 412;
    snap.local_player.local_vehicle_subtype = 11;

    for (uint16_t i = 0; i < 15; ++i) {
        br::inspector::RemotePlayerEntry rp{};
        rp.slot_id           = i;
        rp.reported_id       = i;
        rp.remote_ptr        = 0x7100700000ULL + i * 0x288ULL;
        rp.name              = "Player_" + std::to_string(i);
        rp.ped_ptr           = 0x7100800000ULL + i * 0x600ULL;
        rp.ped_valid         = (i % 3 != 0);
        rp.pos[0]            = 1250.0f + i * 5.0f;
        rp.pos[1]            = -885.0f;
        rp.pos[2]            = 14.8f;
        rp.distance_to_local = 5.0f * (i + 1);
        rp.ped_float_4b0     = 100.0f;
        rp.ped_in_vehicle    = (i % 2 == 0) ? 1 : 0;
        rp.pool_aux_a        = 10 + i;
        rp.pool_aux_b        = 48 + i;
        snap.remote_players.push_back(rp);
    }

    for (uint16_t i = 0; i < 10; ++i) {
        br::inspector::VehicleEntry ve{};
        ve.slot_index         = i;
        ve.net_vehicle_ptr    = 0x7100900000ULL + i * 0x40ULL;
        ve.game_vehicle_ptr   = 0x7100600000ULL + i * 0x800ULL;
        ve.game_vehicle_valid = true;
        ve.vehicle_id         = static_cast<uint16_t>(412 + i);
        ve.class_subtype      = (i == 0) ? 11 : 4;
        ve.pos[0]             = 1245.5f + i * 10.0f;
        ve.pos[1]             = -890.25f;
        ve.pos[2]             = 14.8f;
        ve.distance_to_local  = i * 10.0f;
        ve.speed_magnitude    = 18.4f;
        ve.is_local_vehicle   = (i == 0);
        snap.vehicles.push_back(ve);
    }

    uint8_t dummy_pkt30[73]{30, 1, 0, 2, 0, 3, 0};
    br::net::NetworkCatalogAndObserver::RecordEventForTestOrHook(
        br::net::NetEventKind::RawPacket, 30, dummy_pkt30, 73 * 8, 1, 1, 0, 0x0178F068ULL);
    uint8_t dummy_pkt34[66]{34, 0x9C, 0x01};
    br::net::NetworkCatalogAndObserver::RecordEventForTestOrHook(
        br::net::NetEventKind::RawPacket, 34, dummy_pkt34, 66 * 8, 1, 1, 0, 0x0178C040ULL);
    uint8_t dummy_rpc413[16]{0xAA, 0xBB, 0xCC, 0xDD};
    br::net::NetworkCatalogAndObserver::RecordEventForTestOrHook(
        br::net::NetEventKind::RpcCall, 413, dummy_rpc413, 128, 1, 2, 0, 0x0179CF8CULL);

    ImFont* loaded_fonts[6] = {io.Fonts->Fonts[0], nullptr, nullptr, nullptr, nullptr, nullptr};
    float font_scale        = 22.5f;
    uint8_t frame_active    = 1;

    br::ui::InternalImGuiRuntimeTable rt_table{};
    rt_table.module_base       = mock_elf.fake_base;
    rt_table.build_id_verified = true;
    std::memcpy(rt_table.observed_build_id, br::offsets::kExpectedGnuBuildId, 20);
    rt_table.p_gimgui          = &ctx;
    rt_table.p_font_scale      = &font_scale;
    rt_table.p_loaded_fonts    = loaded_fonts;
    rt_table.p_frame_active    = &frame_active;

    br::ui::InspectorMenu menu;
    menu.SetExternalSnapshotForTest(snap);
    menu.GetConfig().show_mini_hud = true;
    menu.AddWatchAddress(mock_elf.fake_base + br::offsets::kGnuBuildIdDescVA,
                         br::ui::WatchValueType::Uint32, "BuildID Head");

    // Проверим встроенный декодер AArch64 инструкций
    char asm_buf[96]{};
    br::disasm::FormatArm64Instruction(0xD65F03C0U, 0x1901D44ULL, asm_buf, sizeof(asm_buf));
    assert(std::strcmp(asm_buf, "ret") == 0);
    br::disasm::FormatArm64Instruction(0x94000010U, 0x1901D44ULL, asm_buf, sizeof(asm_buf));
    assert(std::strstr(asm_buf, "bl ") != nullptr);
    std::printf("[OK] Встроенный декодер инструкций AArch64 проверен (%s).\n", asm_buf);

    for (int tab = 0; tab < 8; ++tab) {
        menu.GetConfig().theme = static_cast<br::ui::InspectorThemeId>(tab % 4);
        menu.ForceSelectTabForTest(tab);
        ImGui::NewFrame();
        menu.Render(rt_table);
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();
        assert(dd != nullptr && dd->Valid && dd->TotalVtxCount > 0 && dd->TotalIdxCount > 0);
        std::printf("[OK] Отрисован раздел #%d (Wide 1920x1080) -> CmdLists=%d, Vertices=%d, Indices=%d\n",
                    tab + 1, dd->CmdListsCount, dd->TotalVtxCount, dd->TotalIdxCount);
    }

    // Проверим адаптивное компактное отображение (узкое окно / смартфон) и дизассемблер
    io.DisplaySize = ImVec2(800.0f, 480.0f);
    menu.JumpToMemoryAddress(mock_elf.fake_base, "ELF Header ASM", true);
    ImGui::NewFrame();
    menu.Render(rt_table);
    ImGui::Render();
    ImDrawData* dd_compact = ImGui::GetDrawData();
    assert(dd_compact != nullptr && dd_compact->Valid && dd_compact->TotalVtxCount > 0);
    std::printf("[OK] Адаптивный компактный режим (800x480 + ARM64 Disasm) -> Vertices=%d\n",
                dd_compact->TotalVtxCount);

    ImGui::DestroyContext(ctx);
    br::mem::SafeMemory::SetCustomReader(nullptr, nullptr);
    std::printf("=== ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ УСПЕШНО ===\n");
    return 0;
}

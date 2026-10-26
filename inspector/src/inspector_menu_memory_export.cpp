#include "inspector_menu.hpp"
#include "inspector_ui_helpers.hpp"
#include "arm64_disasm.hpp"
#include "safe_memory.hpp"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "imgui.h"

namespace br::ui {

namespace {

FILE* OpenAndroidWritableExportFile(const char* filename, char* out_full_path, size_t out_sz) {
    static constexpr const char* kCandidateDirs[] = {
        "/sdcard/Download",
        "/storage/emulated/0/Download",
        "/data/data/com.blackhub.bronline/files",
        "/data/user/0/com.blackhub.bronline/files",
        "/data/local/tmp",
        "/tmp"
    };

    for (const char* dir : kCandidateDirs) {
        char path[256]{};
        std::snprintf(path, sizeof(path), "%s/%s", dir, filename);
        FILE* fp = std::fopen(path, "wb");
        if (fp) {
            if (out_full_path && out_sz > 0) {
                std::snprintf(out_full_path, out_sz, "%s", path);
            }
            return fp;
        }
    }
    return nullptr;
}

bool ParseHexPatternWithWildcards(const char* query,
                                  std::vector<uint8_t>& out_bytes,
                                  std::vector<uint8_t>& out_mask) {
    out_bytes.clear();
    out_mask.clear();
    if (!query) return false;

    const char* p = query;
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (!*p) break;

        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
        if (p[0] == '?') {
            out_bytes.push_back(0);
            out_mask.push_back(0);
            ++p;
            if (*p == '?') ++p;
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(p[0])) &&
            std::isxdigit(static_cast<unsigned char>(p[1]))) {
            char hex2[3] = {p[0], p[1], '\0'};
            const auto val = static_cast<uint8_t>(std::strtoul(hex2, nullptr, 16));
            out_bytes.push_back(val);
            out_mask.push_back(1);
            p += 2;
        } else {
            return false;
        }
    }
    return !out_bytes.empty();
}

const char* WatchTypeLabel(WatchValueType t) noexcept {
    switch (t) {
        case WatchValueType::Uint8:   return "u8";
        case WatchValueType::Uint16:  return "u16";
        case WatchValueType::Int32:   return "i32";
        case WatchValueType::Uint32:  return "u32";
        case WatchValueType::Float32: return "float";
        case WatchValueType::Ptr64:   return "ptr64";
        case WatchValueType::Vec3f:   return "vec3";
        case WatchValueType::CString: return "str";
    }
    return "val";
}

size_t WatchTypeByteSize(WatchValueType t) noexcept {
    switch (t) {
        case WatchValueType::Uint8:   return 1;
        case WatchValueType::Uint16:  return 2;
        case WatchValueType::Int32:   return 4;
        case WatchValueType::Uint32:  return 4;
        case WatchValueType::Float32: return 4;
        case WatchValueType::Ptr64:   return 8;
        case WatchValueType::Vec3f:   return 12;
        case WatchValueType::CString: return 24;
    }
    return 4;
}

} // namespace

void InspectorMenu::JumpToMemoryAddress(uintptr_t addr, const char* label, bool open_disasm) {
    if (mem_inspect_addr_ != 0 && mem_inspect_addr_ != addr) {
        if (mem_nav_history_.size() >= 32) {
            mem_nav_history_.erase(mem_nav_history_.begin());
        }
        mem_nav_history_.push_back(mem_inspect_addr_);
    }
    mem_inspect_addr_    = addr;
    mem_selected_offset_ = 0;
    std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x%" PRIXPTR, addr);
    std::snprintf(scan_start_buf_, sizeof(scan_start_buf_), "0x%" PRIXPTR, addr);
    if (label && label != mem_inspect_label_) {
        std::snprintf(mem_inspect_label_, sizeof(mem_inspect_label_), "%s", label);
    }
    mem_lab_mode_   = open_disasm ? 1 : 0;
    active_section_ = 5;
}

void InspectorMenu::AddWatchAddress(uintptr_t addr, WatchValueType type, const char* label) {
    if (addr == 0) return;
    for (auto& existing : watch_items_) {
        if (existing.address == addr && existing.type == type) {
            if (label && *label) {
                std::snprintf(existing.label, sizeof(existing.label), "%s", label);
            }
            return;
        }
    }
    MemoryWatchItem item{};
    item.address = addr;
    item.type    = type;
    std::snprintf(item.label, sizeof(item.label), "%s", (label && *label) ? label : "Поле");
    watch_items_.push_back(item);
}

bool InspectorMenu::ExportWorldSnapshotJson(const InternalImGuiRuntimeTable& rt_table) {
    char filename[64]{};
    std::snprintf(filename, sizeof(filename), "br_16111_snapshot_%llu.json",
                  static_cast<unsigned long long>(detail::MonotonicNowMs() / 1000ULL));

    char saved_path[256]{};
    FILE* fp = OpenAndroidWritableExportFile(filename, saved_path, sizeof(saved_path));
    if (!fp) {
        export_last_ok_ = false;
        std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                      "Не удалось открыть директорию для записи JSON (проверьте права доступа).");
        return false;
    }

    const std::string bid = InternalImGuiBridge::FormatBuildIdHex(rt_table.observed_build_id);
    std::fprintf(fp, "{\n");
    std::fprintf(fp, "  \"build_version\": \"%s\",\n", offsets::kTargetBuildVersion);
    std::fprintf(fp, "  \"module_base\": \"0x%" PRIXPTR "\",\n", rt_table.module_base);
    std::fprintf(fp, "  \"build_id_hex\": \"%s\",\n", bid.c_str());
    std::fprintf(fp, "  \"build_id_verified\": %s,\n", rt_table.build_id_verified ? "true" : "false");

    const auto& lp = snapshot_.local_player;
    std::fprintf(fp, "  \"local_player\": {\n");
    std::fprintf(fp, "    \"valid\": %s,\n", lp.valid ? "true" : "false");
    std::fprintf(fp, "    \"id\": %u,\n", lp.player_id);
    std::fprintf(fp, "    \"pos\": [%.3f, %.3f, %.3f],\n", lp.pos[0], lp.pos[1], lp.pos[2]);
    std::fprintf(fp, "    \"speed_kmh\": %.2f,\n", computed_speed_kmh_);
    std::fprintf(fp, "    \"ped_ptr\": \"0x%" PRIXPTR "\",\n", lp.ped_ptr);
    std::fprintf(fp, "    \"veh_ptr\": \"0x%" PRIXPTR "\"\n", lp.local_vehicle_ptr);
    std::fprintf(fp, "  },\n");

    std::fprintf(fp, "  \"pools\": {\n");
    std::fprintf(fp, "    \"remote_players_active\": %zu,\n", snapshot_.remote_players.size());
    std::fprintf(fp, "    \"vehicles_active\": %zu,\n", snapshot_.vehicles.size());
    std::fprintf(fp, "    \"objects_active\": %zu\n", snapshot_.active_object_slots);
    std::fprintf(fp, "  }\n}\n");
    std::fclose(fp);

    export_last_ok_ = true;
    std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                  "JSON-снимок сохранён: %s", saved_path);
    return true;
}

bool InspectorMenu::ExportNetworkEventsCsv() {
    char filename[64]{};
    std::snprintf(filename, sizeof(filename), "br_16111_netlog_%llu.csv",
                  static_cast<unsigned long long>(detail::MonotonicNowMs() / 1000ULL));

    char saved_path[256]{};
    FILE* fp = OpenAndroidWritableExportFile(filename, saved_path, sizeof(saved_path));
    if (!fp) {
        export_last_ok_ = false;
        std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                      "Не удалось создать CSV-файл сетевого лога.");
        return false;
    }

    std::fprintf(fp, "seq,timestamp_ms,kind,id_or_rpc,bit_len,byte_len,priority,reliability,caller_va,hex_head\n");
    std::vector<net::CapturedNetEvent> events;
    std::vector<net::CounterSnapshotEntry> pkt_cnt, rpc_cnt;
    uint64_t tot_pkts = 0, tot_rpcs = 0;
    net::NetworkCatalogAndObserver::GetCapturedSnapshot(events, pkt_cnt, rpc_cnt, tot_pkts, tot_rpcs);

    for (const auto& ev : events) {
        char hex_head[96]{};
        const size_t n = std::min<size_t>(ev.captured_len, 24);
        for (size_t i = 0; i < n; ++i) {
            std::snprintf(hex_head + i * 3, 4, "%02X ", ev.preview_bytes[i]);
        }
        std::fprintf(fp, "%" PRIu64 ",%" PRIu64 ",%s,%d,%d,%d,%d,%d,0x%" PRIXPTR ",\"%s\"\n",
                     ev.seq_no,
                     ev.timestamp_ms,
                     (ev.kind == net::NetEventKind::RawPacket) ? "RAW" : "RPC",
                     ev.id,
                     ev.total_bits,
                     ev.total_bytes,
                     ev.priority,
                     ev.reliability,
                     ev.caller_va,
                     hex_head);
    }
    std::fclose(fp);

    export_last_ok_ = true;
    std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                  "CSV-лог сети (%zu записей) сохранён: %s", events.size(), saved_path);
    return true;
}

bool InspectorMenu::ExportMemoryViewTxt(uintptr_t module_base) {
    if (mem_inspect_addr_ == 0) {
        export_last_ok_ = false;
        std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                      "Сначала выберите адрес в разделе «Память».");
        return false;
    }

    char filename[64]{};
    std::snprintf(filename, sizeof(filename), "br_16111_mem_0x%" PRIXPTR ".txt", mem_inspect_addr_);

    char saved_path[256]{};
    FILE* fp = OpenAndroidWritableExportFile(filename, saved_path, sizeof(saved_path));
    if (!fp) {
        export_last_ok_ = false;
        std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                      "Не удалось сохранить TXT-дамп памяти.");
        return false;
    }

    std::fprintf(fp, "# BLACK RUSSIA 16.111.19050 Read-Only Memory & Disasm Dump\n");
    std::fprintf(fp, "# Label: %s | Address: 0x%" PRIXPTR " | Base: 0x%" PRIXPTR "\n\n",
                 mem_inspect_label_, mem_inspect_addr_, module_base);

    for (size_t off = 0; off < mem_inspect_bytes_; off += 16) {
        uint8_t row[16]{};
        const bool ok = mem::SafeMemory::ReadBytes(mem_inspect_addr_ + off, row, 16);
        std::fprintf(fp, "0x%012" PRIXPTR " (+0x%03zX): ", mem_inspect_addr_ + off, off);
        for (size_t b = 0; b < 16; ++b) {
            if (ok) std::fprintf(fp, "%02X ", row[b]);
            else    std::fprintf(fp, "?? ");
        }
        std::fprintf(fp, "\n");
    }

    std::fprintf(fp, "\n# AArch64 Disassembly Preview:\n");
    for (size_t off = 0; off < std::min<size_t>(mem_inspect_bytes_, 256); off += 4) {
        uint32_t insn = 0;
        if (mem::SafeMemory::Read<uint32_t>(mem_inspect_addr_ + off, insn)) {
            char text[96]{};
            disasm::FormatArm64Instruction(insn, mem_inspect_addr_ + off, text, sizeof(text));
            std::fprintf(fp, "0x%012" PRIXPTR ":  %08X    %s\n", mem_inspect_addr_ + off, insn, text);
        }
    }
    std::fclose(fp);

    export_last_ok_ = true;
    std::snprintf(export_status_msg_, sizeof(export_status_msg_),
                  "Дамп памяти и ARM64 сохранён: %s", saved_path);
    return true;
}

void InspectorMenu::RenderSectionMemoryLab(const InternalImGuiRuntimeTable& rt_table, float ui_scale) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);
    const uintptr_t base = rt_table.module_base;

    if (mem_inspect_addr_ == 0 && base != 0) {
        JumpToMemoryAddress(base + offsets::kGnuBuildIdDescVA, "ELF .note.gnu.build-id", false);
    }

    // Переключатель 5 подрежимов лаборатории памяти
    if (detail::DrawFilterChip("Hex + Типы", mem_lab_mode_ == 0, pal)) mem_lab_mode_ = 0;
    ImGui::SameLine();
    if (detail::DrawFilterChip("ARM64 Дизассемблер", mem_lab_mode_ == 1, pal)) mem_lab_mode_ = 1;
    ImGui::SameLine();
    char wl_btn[48]{};
    std::snprintf(wl_btn, sizeof(wl_btn), "Watchlist (%zu)", watch_items_.size());
    if (detail::DrawFilterChip(wl_btn, mem_lab_mode_ == 2, pal)) mem_lab_mode_ = 2;
    ImGui::SameLine();
    if (detail::DrawFilterChip("Сканер диапазона", mem_lab_mode_ == 3, pal)) mem_lab_mode_ = 3;
    ImGui::SameLine();
    if (detail::DrawFilterChip("Карта VA (16.111)", mem_lab_mode_ == 4, pal)) mem_lab_mode_ = 4;

    ImGui::Separator();

    // Навигационная строка адреса для Hex и ARM64 Disasm
    if (mem_lab_mode_ == 0 || mem_lab_mode_ == 1) {
        if (!mem_nav_history_.empty()) {
            if (ImGui::Button("<- Назад")) {
                const uintptr_t prev = mem_nav_history_.back();
                mem_nav_history_.pop_back();
                mem_inspect_addr_ = prev;
                std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x%" PRIXPTR, prev);
            }
            ImGui::SameLine();
        }

        ImGui::SetNextItemWidth(195.0f * ui_scale);
        ImGui::InputText("Адрес / VA", mem_addr_input_, sizeof(mem_addr_input_));
        ImGui::SameLine();
        if (ImGui::Button("Перейти")) {
            const unsigned long long parsed = std::strtoull(mem_addr_input_, nullptr, 16);
            if (parsed != 0) {
                JumpToMemoryAddress(static_cast<uintptr_t>(parsed), "Ручной адрес", mem_lab_mode_ == 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+Base") && base != 0) {
            const unsigned long long parsed = std::strtoull(mem_addr_input_, nullptr, 16);
            JumpToMemoryAddress(base + static_cast<uintptr_t>(parsed), "Base + VA", mem_lab_mode_ == 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("-0x80") && mem_inspect_addr_ >= 0x80) {
            mem_inspect_addr_ -= 0x80;
            std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x%" PRIXPTR, mem_inspect_addr_);
        }
        ImGui::SameLine();
        if (ImGui::Button("+0x80")) {
            mem_inspect_addr_ += 0x80;
            std::snprintf(mem_addr_input_, sizeof(mem_addr_input_), "0x%" PRIXPTR, mem_inspect_addr_);
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Watch")) {
            AddWatchAddress(mem_inspect_addr_ + mem_selected_offset_, WatchValueType::Uint32, mem_inspect_label_);
        }

        ImGui::TextColored(pal.accent, "Метка: %s | 0x%" PRIXPTR, mem_inspect_label_, mem_inspect_addr_);
        if (base != 0 && mem_inspect_addr_ >= base && mem_inspect_addr_ < base + 0x05000000ULL) {
            ImGui::SameLine();
            ImGui::TextDisabled("(VA +0x%" PRIXPTR ")", mem_inspect_addr_ - base);
        }
    }

    if (mem_lab_mode_ == 0) {
        // Режим 0: Безопасный Hex-просмотрщик + декодер выбранного смещения
        int bytes_i = static_cast<int>(mem_inspect_bytes_);
        ImGui::SetNextItemWidth(160.0f * ui_scale);
        if (ImGui::SliderInt("Размер окна (Б)", &bytes_i, 64, 1024)) {
            mem_inspect_bytes_ = static_cast<size_t>((bytes_i / 16) * 16);
        }
        ImGui::SameLine();
        if (ImGui::Button("Сохранить дамп .txt")) {
            ExportMemoryViewTxt(base);
        }

        std::vector<uint8_t> buf(mem_inspect_bytes_, 0);
        std::vector<uint8_t> ok_mask(mem_inspect_bytes_, 0);
        for (size_t off = 0; off < mem_inspect_bytes_; off += 16) {
            if (mem::SafeMemory::ReadBytes(mem_inspect_addr_ + off, buf.data() + off, 16)) {
                std::memset(ok_mask.data() + off, 1, 16);
            } else {
                for (size_t b = 0; b < 16; ++b) {
                    if (mem::SafeMemory::Read<uint8_t>(mem_inspect_addr_ + off + b, buf[off + b])) {
                        ok_mask[off + b] = 1;
                    }
                }
            }
        }

        // Панель интерпретатора типов по выбранному байту
        if (mem_selected_offset_ < buf.size() && ok_mask[mem_selected_offset_]) {
            const uintptr_t sel_addr = mem_inspect_addr_ + mem_selected_offset_;
            const uint8_t   v_u8  = mem::SafeMemory::ReadOr<uint8_t>(sel_addr, 0);
            const uint16_t  v_u16 = mem::SafeMemory::ReadOr<uint16_t>(sel_addr, 0);
            const int32_t   v_i32 = mem::SafeMemory::ReadOr<int32_t>(sel_addr, 0);
            const float     v_f32 = mem::SafeMemory::ReadOr<float>(sel_addr, 0.0f);
            const uintptr_t v_ptr = mem::SafeMemory::ReadOr<uintptr_t>(sel_addr, 0);

            ImGui::Text("Курсор +0x%03zX | u8:%u | u16:%u | i32:%d | f32:%.4f | ptr:0x%" PRIXPTR,
                        mem_selected_offset_, v_u8, v_u16, v_i32, v_f32, v_ptr);
            const uintptr_t untagged = mem::SafeMemory::StripTag(v_ptr);
            if (mem::SafeMemory::IsPlausibleAddress(untagged)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Разыменовать ptr ->")) {
                    JumpToMemoryAddress(untagged, "Разыменованный указатель", false);
                }
            }
        }

        if (ImGui::BeginChild("##HexLabScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
            ApplyTouchKineticScrolling();
            for (size_t row = 0; row < mem_inspect_bytes_; row += 16) {
                ImGui::PushID(static_cast<int>(row));
                ImGui::TextDisabled("+%03zX", row);
                ImGui::SameLine();
                for (size_t b = 0; b < 16; ++b) {
                    const size_t idx = row + b;
                    char bstr[8]{};
                    if (ok_mask[idx]) std::snprintf(bstr, sizeof(bstr), "%02X", buf[idx]);
                    else              std::snprintf(bstr, sizeof(bstr), "??");

                    const bool is_sel = (mem_selected_offset_ == idx);
                    if (ImGui::Selectable(bstr, is_sel, ImGuiSelectableFlags_None, ImVec2(23.0f * ui_scale, 0.0f))) {
                        mem_selected_offset_ = idx;
                    }
                    ImGui::SameLine(0.0f, (b == 7) ? 10.0f * ui_scale : 3.0f * ui_scale);
                }
                char ascii[17]{};
                for (size_t b = 0; b < 16; ++b) {
                    const uint8_t c = buf[row + b];
                    ascii[b] = (ok_mask[row + b] && c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
                }
                ImGui::Text(" |%s|", ascii);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    } else if (mem_lab_mode_ == 1) {
        // Режим 1: Встроенный Read-Only дизассемблер AArch64
        const uintptr_t aligned_pc = mem_inspect_addr_ & ~static_cast<uintptr_t>(3ULL);
        if (ImGui::BeginTable("##Arm64DisasmTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Адрес", ImGuiTableColumnFlags_WidthFixed, 125.0f * ui_scale);
            ImGui::TableSetupColumn("VA (+Base)", ImGuiTableColumnFlags_WidthFixed, 95.0f * ui_scale);
            ImGui::TableSetupColumn("Слово", ImGuiTableColumnFlags_WidthFixed, 85.0f * ui_scale);
            ImGui::TableSetupColumn("Инструкция AArch64", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Переход", ImGuiTableColumnFlags_WidthFixed, 76.0f * ui_scale);
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            for (size_t i = 0; i < 64; ++i) {
                const uintptr_t pc = aligned_pc + i * 4;
                uint32_t insn = 0;
                const bool ok = mem::SafeMemory::Read<uint32_t>(pc, insn);

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("0x%" PRIXPTR, pc);

                ImGui::TableSetColumnIndex(1);
                if (base != 0 && pc >= base && pc < base + 0x05000000ULL) {
                    ImGui::TextDisabled("+0x%" PRIXPTR, pc - base);
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableSetColumnIndex(2);
                if (ok) ImGui::Text("%08X", insn);
                else    ImGui::TextDisabled("????????");

                ImGui::TableSetColumnIndex(3);
                uintptr_t branch_target = 0;
                if (ok) {
                    char decoded[96]{};
                    disasm::FormatArm64Instruction(insn, pc, decoded, sizeof(decoded));
                    const bool is_br = ((insn & 0x7C000000U) == 0x14000000U) ||
                                       ((insn & 0xFFFFFC1FU) == 0xD65F0000U);
                    if (is_br) {
                        ImGui::TextColored(pal.accent, "%s", decoded);
                    } else {
                        ImGui::TextUnformatted(decoded);
                    }
                    // Если это B / BL — вычислим адрес для кнопки быстрого перехода
                    if ((insn & 0x7C000000U) == 0x14000000U) {
                        int64_t imm26 = static_cast<int64_t>(insn & 0x03FFFFFFU);
                        if (imm26 & 0x02000000LL) imm26 -= 0x04000000LL;
                        branch_target = static_cast<uintptr_t>(static_cast<intptr_t>(pc) + imm26 * 4);
                    }
                } else {
                    ImGui::TextDisabled("<недоступно>");
                }

                ImGui::TableSetColumnIndex(4);
                if (branch_target != 0 && ImGui::SmallButton("Туда ->")) {
                    JumpToMemoryAddress(branch_target, "Переход B/BL", true);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else if (mem_lab_mode_ == 2) {
        // Режим 2: Живой Watchlist (Read-Only отслеживание адресов с подсветкой изменений)
        ImGui::SetNextItemWidth(155.0f * ui_scale);
        ImGui::InputText("Hex адрес", watch_new_addr_, sizeof(watch_new_addr_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(155.0f * ui_scale);
        ImGui::InputText("Метка", watch_new_label_, sizeof(watch_new_label_));
        ImGui::SameLine();
        static constexpr const char* kTypeNames[] = {
            "u8", "u16", "i32", "u32", "float", "ptr64", "vec3", "cstring"
        };
        ImGui::SetNextItemWidth(100.0f * ui_scale);
        ImGui::Combo("Тип", &watch_new_type_, kTypeNames, 8);
        ImGui::SameLine();
        if (ImGui::Button("+ Добавить")) {
            const uintptr_t a = static_cast<uintptr_t>(std::strtoull(watch_new_addr_, nullptr, 16));
            if (a != 0) {
                AddWatchAddress(a, static_cast<WatchValueType>(watch_new_type_), watch_new_label_);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Очистить все")) {
            watch_items_.clear();
        }

        const uint64_t now_ms = detail::MonotonicNowMs();
        if (ImGui::BeginTable("##WatchlistTable", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Метка");
            ImGui::TableSetupColumn("Адрес");
            ImGui::TableSetupColumn("Тип", ImGuiTableColumnFlags_WidthFixed, 64.0f * ui_scale);
            ImGui::TableSetupColumn("Текущее значение (Live)");
            ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 58.0f * ui_scale);
            ImGui::TableSetupColumn("Удал.", ImGuiTableColumnFlags_WidthFixed, 56.0f * ui_scale);
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            int remove_idx = -1;
            for (size_t i = 0; i < watch_items_.size(); ++i) {
                auto& w = watch_items_[i];
                uint8_t cur_raw[24]{};
                const size_t bsz = WatchTypeByteSize(w.type);
                const bool ok = mem::SafeMemory::ReadBytes(w.address, cur_raw, bsz);
                if (ok) {
                    if (w.has_prev && std::memcmp(cur_raw, w.last_raw, bsz) != 0) {
                        w.changed_ms = now_ms;
                    }
                    std::memcpy(w.last_raw, cur_raw, bsz);
                    w.has_prev = true;
                }
                const bool recently_changed = (w.changed_ms != 0 && (now_ms - w.changed_ms) < 1200ULL);

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(w.label);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%" PRIXPTR, w.address);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", WatchTypeLabel(w.type));

                ImGui::TableSetColumnIndex(3);
                if (!ok) {
                    ImGui::TextDisabled("<недоступно>");
                } else {
                    char val_txt[96]{};
                    switch (w.type) {
                        case WatchValueType::Uint8:   std::snprintf(val_txt, sizeof(val_txt), "%u (0x%02X)", cur_raw[0], cur_raw[0]); break;
                        case WatchValueType::Uint16:  { uint16_t v; std::memcpy(&v, cur_raw, 2); std::snprintf(val_txt, sizeof(val_txt), "%u (0x%04X)", v, v); break; }
                        case WatchValueType::Int32:   { int32_t v;  std::memcpy(&v, cur_raw, 4); std::snprintf(val_txt, sizeof(val_txt), "%d", v); break; }
                        case WatchValueType::Uint32:  { uint32_t v; std::memcpy(&v, cur_raw, 4); std::snprintf(val_txt, sizeof(val_txt), "%u (0x%08X)", v, v); break; }
                        case WatchValueType::Float32: { float v;    std::memcpy(&v, cur_raw, 4); std::snprintf(val_txt, sizeof(val_txt), "%.4f", v); break; }
                        case WatchValueType::Ptr64:   { uintptr_t v; std::memcpy(&v, cur_raw, 8); std::snprintf(val_txt, sizeof(val_txt), "0x%" PRIXPTR, v); break; }
                        case WatchValueType::Vec3f:   { float v[3]; std::memcpy(v, cur_raw, 12); std::snprintf(val_txt, sizeof(val_txt), "(%.2f, %.2f, %.2f)", v[0], v[1], v[2]); break; }
                        case WatchValueType::CString: {
                            const std::string s = mem::SafeMemory::ReadCString(w.address, 23);
                            std::snprintf(val_txt, sizeof(val_txt), "\"%s\"", s.c_str());
                            break;
                        }
                    }
                    if (recently_changed) ImGui::TextColored(pal.warning, "%s  [ИЗМ]", val_txt);
                    else                  ImGui::TextUnformatted(val_txt);
                }

                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton("Hex")) {
                    JumpToMemoryAddress(w.address, w.label, false);
                }
                ImGui::TableSetColumnIndex(5);
                if (ImGui::SmallButton("X")) {
                    remove_idx = static_cast<int>(i);
                }
                ImGui::PopID();
            }
            if (remove_idx >= 0) {
                watch_items_.erase(watch_items_.begin() + remove_idx);
            }
            ImGui::EndTable();
        }
    } else if (mem_lab_mode_ == 3) {
        // Режим 3: Read-Only сканер паттернов/значений внутри заданного диапазона
        static constexpr const char* kScanModes[] = {
            "Hex паттерн (AA BB ?? DD)",
            "Int32 (точное число)",
            "Float32 (±0.01)",
            "ASCII подстрока"
        };
        ImGui::SetNextItemWidth(210.0f * ui_scale);
        ImGui::Combo("Режим поиска", &scan_mode_, kScanModes, 4);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f * ui_scale);
        ImGui::InputText("Старт (Hex)", scan_start_buf_, sizeof(scan_start_buf_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(135.0f * ui_scale);
        ImGui::SliderInt("Диапазон (КБ)", &scan_range_kb_, 4, 2048);

        ImGui::SetNextItemWidth(320.0f * ui_scale);
        ImGui::InputText("Искомое", scan_query_buf_, sizeof(scan_query_buf_));
        ImGui::SameLine();
        if (ImGui::Button("Искать (Read-Only)")) {
            scan_results_.clear();
            uintptr_t start_addr = static_cast<uintptr_t>(std::strtoull(scan_start_buf_, nullptr, 16));
            if (start_addr == 0) {
                start_addr = (mem_inspect_addr_ != 0) ? mem_inspect_addr_ : base;
            }
            const size_t total_bytes = static_cast<size_t>(std::clamp(scan_range_kb_, 4, 2048)) * 1024ULL;

            std::vector<uint8_t> needle;
            std::vector<uint8_t> mask;
            bool query_valid = false;
            float target_f32 = 0.0f;

            if (scan_mode_ == 0) {
                query_valid = ParseHexPatternWithWildcards(scan_query_buf_, needle, mask);
            } else if (scan_mode_ == 1) {
                const int32_t v = static_cast<int32_t>(std::strtol(scan_query_buf_, nullptr, 0));
                needle.resize(4);
                std::memcpy(needle.data(), &v, 4);
                mask.assign(4, 1);
                query_valid = (scan_query_buf_[0] != '\0');
            } else if (scan_mode_ == 2) {
                target_f32  = std::strtof(scan_query_buf_, nullptr);
                query_valid = (scan_query_buf_[0] != '\0');
            } else if (scan_mode_ == 3) {
                const size_t slen = std::strlen(scan_query_buf_);
                if (slen > 0) {
                    needle.assign(reinterpret_cast<const uint8_t*>(scan_query_buf_),
                                  reinterpret_cast<const uint8_t*>(scan_query_buf_) + slen);
                    mask.assign(slen, 1);
                    query_valid = true;
                }
            }

            if (!query_valid || start_addr == 0) {
                std::snprintf(scan_status_msg_, sizeof(scan_status_msg_),
                              "Проверьте стартовый адрес и формат поискового запроса.");
            } else {
                constexpr size_t kPage = 4096;
                uint8_t page_buf[kPage + 64]{};
                for (size_t off = 0; off < total_bytes && scan_results_.size() < 256; off += kPage) {
                    const size_t read_len = std::min<size_t>(kPage + 32, total_bytes - off);
                    if (!mem::SafeMemory::ReadBytes(start_addr + off, page_buf, read_len)) {
                        continue;
                    }
                    if (scan_mode_ == 2) {
                        for (size_t k = 0; k + 4 <= std::min(kPage, read_len) && scan_results_.size() < 256; k += 4) {
                            float val = 0.0f;
                            std::memcpy(&val, page_buf + k, 4);
                            if (std::isfinite(val) && std::fabs(val - target_f32) <= 0.01f) {
                                MemoryScanHit hit{};
                                hit.address = start_addr + off + k;
                                std::memcpy(hit.preview, page_buf + k, std::min<size_t>(8, read_len - k));
                                scan_results_.push_back(hit);
                            }
                        }
                    } else {
                        const size_t pat_len = needle.size();
                        for (size_t k = 0; k + pat_len <= read_len && k < kPage && scan_results_.size() < 256; ++k) {
                            bool match = true;
                            for (size_t m = 0; m < pat_len; ++m) {
                                if (mask[m] && page_buf[k + m] != needle[m]) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) {
                                MemoryScanHit hit{};
                                hit.address = start_addr + off + k;
                                std::memcpy(hit.preview, page_buf + k, std::min<size_t>(8, read_len - k));
                                scan_results_.push_back(hit);
                            }
                        }
                    }
                }
                std::snprintf(scan_status_msg_, sizeof(scan_status_msg_),
                              "Проверено %d КБ от 0x%" PRIXPTR " -> найдено совпадений: %zu (макс. 256)",
                              scan_range_kb_, start_addr, scan_results_.size());
            }
        }

        ImGui::TextDisabled("%s", scan_status_msg_);
        if (ImGui::BeginTable("##ScanHitsTable", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Адрес совпадения");
            ImGui::TableSetupColumn("Смещение от Base");
            ImGui::TableSetupColumn("Первые 8 байт");
            ImGui::TableSetupColumn("Действия", ImGuiTableColumnFlags_WidthFixed, 140.0f * ui_scale);
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            for (size_t i = 0; i < scan_results_.size(); ++i) {
                const auto& h = scan_results_[i];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("0x%" PRIXPTR, h.address);
                ImGui::TableSetColumnIndex(1);
                if (base != 0 && h.address >= base && h.address < base + 0x05000000ULL) {
                    ImGui::Text("+0x%" PRIXPTR, h.address - base);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%02X %02X %02X %02X %02X %02X %02X %02X",
                            h.preview[0], h.preview[1], h.preview[2], h.preview[3],
                            h.preview[4], h.preview[5], h.preview[6], h.preview[7]);
                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("Hex")) {
                    JumpToMemoryAddress(h.address, "Результат сканера", false);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("ASM")) {
                    JumpToMemoryAddress(h.address, "Код совпадения", true);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        // Режим 4: Справочник эталонных VA 16.111.19050 с переходом в Hex и ARM64 Disasm
        struct VaEntry {
            const char* name;
            uintptr_t   va;
            bool        is_code;
            const char* desc;
        };
        static constexpr VaEntry kKnownVas[] = {
            {"JNILib::step (GLThread)",      offsets::jni_bridge::kJni_JNILib_step,            true,  "Кадр рендера GLSurfaceView (точка нашего ImGui)"},
            {"JNILib::multiTouchEvent",      offsets::jni_bridge::kJni_JNILib_multiTouchEvent, true,  "Мультитач ввода (action, count, x1, y1...)"},
            {"JNILib::resize",               offsets::jni_bridge::kJni_JNILib_resize,          true,  "Запись ширины/высоты экрана в 0x03C0FAD8"},
            {"g_pRakClient (.bss)",          offsets::globals::kRakClientInterfacePtr,         false, "Указатель на RakClientInterface (vtable 0x02140BF0)"},
            {"g_pPlayerPool (.bss)",         offsets::globals::kPlayerPoolPtr,                 false, "Указатель CPlayerPool (Local + 1504 Remote)"},
            {"g_pVehiclePool (.bss)",        offsets::globals::kVehiclePoolPtr,                false, "Указатель CVehiclePool (2000 слотов)"},
            {"g_pObjectPool (.bss)",         offsets::globals::kObjectPoolPtr,                 false, "Указатель CObjectPool (4096 слотов)"},
            {"PlayerPool::GetRemotePlayer",  offsets::player::kFnPlayerPoolGetRemote,          true,  "Проверка ID <= 1503 и чтение слота +0x08 + id*8"},
            {"VehiclePool::FindByGameVeh",   offsets::vehicle::kFnVehiclePoolFindByGameVeh,    true,  "Поиск слота транспорта по указателю CVehicle*"},
            {"Sender Packet 30 (OnFoot 73B)",0x0178EE04ULL,                                    true,  "Сериализатор пешего пакета синхронизации ID 30"},
            {"Sender Packet 34 (InVeh 66B)", 0x0178BDB0ULL,                                    true,  "Сериализатор транспортного пакета ID 34"},
            {"Sender Packet 38 (Aim 32B)",   0x0178E5B4ULL,                                    true,  "Сериализатор пакета прицела/оружия ID 38"},
            {"RakClient::Send Thunk",        offsets::raknet::kFnRakClientSendThunkVA,         true,  "Слот vtable +0x38 отправки BitStream"},
            {"RakClient::RPC Thunk",         offsets::raknet::kFnRakClientRpcThunkVA,          true,  "Слот vtable +0xB0 отправки RPC"},
        };

        if (ImGui::BeginTable("##VaCatalogTbl", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Символ");
            ImGui::TableSetupColumn("VA в ELF", ImGuiTableColumnFlags_WidthFixed, 95.0f * ui_scale);
            ImGui::TableSetupColumn("Runtime", ImGuiTableColumnFlags_WidthFixed, 125.0f * ui_scale);
            ImGui::TableSetupColumn("Описание");
            ImGui::TableSetupColumn("Открыть", ImGuiTableColumnFlags_WidthFixed, 115.0f * ui_scale);
            ImGui::TableHeadersRow();

            ApplyTouchKineticScrolling();
            for (size_t i = 0; i < (sizeof(kKnownVas) / sizeof(kKnownVas[0])); ++i) {
                const auto& e = kKnownVas[i];
                const uintptr_t rt = (base != 0) ? (base + e.va) : e.va;
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(e.name);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%07" PRIXPTR, e.va);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("0x%" PRIXPTR, rt);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", e.desc);
                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton("Hex")) {
                    JumpToMemoryAddress(rt, e.name, false);
                }
                if (e.is_code) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("ASM")) {
                        JumpToMemoryAddress(rt, e.name, true);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

void InspectorMenu::RenderSectionSettingsAndExport(const InternalImGuiRuntimeTable& rt_table, float ui_scale) {
    const auto pal = detail::GetThemePalette(config_.theme, config_.window_bg_alpha);

    if (ImGui::BeginChild("##SettingsScrollArea", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None)) {
        ApplyTouchKineticScrolling();

        ImGui::TextColored(pal.accent, "Оформление, Адаптивность и Сенсорное управление");
        ImGui::Separator();

        // Палитра из 4 тем
        ImGui::TextUnformatted("Цветовая тема:");
        ImGui::SameLine();
        if (detail::DrawFilterChip("Cyber Obsidian", config_.theme == InspectorThemeId::CyberObsidian, pal))
            config_.theme = InspectorThemeId::CyberObsidian;
        ImGui::SameLine();
        if (detail::DrawFilterChip("Crimson Carbon", config_.theme == InspectorThemeId::CrimsonCarbon, pal))
            config_.theme = InspectorThemeId::CrimsonCarbon;
        ImGui::SameLine();
        if (detail::DrawFilterChip("Emerald Matrix", config_.theme == InspectorThemeId::EmeraldMatrix, pal))
            config_.theme = InspectorThemeId::EmeraldMatrix;
        ImGui::SameLine();
        if (detail::DrawFilterChip("Daylight Frost", config_.theme == InspectorThemeId::DaylightFrost, pal))
            config_.theme = InspectorThemeId::DaylightFrost;

        // Быстрые пресеты расположения окна на экране смартфона / планшета
        ImGui::TextUnformatted("Расположение окна:");
        ImGui::SameLine();
        if (ImGui::Button("Стандарт (82%)")) { pending_preset_ = WindowPresetId::StandardCenter; pending_preset_apply_ = true; }
        ImGui::SameLine();
        if (ImGui::Button("Компакт (64%)"))  { pending_preset_ = WindowPresetId::CompactFloat;   pending_preset_apply_ = true; }
        ImGui::SameLine();
        if (ImGui::Button("Весь экран"))     { pending_preset_ = WindowPresetId::MaximizedSafe;  pending_preset_apply_ = true; }
        ImGui::SameLine();
        if (ImGui::Button("Сплит влево"))    { pending_preset_ = WindowPresetId::SplitLeft;      pending_preset_apply_ = true; }
        ImGui::SameLine();
        if (ImGui::Button("Сплит вправо"))   { pending_preset_ = WindowPresetId::SplitRight;     pending_preset_apply_ = true; }

        ImGui::SetNextItemWidth(240.0f * ui_scale);
        ImGui::SliderFloat("Масштаб интерфейса", &config_.user_ui_scale, 0.70f, 1.75f, "%.2fx");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f * ui_scale);
        ImGui::SliderFloat("Непрозрачность фона", &config_.window_bg_alpha, 0.58f, 1.00f, "%.2f");

        ImGui::SetNextItemWidth(240.0f * ui_scale);
        ImGui::SliderFloat("Отступ Safe Area X (px)", &config_.safe_area_pad_x, 0.0f, 96.0f, "%.0f px");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f * ui_scale);
        ImGui::SliderFloat("Отступ Safe Area Y (px)", &config_.safe_area_pad_y, 0.0f, 64.0f, "%.0f px");

        ImGui::Checkbox("Кинетическая прокрутка списков пальцем (Touch Drag-Scroll)", &config_.kinetic_touch_scroll);
        ImGui::Checkbox("Адаптивная навигация (боковая панель на широком окне / верхняя на узком)", &config_.adaptive_sidebar);
        ImGui::Checkbox("Показывать плавающий виджет быстрого доступа (BR 16.111)", &config_.show_floating_pill);
        ImGui::Checkbox("Показывать прозрачный Мини-HUD поверх игры", &config_.show_mini_hud);

        ImGui::Spacing();
        ImGui::TextColored(pal.accent, "Экспорт диагностических отчётов на устройство (Read-Only)");
        ImGui::Separator();

        if (ImGui::Button("Экспорт снимка мира в .JSON")) {
            ExportWorldSnapshotJson(rt_table);
        }
        ImGui::SameLine();
        if (ImGui::Button("Экспорт сетевого лога в .CSV")) {
            ExportNetworkEventsCsv();
        }
        ImGui::SameLine();
        if (ImGui::Button("Экспорт открытого адреса в .TXT")) {
            ExportMemoryViewTxt(rt_table.module_base);
        }

        ImGui::TextColored(export_last_ok_ ? pal.success : pal.danger, "%s", export_status_msg_);

        ImGui::Spacing();
        ImGui::TextColored(pal.accent, "Диагностика собственного рендера Dear ImGui 1.90.9 и мультитача");
        ImGui::Separator();
        const std::string obs_bid = InternalImGuiBridge::FormatBuildIdHex(rt_table.observed_build_id);
        ImGui::BulletText("Модуль: %s @ 0x%" PRIXPTR " | Build-ID: %s (%s)",
                          offsets::kTargetModuleName,
                          rt_table.module_base,
                          obs_bid.c_str(),
                          rt_table.build_id_verified ? "совпадает с 16.111.19050" : "отличается");
        ImGui::BulletText("Own ImGuiContext*: 0x%" PRIXPTR " | Кадров: %" PRIu64 " | Шрифт: %s",
                          reinterpret_cast<uintptr_t>(ImGui::GetCurrentContext()),
                          rt_table.rendered_frames_total,
                          rt_table.loaded_font_label ? rt_table.loaded_font_label : "-");
        ImGui::BulletText("Хук кадра: %s | Хук тача: %s",
                          rt_table.render_hook_name ? rt_table.render_hook_name : "-",
                          rt_table.touch_hook_name ? rt_table.touch_hook_name : "-");
        ImGui::BulletText("Поверхность GLES3: %.0f x %.0f | Касаний: %" PRIu64 " | Последнее касание: (%.0f, %.0f) [%s]",
                          ImGui::GetIO().DisplaySize.x,
                          ImGui::GetIO().DisplaySize.y,
                          rt_table.touch_events_total,
                          rt_table.last_touch_x,
                          rt_table.last_touch_y,
                          rt_table.last_touch_down ? "НАЖАТ" : "ОТПУЩЕН");
    }
    ImGui::EndChild();
}

} // namespace br::ui

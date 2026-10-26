// ============================================================================
// entry_compat.cpp — C-экспорты MenuBR_* (совместимость со старой smali-
// интеграцией JNILib). Реальная логика — main_entry.cpp (br_inspector_*).
// Self-start: __attribute__((constructor)) в main_entry.cpp (OnLibraryLoaded
// Constructor) + JNI_OnLoad — срабатывают сразу после dlopen.
// ============================================================================

#include "inspector_menu.hpp"
#include "internal_imgui_bridge.hpp"

// C-API из main_entry.cpp (референс)
extern "C" {
void br_inspector_init(void);
void br_inspector_shutdown(void);
bool br_inspector_feed_touch(int action, float x, float y);
}

extern "C" {

// Совместимость со старыми smali-хуками (System.loadLibrary("menu-br"))
extern "C" __attribute__((visibility("default")))
void MenuBR_Init(void) {
    br_inspector_init();
}

extern "C" __attribute__((visibility("default")))
void MenuBR_Shutdown(void) {
    br_inspector_shutdown();
}

extern "C" __attribute__((visibility("default")))
int MenuBR_IsActive(void) {
    return br::ui::InternalImGuiBridge::IsAttached() ? 1 : 0;
}

} // extern "C"

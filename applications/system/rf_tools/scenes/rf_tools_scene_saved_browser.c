#include "../rf_tools_i.h"

// Uses the stock DialogsApp file browser (the same one every Flipper app uses for file
// pick/browse) instead of a hand-rolled list, so it can browse the whole SD card the same
// way any other app does, not just /ext/subghz.

void rf_tools_scene_saved_browser_on_enter(void* context) {
    RfToolsApp* app = context;

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, SUBGHZ_APP_FILENAME_EXTENSION, NULL);
    options.base_path = EXT_PATH("");

    FuriString* result = furi_string_alloc();
    bool picked = dialog_file_browser_show(app->dialogs, result, app->saved_current_dir, &options);

    if(picked) {
        furi_string_set(app->saved_current_dir, result);
        app->tx_kind = RfToolsTxKindFile;
        strncpy(app->tx_path, furi_string_get_cstr(result), sizeof(app->tx_path) - 1);
        app->tx_path[sizeof(app->tx_path) - 1] = '\0';
        furi_string_free(result);
        scene_manager_next_scene(app->scene_manager, RfToolsSceneTxBusy);
    } else {
        furi_string_free(result);
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool rf_tools_scene_saved_browser_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_tools_scene_saved_browser_on_exit(void* context) {
    UNUSED(context);
}

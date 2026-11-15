#include "../rf_tools_i.h"

typedef enum {
    RfToolsStartIndexCapture,
    RfToolsStartIndexRepeater,
    RfToolsStartIndexAnalyzer,
    RfToolsStartIndexSaved,
    RfToolsStartIndexRadioSettings,
} RfToolsStartIndex;

static void rf_tools_scene_start_callback(void* context, uint32_t index) {
    RfToolsApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RfToolsSceneStart, index);

    switch(index) {
    case RfToolsStartIndexCapture:
        scene_manager_next_scene(app->scene_manager, RfToolsSceneCaptureMenu);
        break;
    case RfToolsStartIndexRepeater:
        scene_manager_next_scene(app->scene_manager, RfToolsSceneRepeater);
        break;
    case RfToolsStartIndexAnalyzer:
        scene_manager_next_scene(app->scene_manager, RfToolsSceneAnalyzer);
        break;
    case RfToolsStartIndexSaved:
        scene_manager_next_scene(app->scene_manager, RfToolsSceneSavedBrowser);
        break;
    case RfToolsStartIndexRadioSettings:
        scene_manager_next_scene(app->scene_manager, RfToolsSceneRadioSettings);
        break;
    }
}

void rf_tools_scene_start_on_enter(void* context) {
    RfToolsApp* app = context;

    submenu_add_item(
        app->submenu, "Capture", RfToolsStartIndexCapture, rf_tools_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Repeater", RfToolsStartIndexRepeater, rf_tools_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Frequency Analyzer", RfToolsStartIndexAnalyzer, rf_tools_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Saved Signals", RfToolsStartIndexSaved, rf_tools_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Radio Settings", RfToolsStartIndexRadioSettings, rf_tools_scene_start_callback, app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, RfToolsSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewSubmenu);
}

bool rf_tools_scene_start_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_tools_scene_start_on_exit(void* context) {
    RfToolsApp* app = context;
    submenu_reset(app->submenu);
}

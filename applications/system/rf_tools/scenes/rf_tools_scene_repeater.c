#include "../rf_tools_i.h"

typedef enum {
    RfToolsRepeaterIndexListen,
    RfToolsRepeaterIndexSettings,
} RfToolsRepeaterIndex;

static void rf_tools_scene_repeater_callback(void* context, uint32_t index) {
    RfToolsApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RfToolsSceneRepeater, index);

    if(index == RfToolsRepeaterIndexListen) {
        app->capture_from_repeater = true;
        scene_manager_set_scene_state(app->scene_manager, RfToolsSceneCaptureLive, 0); // decode mode
        scene_manager_next_scene(app->scene_manager, RfToolsSceneCaptureLive);
    } else {
        scene_manager_next_scene(app->scene_manager, RfToolsSceneRepeaterSettings);
    }
}

void rf_tools_scene_repeater_on_enter(void* context) {
    RfToolsApp* app = context;

    submenu_add_item(
        app->submenu, "Listen", RfToolsRepeaterIndexListen, rf_tools_scene_repeater_callback, app);
    submenu_add_item(
        app->submenu, "Settings", RfToolsRepeaterIndexSettings, rf_tools_scene_repeater_callback, app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, RfToolsSceneRepeater));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewSubmenu);
}

bool rf_tools_scene_repeater_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_tools_scene_repeater_on_exit(void* context) {
    RfToolsApp* app = context;
    submenu_reset(app->submenu);
}

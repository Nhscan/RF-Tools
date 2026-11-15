#include "../rf_tools_i.h"

typedef enum {
    RfToolsCaptureMenuIndexRead,
    RfToolsCaptureMenuIndexReadRaw,
} RfToolsCaptureMenuIndex;

static void rf_tools_scene_capture_menu_callback(void* context, uint32_t index) {
    RfToolsApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RfToolsSceneCaptureMenu, index);
    scene_manager_set_scene_state(app->scene_manager, RfToolsSceneCaptureLive, index);
    app->capture_from_repeater = false;
    scene_manager_next_scene(app->scene_manager, RfToolsSceneCaptureLive);
}

void rf_tools_scene_capture_menu_on_enter(void* context) {
    RfToolsApp* app = context;

    submenu_add_item(
        app->submenu, "Read", RfToolsCaptureMenuIndexRead, rf_tools_scene_capture_menu_callback, app);
    submenu_add_item(
        app->submenu, "Read Raw", RfToolsCaptureMenuIndexReadRaw, rf_tools_scene_capture_menu_callback, app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, RfToolsSceneCaptureMenu));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewSubmenu);
}

bool rf_tools_scene_capture_menu_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_tools_scene_capture_menu_on_exit(void* context) {
    RfToolsApp* app = context;
    submenu_reset(app->submenu);
}

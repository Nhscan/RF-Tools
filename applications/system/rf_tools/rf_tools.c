#include "rf_tools_i.h"

static bool rf_tools_custom_event_callback(void* context, uint32_t event) {
    RfToolsApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool rf_tools_back_event_callback(void* context) {
    RfToolsApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static RfToolsApp* rf_tools_app_alloc(void) {
    RfToolsApp* app = malloc(sizeof(RfToolsApp));
    memset(app, 0, sizeof(RfToolsApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&rf_tools_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, rf_tools_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, rf_tools_back_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RfToolsViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RfToolsViewVarItemList, variable_item_list_get_view(app->var_item_list));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RfToolsViewTextInput, text_input_get_view(app->text_input));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RfToolsViewWidget, widget_get_view(app->widget));

    app->capture_view = rf_tools_capture_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RfToolsViewCapture, rf_tools_capture_view_get_view(app->capture_view));

    app->analyzer_view = rf_tools_analyzer_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RfToolsViewAnalyzer, rf_tools_analyzer_view_get_view(app->analyzer_view));

    app->engine = rf_tools_engine_alloc(app->notifications, app->storage);
    app->analyzer_worker = rf_tools_analyzer_worker_alloc(app->engine);

    app->saved_current_dir = furi_string_alloc_set(RF_TOOLS_SAVE_FOLDER);
    app->analyzer_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    return app;
}

static void rf_tools_app_free(RfToolsApp* app) {
    furi_mutex_free(app->analyzer_mutex);
    furi_string_free(app->saved_current_dir);

    rf_tools_analyzer_worker_free(app->analyzer_worker);
    rf_tools_engine_free(app->engine);

    view_dispatcher_remove_view(app->view_dispatcher, RfToolsViewAnalyzer);
    rf_tools_analyzer_view_free(app->analyzer_view);

    view_dispatcher_remove_view(app->view_dispatcher, RfToolsViewCapture);
    rf_tools_capture_view_free(app->capture_view);

    view_dispatcher_remove_view(app->view_dispatcher, RfToolsViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, RfToolsViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, RfToolsViewVarItemList);
    variable_item_list_free(app->var_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, RfToolsViewSubmenu);
    submenu_free(app->submenu);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t rf_tools_app(void* p) {
    UNUSED(p);
    RfToolsApp* app = rf_tools_app_alloc();

    scene_manager_next_scene(app->scene_manager, RfToolsSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    rf_tools_app_free(app);
    return 0;
}

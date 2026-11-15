#include "../rf_tools_i.h"

static void rf_tools_scene_save_name_callback(void* context) {
    RfToolsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventTextInputDone);
}

void rf_tools_scene_save_name_on_enter(void* context) {
    RfToolsApp* app = context;

    memset(app->text_input_buf, 0, sizeof(app->text_input_buf));
    rf_tools_engine_default_save_name(
        app->engine, app->result_is_raw, app->text_input_buf, sizeof(app->text_input_buf));

    text_input_set_header_text(app->text_input, "Name signal");
    text_input_set_result_callback(
        app->text_input,
        rf_tools_scene_save_name_callback,
        app,
        app->text_input_buf,
        sizeof(app->text_input_buf),
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewTextInput);
}

bool rf_tools_scene_save_name_on_event(void* context, SceneManagerEvent event) {
    RfToolsApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != RfToolsEventTextInputDone) {
        return false;
    }

    if(strlen(app->text_input_buf) == 0) return true;

    FuriString* path = furi_string_alloc();
    furi_string_printf(path, "%s/%s%s", RF_TOOLS_SAVE_FOLDER, app->text_input_buf, SUBGHZ_APP_FILENAME_EXTENSION);

    bool ok = app->result_is_raw ? rf_tools_engine_save_raw_as(app->engine, furi_string_get_cstr(path)) :
                                    rf_tools_engine_save_decoded_as(
                                        app->engine, app->result_frequency, furi_string_get_cstr(path));
    furi_string_free(path);

    notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
    if(ok) {
        // Saved - the result has been consumed, no reason to keep it or the capture stack
        // around. Drop straight back to the main menu rather than threading a "return to
        // wherever we came from" state through three more scenes.
        scene_manager_search_and_switch_to_another_scene(app->scene_manager, RfToolsSceneStart);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
    return true;
}

void rf_tools_scene_save_name_on_exit(void* context) {
    RfToolsApp* app = context;
    text_input_reset(app->text_input);
}

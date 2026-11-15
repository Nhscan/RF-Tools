#include "../rf_tools_i.h"

// Layout mirrors applications/main/subghz/scenes/subghz_scene_receiver_info.c: the decoded
// text is a multiline element at x=0,y=0, while frequency is a short single-line element
// starting well to the right (x=78) on the same y=0 row - they never collide because the
// multiline text's own content is short enough not to reach that far right in practice, and
// anything that does is simply clipped by the canvas, never drawn over the frequency text's
// own pixels.

static void rf_tools_scene_result_widget_callback(GuiButtonType result, InputType type, void* context) {
    RfToolsApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventWidgetSave);
    } else if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventWidgetSend);
    }
}

void rf_tools_scene_result_on_enter(void* context) {
    RfToolsApp* app = context;

    FuriString* text = furi_string_alloc();
    if(app->result_is_raw) {
        furi_string_printf(text, "RAW signal\n%zu edges captured", rf_tools_engine_raw_sample_count(app->engine));
    } else {
        furi_string_set(text, app->engine->capture_decoded_detail);
    }
    widget_add_string_multiline_element(
        app->widget, 0, 0, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(text));
    furi_string_free(text);

    FuriString* freq = furi_string_alloc();
    furi_string_printf(
        freq, "%lu.%02lu MHz", app->result_frequency / 1000000, (app->result_frequency / 10000) % 100);
    widget_add_string_element(
        app->widget, 78, 0, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(freq));
    furi_string_free(freq);

    if(app->engine->settings->save_mode != RfToolsSaveOff) {
        widget_add_button_element(
            app->widget, GuiButtonTypeRight, "Save", rf_tools_scene_result_widget_callback, app);
    }
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Send", rf_tools_scene_result_widget_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewWidget);
}

bool rf_tools_scene_result_on_event(void* context, SceneManagerEvent event) {
    RfToolsApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RfToolsEventWidgetSave) {
            scene_manager_next_scene(app->scene_manager, RfToolsSceneSaveName);
            return true;
        } else if(event.event == RfToolsEventWidgetSend) {
            app->tx_kind = app->result_is_raw ? RfToolsTxKindRawTmp : RfToolsTxKindDecoded;
            scene_manager_next_scene(app->scene_manager, RfToolsSceneTxBusy);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        // Discard: go back to whichever menu launched the capture, skipping past CaptureLive
        // (re-entering it would try to start a brand new capture).
        const uint32_t candidates[] = {RfToolsSceneCaptureMenu, RfToolsSceneRepeater, RfToolsSceneAnalyzer};
        scene_manager_search_and_switch_to_previous_scene_one_of(
            app->scene_manager, candidates, COUNT_OF(candidates));
        return true;
    }
    return false;
}

void rf_tools_scene_result_on_exit(void* context) {
    RfToolsApp* app = context;
    widget_reset(app->widget);
}

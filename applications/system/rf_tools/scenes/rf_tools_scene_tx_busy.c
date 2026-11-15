#include "../rf_tools_i.h"

// Runs the actual transmit on its own thread so the GUI/dispatcher thread never blocks for
// the whole repeat-count*timeout duration - that blocking is what made Back unresponsive
// during a stuck/slow send before. Back here sets the engine's abort flag and waits for the
// thread to notice (checked every ~2ms inside the TX wait loop) rather than leaving the radio
// mid-transmission.

static int32_t rf_tools_tx_busy_thread(void* context) {
    RfToolsApp* app = context;
    FURI_LOG_W(RF_TOOLS_TAG, "TxBusy thread started, tick=%lu", furi_get_tick());
    switch(app->tx_kind) {
    case RfToolsTxKindDecoded:
        app->tx_result = rf_tools_engine_transmit_decoded(app->engine, app->result_frequency);
        break;
    case RfToolsTxKindRawTmp:
        app->tx_result = rf_tools_engine_transmit_raw_tmp(app->engine);
        break;
    case RfToolsTxKindFile:
        app->tx_result = rf_tools_engine_transmit_file(app->engine, app->tx_path);
        break;
    }
    FURI_LOG_W(RF_TOOLS_TAG, "TxBusy thread done, tick=%lu, result=%d", furi_get_tick(), app->tx_result);
    view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventTxDone);
    return 0;
}

void rf_tools_scene_tx_busy_on_enter(void* context) {
    RfToolsApp* app = context;
    FURI_LOG_W(RF_TOOLS_TAG, "Send pressed, tick=%lu", furi_get_tick());

    widget_add_string_element(app->widget, 64, 32, AlignCenter, AlignCenter, FontPrimary, "Sending...");
    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewWidget);

    // 1024 bytes was enough for the old single-pass tx_frame; now that a Send also walks
    // through flipper_format_insert_or_update_uint32() (stream seek/shift/rewrite) to force the
    // internal repeat count, and goes through an extra setup-radio call, that's tight enough to
    // have caused a stack overflow (observed as a full device crash/reboot on RX->auto-TX).
    app->tx_thread = furi_thread_alloc_ex("RfToolsTx", 2048, rf_tools_tx_busy_thread, app);
    app->engine->tx_abort = false;
    furi_thread_start(app->tx_thread);
}

bool rf_tools_scene_tx_busy_on_event(void* context, SceneManagerEvent event) {
    RfToolsApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == RfToolsEventTxDone) {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
        notification_message(app->notifications, app->tx_result ? &sequence_success : &sequence_error);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    } else if(event.type == SceneManagerEventTypeBack) {
        rf_tools_engine_request_tx_abort(app->engine);
        widget_reset(app->widget);
        widget_add_string_element(app->widget, 64, 32, AlignCenter, AlignCenter, FontPrimary, "Cancelling...");
        return true; // stay put - the thread needs to actually stop before we can leave
    }
    return false;
}

void rf_tools_scene_tx_busy_on_exit(void* context) {
    RfToolsApp* app = context;
    widget_reset(app->widget);
}

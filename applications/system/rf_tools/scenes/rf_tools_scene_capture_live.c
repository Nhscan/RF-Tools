#include "../rf_tools_i.h"

// UI polling does not own decoder state; the engine snapshots it in the RX callback.
#define RF_TOOLS_CAPTURE_TICK_MS 200

static void rf_tools_scene_capture_live_timer_callback(void* context) {
    RfToolsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventCaptureTick);
}

static int32_t rf_tools_scene_capture_live_auto_tx_thread(void* context) {
    RfToolsApp* app = context;
    app->tx_result = rf_tools_engine_transmit_decoded(app->engine, app->engine->settings->frequency);
    view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventAutoTxDone);
    return 0;
}

static void rf_tools_scene_capture_live_view_callback(RfToolsCaptureViewEvent event, void* context) {
    RfToolsApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        event == RfToolsCaptureViewEventStop ? RfToolsEventCaptureStop : RfToolsEventCaptureBack);
}

void rf_tools_scene_capture_live_on_enter(void* context) {
    RfToolsApp* app = context;
    bool raw_mode = scene_manager_get_scene_state(app->scene_manager, RfToolsSceneCaptureLive) != 0;
    uint32_t freq = app->engine->settings->frequency;

    rf_tools_capture_view_set_callback(app->capture_view, rf_tools_scene_capture_live_view_callback, app);

    // Read Raw waits for an explicit Record press rather than starting the moment the screen
    // opens - Read (decode) still auto-starts, since that's just passive listening.
    if(raw_mode) {
        app->raw_awaiting_start = true;
        rf_tools_capture_view_reset(
            app->capture_view, freq, furi_string_get_cstr(app->engine->settings->preset_name),
            app->engine->device_is_external, true);
        rf_tools_capture_view_set_idle(app->capture_view, true);
        view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewCapture);
        return;
    }

    app->raw_awaiting_start = false;
    if(!rf_tools_engine_start_decode_capture(app->engine, freq)) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    rf_tools_capture_view_reset(
        app->capture_view,
        freq,
        furi_string_get_cstr(app->engine->settings->preset_name),
        app->engine->device_is_external,
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewCapture);

    app->capture_timer =
        furi_timer_alloc(rf_tools_scene_capture_live_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->capture_timer, furi_ms_to_ticks(RF_TOOLS_CAPTURE_TICK_MS));
}

// Stops the capture (if still running) and routes to Result if there's anything worth
// keeping, otherwise straight back to whichever menu launched this capture. Also handles
// Repeater's Auto-TX: show the decoded signal + a countdown, hold there (not re-arming
// listening) until the retransmit actually goes out, then resume listening.
static void rf_tools_scene_capture_live_finish(RfToolsApp* app) {
    rf_tools_engine_stop_capture(app->engine);

    bool decoded = rf_tools_engine_capture_got_decode(app->engine);
    size_t raw_samples = rf_tools_engine_raw_sample_count(app->engine);
    bool worth_keeping = decoded || raw_samples >= RF_TOOLS_MIN_RAW_SAMPLES_WORTH_SAVING;

    if(decoded && app->capture_from_repeater && app->engine->settings->auto_tx) {
        rf_tools_engine_finalize_decoded_result(app->engine, NULL, NULL);

        uint32_t delay_ms = app->engine->settings->repeat_delay_ms ? app->engine->settings->repeat_delay_ms :
                                                                      RF_TOOLS_DEFAULT_TX_GAP_MS;
        // Clear the decode flag now, before any waiting - otherwise every ~200ms Tick between
        // now and when the countdown elapses would see the same stale decode and try to
        // re-enter this branch again.
        rf_tools_engine_clear_decode(app->engine);
        app->auto_tx_pending = true;
        app->auto_tx_fire_tick = furi_get_tick() + furi_ms_to_ticks(delay_ms);
        rf_tools_capture_view_set_pending_tx(
            app->capture_view,
            furi_string_get_cstr(app->engine->capture_decoded_detail),
            (delay_ms + 999) / 1000);
        return;
    }

    if(!worth_keeping) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    if(decoded) {
        rf_tools_engine_finalize_decoded_result(app->engine, &app->result_is_raw, &app->result_has_raw_fallback);
    } else {
        app->result_is_raw = true;
        app->result_has_raw_fallback = false;
    }
    app->result_frequency = app->engine->settings->frequency;
    scene_manager_next_scene(app->scene_manager, RfToolsSceneResult);
}

bool rf_tools_scene_capture_live_on_event(void* context, SceneManagerEvent event) {
    RfToolsApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case RfToolsEventCaptureTick: {
        if(app->auto_tx_pending) {
            uint32_t now = furi_get_tick();
            int32_t remaining = (int32_t)(app->auto_tx_fire_tick - now);
            uint32_t remain_ms = remaining > 0 ? (uint32_t)remaining : 0;
            rf_tools_capture_view_set_countdown(app->capture_view, (remain_ms + 999) / 1000);
            if(remain_ms == 0) {
                app->auto_tx_pending = false;
                // Same stack bump as rf_tools_scene_tx_busy.c's RfToolsTx thread - 1024 was
                // tight enough to overflow once Send started also forcing the internal repeat
                // count via flipper_format_insert_or_update_uint32().
                app->tx_thread = furi_thread_alloc_ex(
                    "RfToolsAutoTx", 2048, rf_tools_scene_capture_live_auto_tx_thread, app);
                app->engine->tx_abort = false;
    furi_thread_start(app->tx_thread);
            }
            return true;
        }

        uint32_t elapsed_s = (furi_get_tick() - app->engine->capture_start_tick) / 1000;
        rf_tools_capture_view_update(app->capture_view, elapsed_s, rf_tools_engine_raw_sample_count(app->engine));
        // Only Read (decode mode) auto-finishes on a decode - Read Raw keeps recording until
        // the user presses Stop even if a clean decode shows up along the way, since that's
        // the explicit Record/Stop/Send flow asked for; the decode (if any) is just along for
        // the ride and gets picked up by rf_tools_scene_capture_live_finish() at Stop time.
        if(app->engine->capture_mode == RfToolsCaptureModeDecode &&
           rf_tools_engine_capture_got_decode(app->engine)) {
            rf_tools_scene_capture_live_finish(app);
        }
        return true;
    }
    case RfToolsEventCaptureStop:
        if(app->auto_tx_pending || app->tx_thread) return true;
        if(app->raw_awaiting_start) {
            // This OK press means "start recording now", not "stop".
            app->raw_awaiting_start = false;
            if(!rf_tools_engine_start_raw_capture(app->engine, app->engine->settings->frequency)) {
                scene_manager_previous_scene(app->scene_manager);
                return true;
            }
            rf_tools_capture_view_set_idle(app->capture_view, false);
            app->capture_timer =
                furi_timer_alloc(rf_tools_scene_capture_live_timer_callback, FuriTimerTypePeriodic, app);
            furi_timer_start(app->capture_timer, furi_ms_to_ticks(RF_TOOLS_CAPTURE_TICK_MS));
            return true;
        }
        rf_tools_scene_capture_live_finish(app);
        return true;
    case RfToolsEventAutoTxDone: {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
        notification_message(app->notifications, app->tx_result ? &sequence_success : &sequence_error);

        if(app->engine->settings->save_mode == RfToolsSaveAuto) {
            FuriString* saved = furi_string_alloc();
            rf_tools_engine_save_decoded(app->engine, app->engine->settings->frequency, saved);
            furi_string_free(saved);
        }

        if(app->auto_tx_exit_after) {
            app->auto_tx_exit_after = false;
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }

        // Resume listening - re-enter this same scene fresh.
        if(!rf_tools_engine_start_decode_capture(app->engine, app->engine->settings->frequency)) {
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
        rf_tools_capture_view_reset(
            app->capture_view,
            app->engine->settings->frequency,
            furi_string_get_cstr(app->engine->settings->preset_name),
            app->engine->device_is_external,
            false);
        return true;
    }
    case RfToolsEventCaptureBack:
        if(app->auto_tx_pending) {
            // Still counting down - nothing is on the air yet, safe to leave immediately.
            app->auto_tx_pending = false;
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
        if(app->tx_thread) {
            // A send is actually in flight - request it stop early and wait for
            // RfToolsEventAutoTxDone before navigating away, same as TxBusy's Back handling,
            // rather than freeing the thread/engine out from under it.
            rf_tools_engine_request_tx_abort(app->engine);
            app->auto_tx_exit_after = true;
            return true;
        }
        rf_tools_engine_stop_capture(app->engine);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    default:
        return false;
    }
}

void rf_tools_scene_capture_live_on_exit(void* context) {
    RfToolsApp* app = context;
    if(app->engine->capture_running) rf_tools_engine_stop_capture(app->engine);
    if(app->capture_timer) {
        furi_timer_stop(app->capture_timer);
        furi_timer_free(app->capture_timer);
        app->capture_timer = NULL;
    }
    // The Back handler waits for RfToolsEventAutoTxDone before ever navigating away while a
    // send is in flight, so tx_thread should already be NULL here - this is just a defensive
    // backstop against leaving it running if that ever changes.
    if(app->tx_thread) {
        rf_tools_engine_request_tx_abort(app->engine);
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }
    app->auto_tx_pending = false;
    app->auto_tx_exit_after = false;
}

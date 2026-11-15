#include "../rf_tools_i.h"

#define RF_TOOLS_ANALYZER_TRIGGER_MIN -110.0f
#define RF_TOOLS_ANALYZER_TRIGGER_MAX -40.0f

// Worker runs on its own FuriThread (see engine/rf_tools_analyzer_worker.c) - it only stashes
// the latest reading behind a mutex and hands off via a custom event, exactly like
// applications/system/remote_analyzer's ra_radio_pair_callback -> RaCustomEventSignal pattern.
// Nothing touches the view model from the worker thread.
static void rf_tools_scene_analyzer_worker_callback(void* context, uint32_t frequency, float rssi, bool is_signal) {
    RfToolsApp* app = context;
    furi_mutex_acquire(app->analyzer_mutex, FuriWaitForever);
    app->analyzer_latest_freq = frequency;
    app->analyzer_latest_rssi = rssi;
    app->analyzer_latest_is_signal = is_signal;
    furi_mutex_release(app->analyzer_mutex);
    view_dispatcher_send_custom_event(app->view_dispatcher, RfToolsEventAnalyzerTick);
}

static void rf_tools_scene_analyzer_view_callback(RfToolsAnalyzerViewEvent event, void* context) {
    RfToolsApp* app = context;
    RfToolsEvent custom;
    switch(event) {
    case RfToolsAnalyzerViewEventTriggerDown:
        custom = RfToolsEventAnalyzerTriggerDown;
        break;
    case RfToolsAnalyzerViewEventTriggerUp:
        custom = RfToolsEventAnalyzerTriggerUp;
        break;
    case RfToolsAnalyzerViewEventSelectSignal:
        custom = RfToolsEventAnalyzerSelect;
        break;
    default: // RfToolsAnalyzerViewEventBack
        custom = RfToolsEventCaptureBack; // reused as a generic "exit this scene" signal
        break;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

void rf_tools_scene_analyzer_on_enter(void* context) {
    RfToolsApp* app = context;

    rf_tools_analyzer_view_reset(app->analyzer_view, app->engine->device_is_external);
    rf_tools_analyzer_view_set_callback(app->analyzer_view, rf_tools_scene_analyzer_view_callback, app);
    app->analyzer_was_locked = false;

    rf_tools_analyzer_worker_set_callback(app->analyzer_worker, rf_tools_scene_analyzer_worker_callback, app);
    rf_tools_analyzer_worker_start(app->analyzer_worker);

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewAnalyzer);
}

bool rf_tools_scene_analyzer_on_event(void* context, SceneManagerEvent event) {
    RfToolsApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case RfToolsEventAnalyzerTick: {
        furi_mutex_acquire(app->analyzer_mutex, FuriWaitForever);
        uint32_t freq = app->analyzer_latest_freq;
        float rssi = app->analyzer_latest_rssi;
        bool is_signal = app->analyzer_latest_is_signal;
        furi_mutex_release(app->analyzer_mutex);

        float trigger = rf_tools_analyzer_worker_get_trigger_level(app->analyzer_worker);
        rf_tools_analyzer_view_update(app->analyzer_view, freq, rssi, trigger, is_signal);
        if(is_signal && !app->analyzer_was_locked) {
            rf_tools_analyzer_view_push_history(app->analyzer_view, freq);
        }
        app->analyzer_was_locked = is_signal;
        return true;
    }
    case RfToolsEventAnalyzerTriggerUp: {
        float t = rf_tools_analyzer_worker_get_trigger_level(app->analyzer_worker);
        t += 3.0f;
        if(t > RF_TOOLS_ANALYZER_TRIGGER_MAX) t = RF_TOOLS_ANALYZER_TRIGGER_MAX;
        rf_tools_analyzer_worker_set_trigger_level(app->analyzer_worker, t);
        return true;
    }
    case RfToolsEventAnalyzerTriggerDown: {
        float t = rf_tools_analyzer_worker_get_trigger_level(app->analyzer_worker);
        t -= 3.0f;
        if(t < RF_TOOLS_ANALYZER_TRIGGER_MIN) t = RF_TOOLS_ANALYZER_TRIGGER_MIN;
        rf_tools_analyzer_worker_set_trigger_level(app->analyzer_worker, t);
        return true;
    }
    case RfToolsEventAnalyzerSelect: {
        uint32_t freq = rf_tools_analyzer_view_get_locked_frequency(app->analyzer_view);
        if(freq == 0) return true;
        rf_tools_analyzer_worker_stop(app->analyzer_worker);
        app->engine->settings->frequency = freq;
        app->capture_from_repeater = false;
        scene_manager_set_scene_state(app->scene_manager, RfToolsSceneCaptureLive, 0);
        scene_manager_next_scene(app->scene_manager, RfToolsSceneCaptureLive);
        return true;
    }
    case RfToolsEventCaptureBack:
        scene_manager_search_and_switch_to_another_scene(app->scene_manager, RfToolsSceneStart);
        return true;
    default:
        return false;
    }
}

void rf_tools_scene_analyzer_on_exit(void* context) {
    RfToolsApp* app = context;
    if(rf_tools_analyzer_worker_is_running(app->analyzer_worker)) {
        rf_tools_analyzer_worker_stop(app->analyzer_worker);
    }
}

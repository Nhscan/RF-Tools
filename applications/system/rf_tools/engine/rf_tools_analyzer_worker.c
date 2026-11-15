#include "rf_tools_analyzer_worker.h"
#include <math.h>

// Ported from applications/main/subghz/plugins/frequency_analyzer/
// subghz_frequency_analyzer_worker.c: same two-stage algorithm (coarse scan the whole
// frequency list, then a +/-300kHz fine scan in 20kHz steps around the best coarse hit), same
// exp-running-average smoothing and sample-hold grace period. The one real difference: stock
// pokes CC1101 registers directly via furi_hal_subghz/cc1101_write_reg, which only works on
// the internal radio. This version goes through subghz_devices_* with one uniform preset for
// both passes instead of stock's two hand-tuned 58kHz/650kHz bandwidth register tweaks, so it
// works on External CC1101 too - the tradeoff is slightly coarser frequency resolution on the
// fine pass, not a correctness problem.

#define TAG "RfToolsAnalyzerWorker"
#define RF_TOOLS_ANALYZER_DEFAULT_TRIGGER -97.0f
#define RF_TOOLS_ANALYZER_SAMPLE_DELAY_MS 2
#define RF_TOOLS_ANALYZER_HOLD_TICKS      20

struct RfToolsAnalyzerWorker {
    FuriThread* thread;
    RfToolsEngine* engine;

    volatile bool worker_running;
    uint8_t sample_hold_counter;
    float fil_val;
    float trigger_level;

    RfToolsAnalyzerWorkerCallback callback;
    void* context;
};

static float rf_tools_analyzer_sample_rssi(RfToolsEngine* engine, uint32_t frequency) {
    subghz_devices_idle(engine->device);
    subghz_devices_set_frequency(engine->device, frequency);
    subghz_devices_set_rx(engine->device);
    furi_delay_ms(RF_TOOLS_ANALYZER_SAMPLE_DELAY_MS);
    return subghz_devices_get_rssi(engine->device);
}

static uint32_t rf_tools_analyzer_running_average(RfToolsAnalyzerWorker* instance, uint32_t new_val) {
    float new_val_f = new_val;
    float k = (fabsf(new_val_f - instance->fil_val) > 500000.f) ? 0.9f : 0.03f;
    instance->fil_val += (new_val_f - instance->fil_val) * k;
    return (uint32_t)instance->fil_val;
}

static int32_t rf_tools_analyzer_worker_thread(void* context) {
    RfToolsAnalyzerWorker* instance = context;
    RfToolsEngine* engine = instance->engine;

    uint8_t* preset_data = rf_tools_engine_current_preset_data(engine, NULL);
    subghz_devices_reset(engine->device);
    subghz_devices_idle(engine->device);
    subghz_devices_load_preset(engine->device, FuriHalSubGhzPresetCustom, preset_data);

    uint32_t frequency_temp = 0;
    float rssi_temp = 0;

    while(instance->worker_running) {
        furi_delay_ms(10);

        float coarse_rssi = -127.0f;
        uint32_t coarse_freq = 0;

        size_t count = subghz_setting_get_frequency_count(engine->setting);
        for(size_t i = 0; i < count && instance->worker_running; i++) {
            uint32_t f = subghz_setting_get_frequency(engine->setting, i);
            if(!subghz_devices_is_frequency_valid(engine->device, f)) continue;
            float rssi = rf_tools_analyzer_sample_rssi(engine, f);
            if(rssi > coarse_rssi) {
                coarse_rssi = rssi;
                coarse_freq = f;
            }
        }
        if(!instance->worker_running) break;

        float fine_rssi = -127.0f;
        uint32_t fine_freq = 0;
        if(coarse_rssi > instance->trigger_level && coarse_freq != 0) {
            for(uint32_t f = coarse_freq - 300000; f < coarse_freq + 300000 && instance->worker_running;
                f += 20000) {
                if(!subghz_devices_is_frequency_valid(engine->device, f)) continue;
                float rssi = rf_tools_analyzer_sample_rssi(engine, f);
                if(rssi > fine_rssi) {
                    fine_rssi = rssi;
                    fine_freq = f;
                }
            }
        }
        if(!instance->worker_running) break;

        if(fine_rssi > instance->trigger_level) {
            instance->sample_hold_counter = RF_TOOLS_ANALYZER_HOLD_TICKS;
            rssi_temp = fine_rssi;
            frequency_temp = fine_freq;
            uint32_t smoothed = rf_tools_analyzer_running_average(instance, fine_freq);
            if(instance->callback) instance->callback(instance->context, smoothed, fine_rssi, true);
        } else if(coarse_rssi > instance->trigger_level && instance->sample_hold_counter < 10) {
            instance->sample_hold_counter = RF_TOOLS_ANALYZER_HOLD_TICKS;
            rssi_temp = coarse_rssi;
            frequency_temp = coarse_freq;
            uint32_t smoothed = rf_tools_analyzer_running_average(instance, coarse_freq);
            if(instance->callback) instance->callback(instance->context, smoothed, coarse_rssi, true);
        } else if(instance->sample_hold_counter > 0) {
            instance->sample_hold_counter--;
            if(instance->sample_hold_counter == RF_TOOLS_ANALYZER_HOLD_TICKS - 2) {
                if(instance->callback) instance->callback(instance->context, frequency_temp, rssi_temp, false);
            }
        } else {
            instance->fil_val = 0;
            if(instance->callback) instance->callback(instance->context, 0, -127.0f, false);
        }
    }

    subghz_devices_idle(engine->device);
    return 0;
}

RfToolsAnalyzerWorker* rf_tools_analyzer_worker_alloc(RfToolsEngine* engine) {
    RfToolsAnalyzerWorker* instance = malloc(sizeof(RfToolsAnalyzerWorker));
    memset(instance, 0, sizeof(RfToolsAnalyzerWorker));
    instance->engine = engine;
    instance->trigger_level = RF_TOOLS_ANALYZER_DEFAULT_TRIGGER;
    instance->thread =
        furi_thread_alloc_ex("RfToolsAnalyzerWorker", 2048, rf_tools_analyzer_worker_thread, instance);
    return instance;
}

void rf_tools_analyzer_worker_free(RfToolsAnalyzerWorker* instance) {
    furi_assert(!instance->worker_running);
    furi_thread_free(instance->thread);
    free(instance);
}

void rf_tools_analyzer_worker_set_callback(
    RfToolsAnalyzerWorker* instance,
    RfToolsAnalyzerWorkerCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void rf_tools_analyzer_worker_start(RfToolsAnalyzerWorker* instance) {
    furi_assert(!instance->worker_running);
    instance->worker_running = true;
    furi_thread_start(instance->thread);
}

void rf_tools_analyzer_worker_stop(RfToolsAnalyzerWorker* instance) {
    furi_assert(instance->worker_running);
    instance->worker_running = false;
    furi_thread_join(instance->thread);
}

bool rf_tools_analyzer_worker_is_running(RfToolsAnalyzerWorker* instance) {
    return instance->worker_running;
}

void rf_tools_analyzer_worker_set_trigger_level(RfToolsAnalyzerWorker* instance, float value) {
    instance->trigger_level = value;
}

float rf_tools_analyzer_worker_get_trigger_level(RfToolsAnalyzerWorker* instance) {
    return instance->trigger_level;
}

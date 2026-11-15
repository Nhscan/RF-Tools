#pragma once

#include "rf_tools_engine.h"

typedef struct RfToolsAnalyzerWorker RfToolsAnalyzerWorker;

// (context, frequency, rssi, is_signal) - is_signal is true while the reading is above the
// trigger level, false on the "signal just dropped out" report that follows a hold period.
// Matches applications/main/subghz/plugins/frequency_analyzer/subghz_frequency_analyzer_worker.h's
// callback shape.
typedef void (*RfToolsAnalyzerWorkerCallback)(void* context, uint32_t frequency, float rssi, bool is_signal);

// engine is borrowed, not owned - the worker uses engine->device/engine->setting to scan, via
// the same subghz_devices_* abstraction as the rest of the app (so External CC1101 works,
// unlike the stock worker this is ported from, which pokes CC1101 registers directly and is
// internal-only).
RfToolsAnalyzerWorker* rf_tools_analyzer_worker_alloc(RfToolsEngine* engine);
void rf_tools_analyzer_worker_free(RfToolsAnalyzerWorker* instance);

void rf_tools_analyzer_worker_set_callback(
    RfToolsAnalyzerWorker* instance,
    RfToolsAnalyzerWorkerCallback callback,
    void* context);

void rf_tools_analyzer_worker_start(RfToolsAnalyzerWorker* instance);
void rf_tools_analyzer_worker_stop(RfToolsAnalyzerWorker* instance);
bool rf_tools_analyzer_worker_is_running(RfToolsAnalyzerWorker* instance);

void rf_tools_analyzer_worker_set_trigger_level(RfToolsAnalyzerWorker* instance, float value);
float rf_tools_analyzer_worker_get_trigger_level(RfToolsAnalyzerWorker* instance);

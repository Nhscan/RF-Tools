#pragma once

#include <gui/view.h>

#define RF_TOOLS_ANALYZER_HISTORY 4

typedef struct RfToolsAnalyzerView RfToolsAnalyzerView;

typedef enum {
    RfToolsAnalyzerViewEventTriggerDown,
    RfToolsAnalyzerViewEventTriggerUp,
    RfToolsAnalyzerViewEventSelectSignal, // OK on a locked signal - go act on it
    RfToolsAnalyzerViewEventBack,
} RfToolsAnalyzerViewEvent;

typedef void (*RfToolsAnalyzerViewCallback)(RfToolsAnalyzerViewEvent event, void* context);

RfToolsAnalyzerView* rf_tools_analyzer_view_alloc(void);
void rf_tools_analyzer_view_free(RfToolsAnalyzerView* instance);
View* rf_tools_analyzer_view_get_view(RfToolsAnalyzerView* instance);

void rf_tools_analyzer_view_set_callback(
    RfToolsAnalyzerView* instance,
    RfToolsAnalyzerViewCallback callback,
    void* context);

// is_external reflects the actually-selected radio (engine->device_is_external) - shown on
// screen so it's never a silent guess which module is in use.
void rf_tools_analyzer_view_reset(RfToolsAnalyzerView* instance, bool is_external);

// Called on every scan tick.
void rf_tools_analyzer_view_update(
    RfToolsAnalyzerView* instance,
    uint32_t frequency,
    float rssi,
    float trigger,
    bool signal_locked);

// Pushes a freshly-locked frequency into the 4-slot history (newest first).
void rf_tools_analyzer_view_push_history(RfToolsAnalyzerView* instance, uint32_t frequency);

uint32_t rf_tools_analyzer_view_get_locked_frequency(RfToolsAnalyzerView* instance);

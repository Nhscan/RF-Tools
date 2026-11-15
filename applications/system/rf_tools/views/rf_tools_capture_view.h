#pragma once

#include <gui/view.h>

typedef struct RfToolsCaptureView RfToolsCaptureView;

typedef enum {
    RfToolsCaptureViewEventStop, // OK pressed - stop and keep whatever was captured
    RfToolsCaptureViewEventBack, // Back pressed - cancel
} RfToolsCaptureViewEvent;

typedef void (*RfToolsCaptureViewCallback)(RfToolsCaptureViewEvent event, void* context);

RfToolsCaptureView* rf_tools_capture_view_alloc(void);
void rf_tools_capture_view_free(RfToolsCaptureView* instance);
View* rf_tools_capture_view_get_view(RfToolsCaptureView* instance);

void rf_tools_capture_view_set_callback(
    RfToolsCaptureView* instance,
    RfToolsCaptureViewCallback callback,
    void* context);

// Called once when the live screen is entered.
void rf_tools_capture_view_reset(
    RfToolsCaptureView* instance,
    uint32_t frequency,
    const char* preset_name,
    bool is_external,
    bool raw_mode);

// Raw mode only: true while waiting for the user to press OK to start recording (shows
// "OK: Record" instead of "Listening"/"Stop").
void rf_tools_capture_view_set_idle(RfToolsCaptureView* instance, bool idle);

// Called periodically (timer-driven) by the owning scene.
void rf_tools_capture_view_update(
    RfToolsCaptureView* instance,
    uint32_t elapsed_s,
    size_t raw_edge_count);

// Repeater Auto-TX: switches the view to showing the just-decoded signal's detail text plus
// a countdown to the retransmit, instead of the normal Listening/Recording layout.
void rf_tools_capture_view_set_pending_tx(
    RfToolsCaptureView* instance,
    const char* decoded_text,
    uint32_t countdown_s);
void rf_tools_capture_view_set_countdown(RfToolsCaptureView* instance, uint32_t countdown_s);

#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>

#include "scenes/rf_tools_scene.h"
#include "engine/rf_tools_engine.h"
#include "engine/rf_tools_analyzer_worker.h"
#include "views/rf_tools_capture_view.h"
#include "views/rf_tools_analyzer_view.h"

typedef enum {
    RfToolsViewSubmenu,
    RfToolsViewVarItemList,
    RfToolsViewTextInput,
    RfToolsViewWidget,
    RfToolsViewCapture,
    RfToolsViewAnalyzer,
} RfToolsView;

typedef enum {
    RfToolsTxKindDecoded,
    RfToolsTxKindRawTmp,
    RfToolsTxKindFile,
} RfToolsTxKind;

// Custom events sent via view_dispatcher_send_custom_event(), handled by the active scene's
// on_event.
typedef enum {
    RfToolsEventCaptureTick, // periodic poll while on CaptureLive (decode/edge-count/timeout)
    RfToolsEventCaptureStop, // OK pressed on the live capture view - stop and keep
    RfToolsEventCaptureBack, // Back pressed on the live capture view - cancel
    RfToolsEventAnalyzerTick, // a new reading arrived from the analyzer worker thread
    RfToolsEventAnalyzerSelect, // OK pressed on a locked signal in the analyzer view
    RfToolsEventAnalyzerTriggerDown,
    RfToolsEventAnalyzerTriggerUp,
    RfToolsEventWidgetSave,
    RfToolsEventWidgetSend,
    RfToolsEventTextInputDone,
    RfToolsEventTxDone, // the tx_thread finished (or was aborted)
    RfToolsEventAutoTxDone, // Repeater Auto-TX's background send thread finished
} RfToolsEvent;

typedef struct RfToolsApp RfToolsApp;

struct RfToolsApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    Storage* storage;
    DialogsApp* dialogs;

    Submenu* submenu;
    VariableItemList* var_item_list;
    TextInput* text_input;
    Widget* widget;
    RfToolsCaptureView* capture_view;
    RfToolsAnalyzerView* analyzer_view;

    RfToolsEngine* engine;
    RfToolsAnalyzerWorker* analyzer_worker;
    FuriTimer* capture_timer;

    // --- Repeater Auto-TX: wait out the Delay setting (ticked by the same periodic
    // capture_timer, shown on-screen as a countdown over the decoded signal), then send on a
    // background thread without freezing the GUI, then resume listening ---
    bool auto_tx_pending; // true while counting down to the retransmit
    uint32_t auto_tx_fire_tick; // furi_get_tick() value at which to actually start the send
    bool auto_tx_exit_after; // Back was pressed while the send was in flight - leave once it's done

    char text_input_buf[64];

    // --- result scene state (set by whichever scene produced a result) ---
    bool result_is_raw;
    bool result_has_raw_fallback;
    uint32_t result_frequency;
    // true when the active CaptureLive session was launched from Repeater's "Listen" (so
    // Auto-TX applies) rather than Capture's Read/Read Raw.
    bool capture_from_repeater;
    // Raw mode only: true while waiting for the user to press OK to actually start recording.
    bool raw_awaiting_start;

    // --- saved browser state (remembers where you last left off, for next time) ---
    FuriString* saved_current_dir;

    // --- TX busy scene: request handed in, result handed back ---
    FuriThread* tx_thread;
    RfToolsTxKind tx_kind;
    char tx_path[128];
    bool tx_result;
    RfToolsScene tx_return_scene; // where to go after - skips back past CaptureLive etc when needed

    // --- analyzer worker -> scene handoff (worker runs on its own FuriThread) ---
    FuriMutex* analyzer_mutex;
    uint32_t analyzer_latest_freq;
    float analyzer_latest_rssi;
    bool analyzer_latest_is_signal;
    bool analyzer_was_locked;
};

int32_t rf_tools_app(void* p);

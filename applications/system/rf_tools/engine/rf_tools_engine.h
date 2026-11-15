#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/flipper_format/flipper_format.h>

#include "rf_tools_settings.h"
#include "../helpers/radio_device_loader.h"

#define RF_TOOLS_TAG          "RFTools"
#define RF_TOOLS_SAVE_FOLDER      EXT_PATH("subghz/RF-Tools")
#define RF_TOOLS_RAW_TMP_NAME     "RFTOOLS_RAW_TMP"
#define RF_TOOLS_DECODED_TMP_NAME "RFTOOLS_DECODED_TMP"
#define RF_TOOLS_MAX_LEN_NAME 64
// Below this many raw edges, a capture that never decoded is treated as noise rather than a
// real (but uncatalogued) signal worth offering to save.
#define RF_TOOLS_MIN_RAW_SAMPLES_WORTH_SAVING 20

typedef enum {
    RfToolsCaptureModeNone,
    RfToolsCaptureModeDecode, // decode only, auto-finishes the instant something decodes
    RfToolsCaptureModeRaw, // records raw edges to disk; also decodes passively in the
                            // background so Stop can offer a clean TX if one was found,
                            // but never auto-finishes early like decode mode does
} RfToolsCaptureMode;

typedef struct RfToolsEngine RfToolsEngine;

struct RfToolsEngine {
    Storage* storage;
    NotificationApp* notifications;

    RfToolsSettings* settings;
    SubGhzSetting* setting;
    SubGhzEnvironment* environment;
    const SubGhzDevice* device;
    bool device_is_external;

    SubGhzWorker* worker;
    SubGhzReceiver* receiver;
    void* raw_decoder; // SubGhzProtocolDecoderRAW*, while raw-capturing (alone or with decode)
    RfToolsCaptureMode capture_mode;
    volatile bool capture_running;
    volatile bool capture_got_decode;
    FlipperFormat* capture_decoded_fff; // immutable snapshot taken inside decoder callback
    bool capture_snapshot_valid;
    uint32_t capture_frequency;
    FuriString* capture_decoded_name; // bare protocol name, used for auto filenames
    FuriString* capture_decoded_detail; // subghz_protocol_decoder_base_get_string() output
    uint32_t capture_start_tick;
    FuriMutex* capture_mutex;
    // TEMPORARY DEBUG: counts subghz_worker overrun events during the active capture, logged
    // alongside every decode so a dropped-edge-caused misparse can be confirmed/ruled out from
    // the serial log instead of guessed at.
    uint32_t debug_overrun_count;

    // Set by rf_tools_engine_request_tx_abort() from another thread (e.g. a scene reacting to
    // Back) to stop an in-progress transmit early instead of blocking until every repeat/frame
    // timeout runs its course.
    volatile bool tx_abort;
};

// Call from any thread to make the current/next transmit stop early.
void rf_tools_engine_request_tx_abort(RfToolsEngine* engine);

// --- lifecycle ---------------------------------------------------------------

RfToolsEngine* rf_tools_engine_alloc(NotificationApp* notifications, Storage* storage);
void rf_tools_engine_free(RfToolsEngine* engine);

// Re-selects the device per engine->settings->radio_choice. On return,
// engine->device_is_external tells which one was actually obtained (a request for External
// silently falls back to Internal if nothing answers on SPI).
void rf_tools_engine_reselect_device(RfToolsEngine* engine);

uint8_t* rf_tools_engine_current_preset_data(RfToolsEngine* engine, size_t* out_size);

// --- capture -------------------------------------------------------------------

bool rf_tools_engine_start_decode_capture(RfToolsEngine* engine, uint32_t frequency);
bool rf_tools_engine_start_raw_capture(RfToolsEngine* engine, uint32_t frequency);
void rf_tools_engine_stop_capture(RfToolsEngine* engine);

size_t rf_tools_engine_raw_sample_count(RfToolsEngine* engine);
bool rf_tools_engine_capture_got_decode(RfToolsEngine* engine);

// Clears the "got a decode" flag without starting a new capture - needed by Repeater's
// Auto-TX, which has to wait out the configured delay and then run the transmit on a
// background thread before it's safe to re-arm listening; without this, every ~200ms poll
// in between would see the same stale decode and re-trigger Auto-TX again immediately.
void rf_tools_engine_clear_decode(RfToolsEngine* engine);

// Call right after rf_tools_engine_stop_capture() on a successful decode. Figures out whether
// a usable parallel raw capture also exists, and deletes the raw temp file if it turned out
// unused (preferred-clean capture that also happened to have a raw fallback available).
// Returns the chosen representation via *out_is_raw / *out_has_raw_fallback.
void rf_tools_engine_finalize_decoded_result(RfToolsEngine* engine, bool* out_is_raw, bool* out_has_raw_fallback);

// --- save ------------------------------------------------------------------------

// Saves the just-captured decoded signal under an auto-generated name in RF_TOOLS_SAVE_FOLDER.
void rf_tools_engine_save_decoded(RfToolsEngine* engine, uint32_t frequency, FuriString* out_saved_path);

// Saves the raw temp file (from the just-finished capture) under an auto-generated name.
void rf_tools_engine_save_raw(RfToolsEngine* engine, FuriString* out_saved_path);

// Same as above, but to an exact caller-chosen path (user-typed name from the Save Name
// screen) instead of an auto-generated one.
bool rf_tools_engine_save_decoded_as(RfToolsEngine* engine, uint32_t frequency, const char* full_path);
bool rf_tools_engine_save_raw_as(RfToolsEngine* engine, const char* full_path);

// A reasonable default filename (no extension/folder) for the current result, for pre-filling
// the Save Name screen - "RAW" + datetime, or the decoded protocol name + datetime.
void rf_tools_engine_default_save_name(RfToolsEngine* engine, bool is_raw, char* buf, size_t buf_size);

// --- transmit --------------------------------------------------------------------

// Resolves and ACTIVATES the preset named in a file's "Preset" field (long FuriHalSubGhzPreset
// name or "FuriHalSubGhzPresetCustom", converted to the short name subghz_setting uses) as
// engine->settings->preset_name - mirrors what the stock app does when a file is loaded
// (subghz_i.c's subghz_key_load), so that by the time we transmit, resolution has already
// happened once and doesn't need to be redone per repeat. Call before rf_tools_engine_transmit_fff
// when transmitting a file whose preset might differ from the live one (Saved Signals browser).
bool rf_tools_engine_apply_preset_from_file(RfToolsEngine* engine, FlipperFormat* fff);

// Transmits fff (decoded protocol or RAW, read from its "Protocol" key) repeat_count times,
// always tuning to the CURRENT engine->settings->frequency - this is what lets a captured
// signal be repeated on a different frequency than it was received on. Preset is NOT re-read
// from fff here - it uses whatever is currently active (engine->settings->preset_name), same
// as stock's subghz_txrx_tx_start.
bool rf_tools_engine_transmit_fff(RfToolsEngine* engine, FlipperFormat* fff);

// Convenience wrappers:
bool rf_tools_engine_transmit_file(RfToolsEngine* engine, const char* path); // applies preset from file first
bool rf_tools_engine_transmit_decoded(RfToolsEngine* engine, uint32_t frequency); // uses capture_decoded_base
bool rf_tools_engine_transmit_raw_tmp(RfToolsEngine* engine); // transmits the raw temp file as-is

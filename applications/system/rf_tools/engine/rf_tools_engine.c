#include "rf_tools_engine.h"
#include <toolbox/name_generator.h>
#include <toolbox/stream/stream.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <lib/subghz/blocks/custom_btn.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>

#define RF_TOOLS_TX_TIMEOUT_MS 120000u

// Stock's own Send button doesn't send one pass through a protocol's upload buffer and stop -
// holding OK sets SubGhzBlockGeneric's endless_tx flag, which makes the encoder's yield
// function (lib/subghz/protocols/common.c) loop the SAME frame forever without decrementing
// its repeat counter; releasing lets it decrement normally again, but it finishes whatever
// repeats are still owed first - so even the quickest tap guarantees at least the protocol's
// own built-in minimum, and any real hold sends many more. Many protocols default that minimum
// very low (Princeton's is 3, ~150ms total) - fine for a deliberate hold, but a single
// unattended burst that short is a plausible reason a receiver never latches on. Overriding
// "Repeat" here (read as an optional field by subghz_protocol_encoder_common_deserialize) makes
// every one of our bursts send this many cycles continuously, matching what an ordinary
// press-and-hold on stock would actually put on air, without requiring a hold gesture. RAW
// doesn't use this field at all - it streams real captured edges from a file, not a canned
// repeat count - so it's only applied to protocol-encoded (decoded) transmits.
#define RF_TOOLS_TX_INTERNAL_REPEATS 10

// --- internal helpers -------------------------------------------------------

static void rf_tools_worker_pair_callback(void* context, bool level, uint32_t duration) {
    RfToolsEngine* engine = context;
    // Decode and raw capture are mutually exclusive, same as stock's Read vs Read RAW. This
    // callback runs on the subghz worker thread at full edge rate; running both the protocol
    // decoder AND the raw feed on every single edge was tried (so Read Raw could also surface
    // a clean decode) and confirmed to starve the raw sample buffer on real captures - edges
    // got dropped fast enough to show "0 edges captured". Not worth that reliability cost;
    // a decode-after-the-fact replay (feeding the saved raw edges back through a decoder once
    // capture has stopped, off the real-time path) is the safe way to get that feature.
    if(engine->capture_mode == RfToolsCaptureModeDecode && engine->receiver) {
        subghz_receiver_decode(engine->receiver, level, duration);
    } else if(engine->capture_mode == RfToolsCaptureModeRaw && engine->raw_decoder) {
        subghz_protocol_decoder_raw_feed(engine->raw_decoder, level, duration);
    }
}

static void rf_tools_worker_overrun_callback(void* context) {
    RfToolsEngine* engine = context;
    engine->debug_overrun_count++;
    FURI_LOG_W(RF_TOOLS_TAG, "Worker overrun #%lu (dropped edge, receiver reset)", engine->debug_overrun_count);
    if(engine->receiver) subghz_receiver_reset(engine->receiver);
}

static void rf_tools_protocol_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    RfToolsEngine* engine = context;

    furi_mutex_acquire(engine->capture_mutex, FuriWaitForever);
    if(!engine->capture_got_decode) {
        // Decoder timing fields are working accumulators again as soon as this callback
        // returns. Snapshot NOW, like stock history; never serialize the live decoder later.
        size_t preset_size = 0;
        uint8_t* preset_data = rf_tools_engine_current_preset_data(engine, &preset_size);
        SubGhzRadioPreset preset = {
            .name = engine->settings->preset_name,
            .frequency = engine->capture_frequency,
            .data = preset_data,
            .data_size = preset_size,
        };
        stream_clean(flipper_format_get_raw_stream(engine->capture_decoded_fff));
        if(subghz_protocol_decoder_base_serialize(
               decoder_base, engine->capture_decoded_fff, &preset) != SubGhzProtocolStatusOk) {
            furi_mutex_release(engine->capture_mutex);
            return;
        }
        engine->capture_snapshot_valid = true;
        furi_string_reset(engine->capture_decoded_detail);
        furi_string_set(engine->capture_decoded_name, decoder_base->protocol->name);
        // For weather-station-type protocols this is a multi-line string that includes
        // temperature/humidity/channel etc, not just the protocol name.
        subghz_protocol_decoder_base_get_string(decoder_base, engine->capture_decoded_detail);
        engine->capture_got_decode = true;
        FURI_LOG_W(
            RF_TOOLS_TAG,
            "Decoded %s after %lu overruns this capture - detail: %s",
            decoder_base->protocol->name,
            engine->debug_overrun_count,
            furi_string_get_cstr(engine->capture_decoded_detail));
    }
    furi_mutex_release(engine->capture_mutex);


}

uint8_t* rf_tools_engine_current_preset_data(RfToolsEngine* engine, size_t* out_size) {
    uint8_t* data = subghz_setting_get_preset_data_by_name(
        engine->setting, furi_string_get_cstr(engine->settings->preset_name));
    if(out_size) {
        int idx = subghz_setting_get_inx_preset_by_name(
            engine->setting, furi_string_get_cstr(engine->settings->preset_name));
        *out_size = (idx >= 0) ? subghz_setting_get_preset_data_size(engine->setting, idx) : 0;
    }
    return data;
}

static void rf_tools_apply_preset_and_frequency(RfToolsEngine* engine, uint32_t frequency) {
    uint8_t* preset_data = rf_tools_engine_current_preset_data(engine, NULL);
    subghz_devices_reset(engine->device);
    subghz_devices_idle(engine->device);
    subghz_devices_load_preset(engine->device, FuriHalSubGhzPresetCustom, preset_data);
    subghz_devices_idle(engine->device);
    subghz_devices_set_frequency(engine->device, frequency);
}

static void rf_tools_raw_tmp_path(FuriString* out) {
    furi_string_printf(
        out, "%s/%s%s", SUBGHZ_RAW_FOLDER, RF_TOOLS_RAW_TMP_NAME, SUBGHZ_APP_FILENAME_EXTENSION);
}

static bool rf_tools_start_raw_stream(RfToolsEngine* engine, uint32_t frequency) {
    if(engine->raw_decoder) {
        subghz_protocol_decoder_raw_free(engine->raw_decoder);
        engine->raw_decoder = NULL;
    }
    engine->raw_decoder = subghz_protocol_decoder_raw_alloc(engine->environment);

    size_t preset_size = 0;
    uint8_t* preset_data = rf_tools_engine_current_preset_data(engine, &preset_size);
    SubGhzRadioPreset preset = {
        .name = engine->settings->preset_name,
        .frequency = frequency,
        .data = preset_data,
        .data_size = preset_size,
    };

    if(!subghz_protocol_raw_save_to_file_init(engine->raw_decoder, RF_TOOLS_RAW_TMP_NAME, &preset)) {
        subghz_protocol_decoder_raw_free(engine->raw_decoder);
        engine->raw_decoder = NULL;
        return false;
    }
    return true;
}

// --- lifecycle ----------------------------------------------------------------

void rf_tools_engine_reselect_device(RfToolsEngine* engine) {
    SubGhzRadioDeviceType want = (engine->settings->radio_choice == RfToolsRadioInternal) ?
                                      SubGhzRadioDeviceTypeInternal :
                                      SubGhzRadioDeviceTypeExternalCC1101;

    const SubGhzDevice* device = radio_device_loader_set(engine->device, want);
    const SubGhzDevice* ext_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    bool is_external = (ext_device != NULL) && (device == ext_device);

    if(!is_external) {
        // radio_device_loader_set() only begin()s the external path itself; the internal
        // module (and the "requested External but nothing answered" fallback) needs its own.
        subghz_devices_begin(device);
    }

    engine->device = device;
    engine->device_is_external = is_external;
}

RfToolsEngine* rf_tools_engine_alloc(NotificationApp* notifications, Storage* storage) {
    RfToolsEngine* engine = malloc(sizeof(RfToolsEngine));
    memset(engine, 0, sizeof(RfToolsEngine));

    engine->storage = storage;
    engine->notifications = notifications;
    engine->settings = rf_tools_settings_alloc();
    engine->capture_decoded_fff = flipper_format_string_alloc();
    engine->capture_decoded_name = furi_string_alloc();
    engine->capture_decoded_detail = furi_string_alloc();
    engine->capture_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    engine->setting = subghz_setting_alloc();
    subghz_setting_load(engine->setting, EXT_PATH("subghz/assets/setting_user"));

    engine->environment = subghz_environment_alloc();
    subghz_environment_load_keystore(engine->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(engine->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    // Came Atomo / Alutech AT-4N / Nice Flor S rolling-code decoders need these rainbow-table
    // paths set, or they never produce a match - this is what makes decode coverage match the
    // stock SubGhz app's instead of silently missing those three protocols.
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
        engine->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(
        engine->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
    subghz_environment_set_came_atomo_rainbow_table_file_name(
        engine->environment, SUBGHZ_CAME_ATOMO_DIR_NAME);
    subghz_environment_set_protocol_registry(engine->environment, (void*)&subghz_protocol_registry);

    subghz_devices_init();
    engine->device = NULL;
    rf_tools_engine_reselect_device(engine);

    rf_tools_settings_load(engine->settings, storage, engine->setting, engine->device);
    rf_tools_engine_reselect_device(engine); // re-apply in case the config picked a different radio

    engine->worker = subghz_worker_alloc();
    subghz_worker_set_context(engine->worker, engine);
    subghz_worker_set_pair_callback(engine->worker, rf_tools_worker_pair_callback);
    subghz_worker_set_overrun_callback(engine->worker, rf_tools_worker_overrun_callback);

    engine->receiver = subghz_receiver_alloc_init(engine->environment);
    subghz_receiver_set_rx_callback(engine->receiver, rf_tools_protocol_callback, engine);
    // subghz_receiver_alloc_init() leaves the filter as whatever garbage was on the heap -
    // every stock caller sets this explicitly. SubGhzProtocolFlag_Decodable matches any
    // normal protocol regardless of band.
    subghz_receiver_set_filter(engine->receiver, SubGhzProtocolFlag_Decodable);

    return engine;
}

void rf_tools_engine_free(RfToolsEngine* engine) {
    if(engine->capture_running) rf_tools_engine_stop_capture(engine);

    if(engine->raw_decoder) {
        subghz_protocol_decoder_raw_free(engine->raw_decoder);
        engine->raw_decoder = NULL;
    }
    if(engine->receiver) subghz_receiver_free(engine->receiver);
    if(engine->worker) subghz_worker_free(engine->worker);
    if(engine->device) {
        subghz_devices_sleep(engine->device);
        radio_device_loader_end(engine->device);
    }
    subghz_devices_deinit();

    if(engine->environment) subghz_environment_free(engine->environment);
    if(engine->setting) subghz_setting_free(engine->setting);

    furi_mutex_free(engine->capture_mutex);
    flipper_format_free(engine->capture_decoded_fff);
    furi_string_free(engine->capture_decoded_name);
    furi_string_free(engine->capture_decoded_detail);
    rf_tools_settings_free(engine->settings);
    free(engine);
}

// --- capture -----------------------------------------------------------------

bool rf_tools_engine_start_decode_capture(RfToolsEngine* engine, uint32_t frequency) {
    if(engine->capture_running) return false;

    rf_tools_apply_preset_and_frequency(engine, frequency);
    subghz_receiver_reset(engine->receiver);

    engine->capture_mode = RfToolsCaptureModeDecode;
    engine->capture_got_decode = false;
    engine->capture_snapshot_valid = false;
    engine->capture_frequency = frequency;
    subghz_custom_btns_reset();
    engine->debug_overrun_count = 0;
    if(engine->raw_decoder) {
        subghz_protocol_decoder_raw_free(engine->raw_decoder);
        engine->raw_decoder = NULL;
    }

    furi_hal_power_suppress_charge_enter();
    subghz_devices_flush_rx(engine->device);
    subghz_devices_start_async_rx(engine->device, subghz_worker_rx_callback, engine->worker);
    subghz_worker_start(engine->worker);

    engine->capture_running = true;
    engine->capture_start_tick = furi_get_tick();
    notification_message(engine->notifications, &sequence_blink_start_cyan);
    return true;
}

bool rf_tools_engine_start_raw_capture(RfToolsEngine* engine, uint32_t frequency) {
    if(engine->capture_running) return false;

    rf_tools_apply_preset_and_frequency(engine, frequency);
    subghz_receiver_reset(engine->receiver);

    if(!rf_tools_start_raw_stream(engine, frequency)) {
        subghz_devices_idle(engine->device);
        return false;
    }

    engine->capture_mode = RfToolsCaptureModeRaw;
    engine->capture_got_decode = false;
    engine->capture_snapshot_valid = false;
    engine->capture_frequency = frequency;
    subghz_custom_btns_reset();

    furi_hal_power_suppress_charge_enter();
    subghz_devices_flush_rx(engine->device);
    subghz_devices_start_async_rx(engine->device, subghz_worker_rx_callback, engine->worker);
    subghz_worker_start(engine->worker);

    engine->capture_running = true;
    engine->capture_start_tick = furi_get_tick();
    notification_message(engine->notifications, &sequence_blink_start_cyan);
    return true;
}

void rf_tools_engine_stop_capture(RfToolsEngine* engine) {
    if(!engine->capture_running) return;

    subghz_devices_stop_async_rx(engine->device);
    if(subghz_worker_is_running(engine->worker)) {
        subghz_worker_stop(engine->worker);
    }

    if(engine->raw_decoder) {
        subghz_protocol_raw_save_to_file_stop(engine->raw_decoder);
    }

    subghz_devices_idle(engine->device);
    furi_hal_power_suppress_charge_exit();
    notification_message(engine->notifications, &sequence_blink_stop);

    engine->capture_running = false;
}

size_t rf_tools_engine_raw_sample_count(RfToolsEngine* engine) {
    return engine->raw_decoder ? subghz_protocol_raw_get_sample_write(engine->raw_decoder) : 0;
}

bool rf_tools_engine_capture_got_decode(RfToolsEngine* engine) {
    furi_mutex_acquire(engine->capture_mutex, FuriWaitForever);
    bool got = engine->capture_got_decode;
    furi_mutex_release(engine->capture_mutex);
    return got;
}

void rf_tools_engine_clear_decode(RfToolsEngine* engine) {
    furi_mutex_acquire(engine->capture_mutex, FuriWaitForever);
    engine->capture_got_decode = false;
    furi_mutex_release(engine->capture_mutex);
}

void rf_tools_engine_finalize_decoded_result(RfToolsEngine* engine, bool* out_is_raw, bool* out_has_raw_fallback) {
    size_t raw_samples = rf_tools_engine_raw_sample_count(engine);
    bool has_raw_fallback = raw_samples > 0;
    bool is_raw = has_raw_fallback && engine->settings->tx_prefer == RfToolsTxRaw;

    if(has_raw_fallback && !is_raw) {
        // Preferred the clean re-encode; the parallel raw capture made it to disk but won't
        // be used - don't leave it cluttering /ext/subghz.
        FuriString* tmp_path = furi_string_alloc();
        rf_tools_raw_tmp_path(tmp_path);
        storage_simply_remove(engine->storage, furi_string_get_cstr(tmp_path));
        furi_string_free(tmp_path);
    }

    if(out_is_raw) *out_is_raw = is_raw;
    if(out_has_raw_fallback) *out_has_raw_fallback = has_raw_fallback;
}

// --- save --------------------------------------------------------------------

void rf_tools_engine_default_save_name(RfToolsEngine* engine, bool is_raw, char* buf, size_t buf_size) {
    if(is_raw) {
        name_generator_make_auto_datetime(buf, buf_size, "RAW", NULL);
    } else {
        name_generator_make_auto_datetime(buf, buf_size, furi_string_get_cstr(engine->capture_decoded_name), NULL);
    }
}

bool rf_tools_engine_save_decoded_as(RfToolsEngine* engine, uint32_t frequency, const char* full_path) {
    UNUSED(frequency);
    if(!engine->capture_snapshot_valid) return false;
    storage_simply_mkdir(engine->storage, RF_TOOLS_SAVE_FOLDER);
    Stream* snapshot = flipper_format_get_raw_stream(engine->capture_decoded_fff);
    stream_rewind(snapshot);
    size_t size = stream_size(snapshot);
    return size > 0 &&
           stream_save_to_file(snapshot, engine->storage, full_path, FSOM_CREATE_ALWAYS) == size;
}

bool rf_tools_engine_save_raw_as(RfToolsEngine* engine, const char* full_path) {
    storage_simply_mkdir(engine->storage, RF_TOOLS_SAVE_FOLDER);

    FuriString* tmp_path = furi_string_alloc();
    rf_tools_raw_tmp_path(tmp_path);

    storage_simply_remove(engine->storage, full_path);
    bool ok =
        storage_common_rename(engine->storage, furi_string_get_cstr(tmp_path), full_path) == FSE_OK;

    furi_string_free(tmp_path);
    return ok;
}

void rf_tools_engine_save_decoded(RfToolsEngine* engine, uint32_t frequency, FuriString* out_saved_path) {
    furi_string_reset(out_saved_path);
    char namebuf[RF_TOOLS_MAX_LEN_NAME] = {0};
    rf_tools_engine_default_save_name(engine, false, namebuf, sizeof(namebuf));
    FuriString* final_path = furi_string_alloc();
    furi_string_printf(final_path, "%s/%s%s", RF_TOOLS_SAVE_FOLDER, namebuf, SUBGHZ_APP_FILENAME_EXTENSION);
    if(rf_tools_engine_save_decoded_as(engine, frequency, furi_string_get_cstr(final_path))) {
        furi_string_set(out_saved_path, final_path);
    }
    furi_string_free(final_path);
}

void rf_tools_engine_save_raw(RfToolsEngine* engine, FuriString* out_saved_path) {
    furi_string_reset(out_saved_path);
    char namebuf[RF_TOOLS_MAX_LEN_NAME] = {0};
    rf_tools_engine_default_save_name(engine, true, namebuf, sizeof(namebuf));
    FuriString* final_path = furi_string_alloc();
    furi_string_printf(final_path, "%s/%s%s", RF_TOOLS_SAVE_FOLDER, namebuf, SUBGHZ_APP_FILENAME_EXTENSION);
    if(rf_tools_engine_save_raw_as(engine, furi_string_get_cstr(final_path))) {
        furi_string_set(out_saved_path, final_path);
    }
    furi_string_free(final_path);
}

// --- transmit --------------------------------------------------------------------

// A .sub file's "Preset" field is written as the long FuriHalSubGhzPresetXxx name (or
// "FuriHalSubGhzPresetCustom"), but subghz_setting's registry is keyed by the short name
// ("AM650" etc, or "CUSTOM") - this is the same translation the stock app does on load
// (applications/main/subghz/helpers/subghz_txrx.c: subghz_txrx_get_preset_name).
static const char* rf_tools_preset_long_to_short(const char* preset) {
    if(!strcmp(preset, "FuriHalSubGhzPresetOok270Async")) return "AM270";
    if(!strcmp(preset, "FuriHalSubGhzPresetOok650Async")) return "AM650";
    if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev238Async")) return "FM238";
    if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev12KAsync")) return "FM12K";
    if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev476Async")) return "FM476";
    if(!strcmp(preset, "FuriHalSubGhzPresetCustom")) return "CUSTOM";
    return preset; // already short, or unrecognized
}

bool rf_tools_engine_apply_preset_from_file(RfToolsEngine* engine, FlipperFormat* fff) {
    FuriString* preset_str = furi_string_alloc();
    flipper_format_rewind(fff);
    bool have_preset = flipper_format_read_string(fff, "Preset", preset_str);
    bool ok = false;

    if(have_preset) {
        const char* short_name = rf_tools_preset_long_to_short(furi_string_get_cstr(preset_str));
        if(!strcmp(short_name, "CUSTOM")) {
            subghz_setting_delete_custom_preset(engine->setting, "CUSTOM");
            // Custom_preset_module/Custom_preset_data follow "Preset" immediately in the file -
            // this call expects the stream positioned right there, so no rewind in between.
            if(subghz_setting_load_custom_preset(engine->setting, "CUSTOM", fff)) {
                furi_string_set(engine->settings->preset_name, "CUSTOM");
                ok = true;
            }
        } else if(subghz_setting_get_inx_preset_by_name(engine->setting, short_name) >= 0) {
            furi_string_set(engine->settings->preset_name, short_name);
            ok = true;
        }
    }

    furi_string_free(preset_str);
    return ok;
}

void rf_tools_engine_request_tx_abort(RfToolsEngine* engine) {
    engine->tx_abort = true;
}

// Mirrors applications/main/subghz/helpers/subghz_txrx.c's subghz_txrx_set_tx_power(), called
// with its "0 / default" selection - which that function maps to its OWN table's max entry
// (0xC0), not to "whatever the preset blob happens to already contain". Every preset's raw
// register dump bakes some PA value in at a fixed byte offset from the end of the blob, and
// that baked-in value is not guaranteed to be the chip's highest setting - the LED can still
// show TX while the actual output power is weak. The stock app always overrides this byte
// before transmitting; we never did, so this is forced on every TX here instead of being a
// user-configurable "TX Power" setting, per explicit request to always use max power.
#define RF_TOOLS_PRESET_POWER_OFFSET_FM 8
#define RF_TOOLS_PRESET_POWER_OFFSET_AM 7
#define RF_TOOLS_PA_MAX                 0xC0 // 12dBm (AM table) / 10dBm (FM table)

static void rf_tools_force_max_tx_power(uint8_t* preset_data, size_t preset_data_size) {
    if(!preset_data || preset_data_size <= RF_TOOLS_PRESET_POWER_OFFSET_FM) return;
    uint8_t fm_byte = preset_data[preset_data_size - RF_TOOLS_PRESET_POWER_OFFSET_FM];
    uint8_t am_byte = preset_data[preset_data_size - RF_TOOLS_PRESET_POWER_OFFSET_AM];
    if(fm_byte && !am_byte) {
        preset_data[preset_data_size - RF_TOOLS_PRESET_POWER_OFFSET_FM] = RF_TOOLS_PA_MAX;
    } else if(am_byte && !fm_byte) {
        preset_data[preset_data_size - RF_TOOLS_PRESET_POWER_OFFSET_AM] = RF_TOOLS_PA_MAX;
    }
}

// Loads the preset/frequency onto the radio ONCE for the whole Send operation. Previously this
// (reset + idle + load_preset + idle + set_frequency) ran again inside the per-repeat function
// below, meaning a multi-repeat Send fully reset and reconfigured the CC1101 between every
// single repeat - stock's own repeat mechanism (SubGhzBlockGeneric's endless_tx /
// encoder.repeat) never tears the radio down between repeats at all, it stays keyed in one
// continuous async session. Re-reset/reload on every repeat is at minimum wasted settling time
// the PLL/AGC has to re-acquire before each burst that stock never pays - worth eliminating
// regardless of whether it's the full explanation for a bad transmission.
static bool rf_tools_tx_setup_radio(RfToolsEngine* engine, uint32_t frequency) {
    size_t preset_size = 0;
    uint8_t* preset_data = rf_tools_engine_current_preset_data(engine, &preset_size);
    if(!preset_data) {
        FURI_LOG_E(RF_TOOLS_TAG, "No preset data for '%s'", furi_string_get_cstr(engine->settings->preset_name));
        return false;
    }
    rf_tools_force_max_tx_power(preset_data, preset_size);

    if(!engine->device_is_external && !furi_hal_subghz_is_tx_allowed(frequency)) {
        // Mirrors subghz_i.c's own load-time check (SubGhzLoadKeyStateOnlyRx) - the internal
        // module's region lock, not something an external CC1101 is bound by.
        FURI_LOG_E(RF_TOOLS_TAG, "%lu Hz is RX-only on the internal radio, refusing TX", frequency);
        return false;
    }

    subghz_devices_reset(engine->device);
    subghz_devices_idle(engine->device);
    subghz_devices_load_preset(engine->device, FuriHalSubGhzPresetCustom, preset_data);
    subghz_devices_idle(engine->device);
    subghz_devices_set_frequency(engine->device, frequency);
    return true;
}

// Keys the radio and feeds exactly one burst (one SubGhzTransmitter, start to finish) - the
// radio is assumed to already be configured by rf_tools_tx_setup_radio().
static bool rf_tools_tx_frame(RfToolsEngine* engine, SubGhzTransmitter* transmitter) {
    bool ok = false;
    FURI_LOG_W(RF_TOOLS_TAG, "tx_frame: calling set_tx(), tick=%lu", furi_get_tick());
    if(subghz_devices_set_tx(engine->device)) {
        FURI_LOG_W(RF_TOOLS_TAG, "tx_frame: set_tx ok, calling start_async_tx(), tick=%lu", furi_get_tick());
        if(subghz_devices_start_async_tx(engine->device, subghz_transmitter_yield, transmitter)) {
            FURI_LOG_W(RF_TOOLS_TAG, "tx_frame: start_async_tx ok, waiting, tick=%lu", furi_get_tick());
            uint32_t waited_ms = 0;
            while(!subghz_devices_is_async_complete_tx(engine->device) && waited_ms < RF_TOOLS_TX_TIMEOUT_MS &&
                  !engine->tx_abort) {
                furi_delay_ms(2);
                waited_ms += 2;
            }
            ok = subghz_devices_is_async_complete_tx(engine->device) && !engine->tx_abort;
            FURI_LOG_W(
                RF_TOOLS_TAG,
                "tx_frame: wait done after %lums (ok=%d abort=%d), tick=%lu",
                waited_ms,
                ok,
                engine->tx_abort,
                furi_get_tick());
            subghz_devices_stop_async_tx(engine->device);
        } else {
            FURI_LOG_E(RF_TOOLS_TAG, "tx_frame: start_async_tx() refused, tick=%lu", furi_get_tick());
        }
    } else {
        FURI_LOG_E(RF_TOOLS_TAG, "set_tx() refused");
    }
    return ok;
}

bool rf_tools_engine_transmit_fff(RfToolsEngine* engine, FlipperFormat* fff) {
    // Preset is NOT read from fff here - it's already active (either the live settings
    // preset, or whatever rf_tools_engine_apply_preset_from_file() set it to). This mirrors
    // subghz_txrx_tx_start(), which transmits using the TxRx instance's already-resolved
    // preset rather than re-deriving it from the file on every attempt.
    subghz_custom_btns_reset();

    FuriString* protocol = furi_string_alloc();
    flipper_format_rewind(fff);
    bool have_protocol = flipper_format_read_string(fff, "Protocol", protocol);

    if(have_protocol && strcmp(furi_string_get_cstr(protocol), SUBGHZ_PROTOCOL_RAW_NAME) != 0) {
        uint32_t internal_repeats = RF_TOOLS_TX_INTERNAL_REPEATS;
        flipper_format_insert_or_update_uint32(fff, "Repeat", &internal_repeats, 1);
    }

    FURI_LOG_W(RF_TOOLS_TAG, "transmit_fff: setup_radio starting, tick=%lu", furi_get_tick());
    bool any_ok = false;
    if(have_protocol && rf_tools_tx_setup_radio(engine, engine->settings->frequency)) {
        FURI_LOG_W(RF_TOOLS_TAG, "transmit_fff: setup_radio done, tick=%lu", furi_get_tick());
        uint32_t repeats = engine->settings->repeat_count ? engine->settings->repeat_count : 1;
        uint32_t gap_ms =
            engine->settings->repeat_delay_ms ? engine->settings->repeat_delay_ms : RF_TOOLS_DEFAULT_TX_GAP_MS;
        furi_hal_power_suppress_charge_enter();
        for(uint32_t i = 0; i < repeats && !engine->tx_abort; i++) {
            SubGhzTransmitter* tx = subghz_transmitter_alloc_init(engine->environment, furi_string_get_cstr(protocol));
            if(tx) {
                flipper_format_rewind(fff);
                if(subghz_transmitter_deserialize(tx, fff) == SubGhzProtocolStatusOk) {
                    if(rf_tools_tx_frame(engine, tx)) any_ok = true;
                } else {
                    FURI_LOG_E(RF_TOOLS_TAG, "Transmitter deserialize failed for %s", furi_string_get_cstr(protocol));
                }
                subghz_transmitter_stop(tx);
                subghz_transmitter_free(tx);
            } else {
                FURI_LOG_E(RF_TOOLS_TAG, "No encoder for protocol %s", furi_string_get_cstr(protocol));
            }
            if(i + 1 < repeats) {
                for(uint32_t waited = 0; waited < gap_ms && !engine->tx_abort; waited += 10) {
                    furi_delay_ms(MIN(10u, gap_ms - waited));
                }
            }
        }
        furi_hal_power_suppress_charge_exit();
        subghz_devices_idle(engine->device);
    } else if(!have_protocol) {
        FURI_LOG_E(RF_TOOLS_TAG, "No Protocol field in fff being transmitted");
    }

    furi_string_free(protocol);
    return any_ok && !engine->tx_abort;
}

// RAW's own encoder (subghz_protocol_encoder_raw_deserialize, lib/subghz/protocols/raw.c)
// does NOT read "RAW_Data" out of the fff handed to it - it reads "File_name" +
// "Radio_device_name" and streams the actual samples back off disk via
// subghz_file_encoder_worker. subghz_protocol_raw_gen_fff_data() is what builds that small
// three-field pointer record; skipping it (passing the real capture file straight to
// subghz_transmitter_deserialize) makes deserialize fail on a missing "File_name", so the
// encoder's is_running stays false and TX completes almost instantly with no real content -
// this is why a 128-edge capture was transmitting what looked like "2" of something.
static bool rf_tools_transmit_raw_from_path(RfToolsEngine* engine, const char* raw_file_path) {
    FlipperFormat* wrapper = flipper_format_string_alloc();
    subghz_protocol_raw_gen_fff_data(wrapper, raw_file_path, subghz_devices_get_name(engine->device));
    bool ok = rf_tools_engine_transmit_fff(engine, wrapper);
    flipper_format_free(wrapper);
    return ok;
}

bool rf_tools_engine_transmit_file(RfToolsEngine* engine, const char* path) {
    FlipperFormat* fff = flipper_format_file_alloc(engine->storage);
    bool opened = flipper_format_file_open_existing(fff, path);
    bool is_raw = false;
    if(opened) {
        opened = rf_tools_engine_apply_preset_from_file(engine, fff);
        flipper_format_rewind(fff);
        FuriString* protocol = furi_string_alloc();
        if(flipper_format_read_string(fff, "Protocol", protocol)) {
            is_raw = !strcmp(furi_string_get_cstr(protocol), SUBGHZ_PROTOCOL_RAW_NAME);
        }
        furi_string_free(protocol);
    }
    flipper_format_file_close(fff);
    flipper_format_free(fff);
    if(!opened) return false;

    // The file has to be fully closed before handing its path to the RAW encoder, which
    // opens it again itself via subghz_file_encoder_worker.
    if(is_raw) return rf_tools_transmit_raw_from_path(engine, path);

    fff = flipper_format_string_alloc();
    bool loaded = stream_load_from_file(flipper_format_get_raw_stream(fff), engine->storage, path) > 0;
    bool ok = loaded && rf_tools_engine_transmit_fff(engine, fff);
    flipper_format_free(fff);
    return ok;
}

bool rf_tools_engine_transmit_decoded(RfToolsEngine* engine, uint32_t frequency) {
    UNUSED(frequency);
    if(!engine->capture_snapshot_valid) return false;
    // Encoders can update Key/Repeat. Give each Send its own mutable copy.
    FlipperFormat* tx_data = flipper_format_string_alloc();
    Stream* snapshot = flipper_format_get_raw_stream(engine->capture_decoded_fff);
    bool copied = stream_copy_full(snapshot, flipper_format_get_raw_stream(tx_data)) == stream_size(snapshot);
    bool ok = copied && rf_tools_engine_transmit_fff(engine, tx_data);
    flipper_format_free(tx_data);
    return ok;
}

bool rf_tools_engine_transmit_raw_tmp(RfToolsEngine* engine) {
    FuriString* tmp_path = furi_string_alloc();
    rf_tools_raw_tmp_path(tmp_path);
    bool ok = rf_tools_engine_transmit_file(engine, furi_string_get_cstr(tmp_path));
    furi_string_free(tmp_path);
    return ok;
}

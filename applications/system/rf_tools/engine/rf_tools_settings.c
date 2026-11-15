#include "rf_tools_settings.h"
#include <lib/flipper_format/flipper_format.h>

#define RF_TOOLS_CONFIG_FILE_TYPE    "RF-Tools Config File"
#define RF_TOOLS_CONFIG_FILE_VERSION 1

RfToolsSettings* rf_tools_settings_alloc(void) {
    RfToolsSettings* settings = malloc(sizeof(RfToolsSettings));
    memset(settings, 0, sizeof(RfToolsSettings));
    settings->preset_name = furi_string_alloc();
    return settings;
}

void rf_tools_settings_free(RfToolsSettings* settings) {
    furi_string_free(settings->preset_name);
    free(settings);
}

const char* rf_tools_settings_radio_choice_name(RfToolsRadioChoice choice) {
    switch(choice) {
    case RfToolsRadioInternal:
        return "Internal";
    case RfToolsRadioExternal:
        return "External";
    default:
        return "Auto";
    }
}

const char* rf_tools_settings_save_mode_name(RfToolsSaveMode mode) {
    switch(mode) {
    case RfToolsSaveAuto:
        return "Auto";
    case RfToolsSaveOff:
        return "Off";
    default:
        return "Ask";
    }
}

const char* rf_tools_settings_tx_prefer_name(RfToolsTxRepresentation rep) {
    return (rep == RfToolsTxRaw) ? "Raw" : "Clean";
}

static RfToolsRadioChoice rf_tools_settings_radio_choice_from_name(const char* name) {
    if(!strcmp(name, "Internal")) return RfToolsRadioInternal;
    if(!strcmp(name, "External")) return RfToolsRadioExternal;
    return RfToolsRadioAuto;
}

static RfToolsSaveMode rf_tools_settings_save_mode_from_name(const char* name) {
    if(!strcmp(name, "Auto")) return RfToolsSaveAuto;
    if(!strcmp(name, "Off")) return RfToolsSaveOff;
    return RfToolsSaveAsk;
}

static RfToolsTxRepresentation rf_tools_settings_tx_prefer_from_name(const char* name) {
    return !strcmp(name, "Raw") ? RfToolsTxRaw : RfToolsTxClean;
}

void rf_tools_settings_step_frequency(RfToolsSettings* settings, SubGhzSetting* setting, int dir) {
    size_t count = subghz_setting_get_frequency_count(setting);
    if(count == 0) return;

    size_t idx = 0;
    for(size_t i = 0; i < count; i++) {
        if(subghz_setting_get_frequency(setting, i) == settings->frequency) {
            idx = i;
            break;
        }
    }

    int v = (int)idx + dir;
    if(v < 0) v = (int)count - 1;
    if(v >= (int)count) v = 0;
    settings->frequency = subghz_setting_get_frequency(setting, (size_t)v);
}

static void rf_tools_settings_defaults(RfToolsSettings* settings, SubGhzSetting* setting) {
    settings->radio_choice = RfToolsRadioAuto;
    settings->repeat_count = RF_TOOLS_DEFAULT_REPEAT;
    settings->repeat_delay_ms = RF_TOOLS_DEFAULT_TX_GAP_MS;
    settings->save_mode = RfToolsSaveAsk;
    settings->tx_prefer = RfToolsTxClean;
    settings->auto_tx = false;
    settings->frequency = subghz_setting_get_default_frequency(setting);

    int idx = subghz_setting_get_inx_preset_by_name(setting, "AM650");
    if(idx < 0) idx = 0;
    if(subghz_setting_get_preset_count(setting) > 0) {
        furi_string_set(settings->preset_name, subghz_setting_get_preset_name(setting, idx));
    } else {
        furi_string_set(settings->preset_name, "AM650");
    }
}

void rf_tools_settings_load(
    RfToolsSettings* settings,
    Storage* storage,
    SubGhzSetting* setting,
    const SubGhzDevice* device) {
    rf_tools_settings_defaults(settings, setting);

    FlipperFormat* fff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_existing(fff, RF_TOOLS_CONFIG_PATH)) break;

        uint32_t version = 0;
        FuriString* file_type = furi_string_alloc();
        if(!flipper_format_read_header(fff, file_type, &version)) {
            furi_string_free(file_type);
            break;
        }
        furi_string_free(file_type);

        FuriString* temp = furi_string_alloc();
        if(flipper_format_read_string(fff, "Radio", temp)) {
            settings->radio_choice = rf_tools_settings_radio_choice_from_name(furi_string_get_cstr(temp));
        }

        uint32_t freq = 0;
        if(flipper_format_read_uint32(fff, "Frequency", &freq, 1) &&
           subghz_devices_is_frequency_valid(device, freq)) {
            settings->frequency = freq;
        }

        if(flipper_format_read_string(fff, "Preset", temp)) {
            if(subghz_setting_get_inx_preset_by_name(setting, furi_string_get_cstr(temp)) >= 0) {
                furi_string_set(settings->preset_name, temp);
            }
        }

        uint32_t repeat = 0;
        if(flipper_format_read_uint32(fff, "Repeat", &repeat, 1) && repeat > 0 && repeat <= 10) {
            settings->repeat_count = repeat;
        }

        uint32_t delay = 0;
        if(flipper_format_read_uint32(fff, "RepeatDelay", &delay, 1) && delay >= 1000 && delay <= 30000) {
            settings->repeat_delay_ms = delay;
        }

        if(flipper_format_read_string(fff, "SaveMode", temp)) {
            settings->save_mode = rf_tools_settings_save_mode_from_name(furi_string_get_cstr(temp));
        }

        if(flipper_format_read_string(fff, "TxPrefer", temp)) {
            settings->tx_prefer = rf_tools_settings_tx_prefer_from_name(furi_string_get_cstr(temp));
        }

        bool auto_tx = false;
        if(flipper_format_read_bool(fff, "AutoTx", &auto_tx, 1)) {
            settings->auto_tx = auto_tx;
        }

        furi_string_free(temp);
    } while(false);

    flipper_format_free(fff);
}

void rf_tools_settings_save(RfToolsSettings* settings, Storage* storage) {
    storage_simply_mkdir(storage, RF_TOOLS_CONFIG_DIR);

    FlipperFormat* fff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_always(fff, RF_TOOLS_CONFIG_PATH)) break;
        if(!flipper_format_write_header_cstr(
               fff, RF_TOOLS_CONFIG_FILE_TYPE, RF_TOOLS_CONFIG_FILE_VERSION))
            break;
        if(!flipper_format_write_string_cstr(
               fff, "Radio", rf_tools_settings_radio_choice_name(settings->radio_choice)))
            break;
        if(!flipper_format_write_uint32(fff, "Frequency", &settings->frequency, 1)) break;
        if(!flipper_format_write_string_cstr(fff, "Preset", furi_string_get_cstr(settings->preset_name)))
            break;
        if(!flipper_format_write_uint32(fff, "Repeat", &settings->repeat_count, 1)) break;
        if(!flipper_format_write_uint32(fff, "RepeatDelay", &settings->repeat_delay_ms, 1)) break;
        if(!flipper_format_write_string_cstr(
               fff, "SaveMode", rf_tools_settings_save_mode_name(settings->save_mode)))
            break;
        if(!flipper_format_write_string_cstr(
               fff, "TxPrefer", rf_tools_settings_tx_prefer_name(settings->tx_prefer)))
            break;
        if(!flipper_format_write_bool(fff, "AutoTx", &settings->auto_tx, 1)) break;
    } while(false);

    flipper_format_file_close(fff);
    flipper_format_free(fff);
}

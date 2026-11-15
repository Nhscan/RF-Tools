#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/devices/devices.h>

#define RF_TOOLS_CONFIG_PATH EXT_PATH("apps_data/rf_tools/rf_tools.conf")
#define RF_TOOLS_CONFIG_DIR  EXT_PATH("apps_data/rf_tools")
#define RF_TOOLS_DEFAULT_REPEAT     3
#define RF_TOOLS_DEFAULT_TX_GAP_MS  1000 // 1s - adjustable 1s..30s on the Repeater Settings screen

typedef enum {
    RfToolsRadioAuto,
    RfToolsRadioInternal,
    RfToolsRadioExternal,
} RfToolsRadioChoice;

typedef enum {
    RfToolsSaveAsk, // show the result screen's Save button (default)
    RfToolsSaveAuto, // save automatically, no prompt
    RfToolsSaveOff, // never save
} RfToolsSaveMode;

typedef enum {
    RfToolsTxClean, // re-encode from the decoded protocol (default)
    RfToolsTxRaw, // replay the captured raw edges instead
} RfToolsTxRepresentation;

typedef struct {
    RfToolsRadioChoice radio_choice;
    uint32_t frequency; // Hz - live, adjustable with Left/Right on most screens
    FuriString* preset_name; // short name recognized by SubGhzSetting, e.g. "AM650" or "CUSTOM"
    uint32_t repeat_count;
    uint32_t repeat_delay_ms;
    RfToolsSaveMode save_mode;
    RfToolsTxRepresentation tx_prefer;
    bool auto_tx; // Repeater only: transmit immediately on decode, then resume listening
} RfToolsSettings;

RfToolsSettings* rf_tools_settings_alloc(void);
void rf_tools_settings_free(RfToolsSettings* settings);

// Fills settings with sane defaults, then overlays the config file on SD if present.
void rf_tools_settings_load(
    RfToolsSettings* settings,
    Storage* storage,
    SubGhzSetting* setting,
    const SubGhzDevice* device);

void rf_tools_settings_save(RfToolsSettings* settings, Storage* storage);

const char* rf_tools_settings_radio_choice_name(RfToolsRadioChoice choice);
const char* rf_tools_settings_save_mode_name(RfToolsSaveMode mode);
const char* rf_tools_settings_tx_prefer_name(RfToolsTxRepresentation rep);

// Steps settings->frequency to the next/previous entry in the setting's frequency list
// (wrapping).
void rf_tools_settings_step_frequency(RfToolsSettings* settings, SubGhzSetting* setting, int dir);

#include "../rf_tools_i.h"
#include "../helpers/radio_device_loader.h"
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>

// Device row ported from applications/main/subghz/scenes/subghz_scene_radio_settings.c:
// hide "External" entirely when nothing answers on SPI, and if a requested External still
// falls back to Internal, relabel the row to what was actually picked rather than trust the
// index blindly.

#define RADIO_DEVICE_COUNT    2
#define RF_TOOLS_REPEAT_COUNT 10 // 1..10 retransmits - mirrors Repeater Settings' own control
#define RF_TOOLS_DELAY_COUNT  30 // 1s steps, 1..30s - same shared setting, same range
static const char* const radio_device_text[RADIO_DEVICE_COUNT] = {"Internal", "External"};
static const RfToolsRadioChoice radio_device_value[RADIO_DEVICE_COUNT] = {
    RfToolsRadioInternal,
    RfToolsRadioExternal,
};

typedef enum {
    RfToolsRadioSettingsIndexDevice,
    RfToolsRadioSettingsIndexFrequency,
    RfToolsRadioSettingsIndexPreset,
} RfToolsRadioSettingsIndex;

static void rf_tools_scene_radio_settings_set_device(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    app->engine->settings->radio_choice = radio_device_value[index];
    rf_tools_engine_reselect_device(app->engine);

    bool is_external = app->engine->device_is_external;
    uint8_t shown = is_external ? 1 : 0;
    variable_item_set_current_value_index(item, shown);
    variable_item_set_current_value_text(item, radio_device_text[shown]);
}

static void rf_tools_scene_radio_settings_set_frequency(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->frequency = subghz_setting_get_frequency(app->engine->setting, index);

    FuriString* text = furi_string_alloc();
    furi_string_printf(
        text, "%lu.%02lu", app->engine->settings->frequency / 1000000, (app->engine->settings->frequency / 10000) % 100);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void rf_tools_scene_radio_settings_set_preset(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    const char* name = subghz_setting_get_preset_name(app->engine->setting, index);
    furi_string_set(app->engine->settings->preset_name, name);
    variable_item_set_current_value_text(item, name);
}

// Repeat/Delay are global - every Send button in the app uses them, not just Repeater's Listen
// - so they're exposed here too (same underlying RfToolsSettings fields as Repeater Settings'
// own identical rows, just reachable from the general settings screen as well).
static void rf_tools_scene_radio_settings_set_repeat(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->repeat_count = (uint32_t)(index + 1);

    FuriString* text = furi_string_alloc();
    furi_string_printf(text, "%lux", app->engine->settings->repeat_count);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void rf_tools_scene_radio_settings_set_delay(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->repeat_delay_ms = (uint32_t)(index + 1) * 1000;

    FuriString* text = furi_string_alloc();
    furi_string_printf(text, "%lus", app->engine->settings->repeat_delay_ms / 1000);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

void rf_tools_scene_radio_settings_on_enter(void* context) {
    RfToolsApp* app = context;
    VariableItem* item;

    uint8_t device_count = RADIO_DEVICE_COUNT;
    if(!app->engine->device_is_external &&
       !radio_device_loader_is_connect_external(SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        device_count = 1;
    }
    item = variable_item_list_add(
        app->var_item_list, "Device", device_count, rf_tools_scene_radio_settings_set_device, app);
    variable_item_set_current_value_index(item, app->engine->device_is_external ? 1 : 0);
    variable_item_set_current_value_text(
        item, radio_device_text[app->engine->device_is_external ? 1 : 0]);

    size_t freq_count = subghz_setting_get_frequency_count(app->engine->setting);
    item = variable_item_list_add(
        app->var_item_list, "Frequency", freq_count, rf_tools_scene_radio_settings_set_frequency, app);
    size_t freq_idx = 0;
    for(size_t i = 0; i < freq_count; i++) {
        if(subghz_setting_get_frequency(app->engine->setting, i) == app->engine->settings->frequency) {
            freq_idx = i;
            break;
        }
    }
    variable_item_set_current_value_index(item, freq_idx);
    FuriString* freq_text = furi_string_alloc();
    furi_string_printf(
        freq_text, "%lu.%02lu", app->engine->settings->frequency / 1000000, (app->engine->settings->frequency / 10000) % 100);
    variable_item_set_current_value_text(item, furi_string_get_cstr(freq_text));
    furi_string_free(freq_text);

    size_t preset_count = subghz_setting_get_preset_count(app->engine->setting);
    item = variable_item_list_add(
        app->var_item_list, "Preset", preset_count, rf_tools_scene_radio_settings_set_preset, app);
    int preset_idx = subghz_setting_get_inx_preset_by_name(
        app->engine->setting, furi_string_get_cstr(app->engine->settings->preset_name));
    if(preset_idx < 0) preset_idx = 0;
    variable_item_set_current_value_index(item, preset_idx);
    variable_item_set_current_value_text(
        item, subghz_setting_get_preset_name(app->engine->setting, preset_idx));

    item = variable_item_list_add(
        app->var_item_list, "Repeat", RF_TOOLS_REPEAT_COUNT, rf_tools_scene_radio_settings_set_repeat, app);
    uint32_t repeat = app->engine->settings->repeat_count;
    if(repeat < 1) repeat = 1;
    if(repeat > RF_TOOLS_REPEAT_COUNT) repeat = RF_TOOLS_REPEAT_COUNT;
    variable_item_set_current_value_index(item, (uint8_t)(repeat - 1));
    FuriString* repeat_text = furi_string_alloc();
    furi_string_printf(repeat_text, "%lux", repeat);
    variable_item_set_current_value_text(item, furi_string_get_cstr(repeat_text));
    furi_string_free(repeat_text);

    item = variable_item_list_add(
        app->var_item_list, "Delay", RF_TOOLS_DELAY_COUNT, rf_tools_scene_radio_settings_set_delay, app);
    uint32_t delay_s = app->engine->settings->repeat_delay_ms / 1000;
    if(delay_s < 1) delay_s = 1;
    if(delay_s > RF_TOOLS_DELAY_COUNT) delay_s = RF_TOOLS_DELAY_COUNT;
    variable_item_set_current_value_index(item, (uint8_t)(delay_s - 1));
    FuriString* delay_text = furi_string_alloc();
    furi_string_printf(delay_text, "%lus", delay_s);
    variable_item_set_current_value_text(item, furi_string_get_cstr(delay_text));
    furi_string_free(delay_text);

    variable_item_list_set_selected_item(
        app->var_item_list, scene_manager_get_scene_state(app->scene_manager, RfToolsSceneRadioSettings));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewVarItemList);
}

bool rf_tools_scene_radio_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_tools_scene_radio_settings_on_exit(void* context) {
    RfToolsApp* app = context;
    scene_manager_set_scene_state(
        app->scene_manager,
        RfToolsSceneRadioSettings,
        variable_item_list_get_selected_item_index(app->var_item_list));
    variable_item_list_reset(app->var_item_list);
    rf_tools_settings_save(app->engine->settings, app->storage);
}

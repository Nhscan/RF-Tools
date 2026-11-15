#include "../rf_tools_i.h"

#define RF_TOOLS_DELAY_COUNT  30 // 1s steps, 1..30s
#define RF_TOOLS_REPEAT_COUNT 10 // 1..10 retransmits

static void rf_tools_scene_repeater_settings_set_repeat(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->repeat_count = (uint32_t)(index + 1);

    FuriString* text = furi_string_alloc();
    furi_string_printf(text, "%lux", app->engine->settings->repeat_count);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void rf_tools_scene_repeater_settings_set_delay(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->repeat_delay_ms = (uint32_t)(index + 1) * 1000;

    FuriString* text = furi_string_alloc();
    furi_string_printf(text, "%lus", app->engine->settings->repeat_delay_ms / 1000);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void rf_tools_scene_repeater_settings_set_save_mode(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->save_mode = (RfToolsSaveMode)index;
    variable_item_set_current_value_text(item, rf_tools_settings_save_mode_name(index));
}

static void rf_tools_scene_repeater_settings_set_tx_prefer(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->tx_prefer = (RfToolsTxRepresentation)index;
    variable_item_set_current_value_text(item, rf_tools_settings_tx_prefer_name(index));
}

static void rf_tools_scene_repeater_settings_set_auto_tx(VariableItem* item) {
    RfToolsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->engine->settings->auto_tx = index != 0;
    variable_item_set_current_value_text(item, index ? "On" : "Off");
}

void rf_tools_scene_repeater_settings_on_enter(void* context) {
    RfToolsApp* app = context;
    VariableItem* item;

    item = variable_item_list_add(
        app->var_item_list, "Repeat", RF_TOOLS_REPEAT_COUNT, rf_tools_scene_repeater_settings_set_repeat, app);
    uint32_t repeat = app->engine->settings->repeat_count;
    if(repeat < 1) repeat = 1;
    if(repeat > RF_TOOLS_REPEAT_COUNT) repeat = RF_TOOLS_REPEAT_COUNT;
    variable_item_set_current_value_index(item, (uint8_t)(repeat - 1));
    FuriString* repeat_text = furi_string_alloc();
    furi_string_printf(repeat_text, "%lux", repeat);
    variable_item_set_current_value_text(item, furi_string_get_cstr(repeat_text));
    furi_string_free(repeat_text);

    item = variable_item_list_add(
        app->var_item_list, "Delay", RF_TOOLS_DELAY_COUNT, rf_tools_scene_repeater_settings_set_delay, app);
    uint32_t delay_s = app->engine->settings->repeat_delay_ms / 1000;
    if(delay_s < 1) delay_s = 1;
    if(delay_s > RF_TOOLS_DELAY_COUNT) delay_s = RF_TOOLS_DELAY_COUNT;
    variable_item_set_current_value_index(item, (uint8_t)(delay_s - 1));
    FuriString* delay_text = furi_string_alloc();
    furi_string_printf(delay_text, "%lus", delay_s);
    variable_item_set_current_value_text(item, furi_string_get_cstr(delay_text));
    furi_string_free(delay_text);

    item = variable_item_list_add(
        app->var_item_list, "Save", 3, rf_tools_scene_repeater_settings_set_save_mode, app);
    variable_item_set_current_value_index(item, app->engine->settings->save_mode);
    variable_item_set_current_value_text(
        item, rf_tools_settings_save_mode_name(app->engine->settings->save_mode));

    item = variable_item_list_add(
        app->var_item_list, "TX As", 2, rf_tools_scene_repeater_settings_set_tx_prefer, app);
    variable_item_set_current_value_index(item, app->engine->settings->tx_prefer);
    variable_item_set_current_value_text(
        item, rf_tools_settings_tx_prefer_name(app->engine->settings->tx_prefer));

    item = variable_item_list_add(
        app->var_item_list, "Auto-TX", 2, rf_tools_scene_repeater_settings_set_auto_tx, app);
    variable_item_set_current_value_index(item, app->engine->settings->auto_tx ? 1 : 0);
    variable_item_set_current_value_text(item, app->engine->settings->auto_tx ? "On" : "Off");

    variable_item_list_set_selected_item(
        app->var_item_list,
        scene_manager_get_scene_state(app->scene_manager, RfToolsSceneRepeaterSettings));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfToolsViewVarItemList);
}

bool rf_tools_scene_repeater_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_tools_scene_repeater_settings_on_exit(void* context) {
    RfToolsApp* app = context;
    scene_manager_set_scene_state(
        app->scene_manager,
        RfToolsSceneRepeaterSettings,
        variable_item_list_get_selected_item_index(app->var_item_list));
    variable_item_list_reset(app->var_item_list);
    rf_tools_settings_save(app->engine->settings, app->storage);
}

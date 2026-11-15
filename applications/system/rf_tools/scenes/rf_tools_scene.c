#include "rf_tools_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const rf_tools_on_enter_handlers[])(void*) = {
#include "rf_tools_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const rf_tools_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "rf_tools_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const rf_tools_on_exit_handlers[])(void* context) = {
#include "rf_tools_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers rf_tools_scene_handlers = {
    .on_enter_handlers = rf_tools_on_enter_handlers,
    .on_event_handlers = rf_tools_on_event_handlers,
    .on_exit_handlers = rf_tools_on_exit_handlers,
    .scene_num = RfToolsSceneNum,
};

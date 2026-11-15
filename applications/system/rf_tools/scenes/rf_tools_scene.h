#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) RfToolsScene##id,
typedef enum {
#include "rf_tools_scene_config.h"
    RfToolsSceneNum,
} RfToolsScene;
#undef ADD_SCENE

extern const SceneManagerHandlers rf_tools_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "rf_tools_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "rf_tools_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "rf_tools_scene_config.h"
#undef ADD_SCENE

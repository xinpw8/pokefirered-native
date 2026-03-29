/*
 * pfr_env_binding.c — PufferLib Python/C binding for PFRN env
 */

#include "pfr_env.h"
#include "pfr_game_api.h"

#define Env PfrEnv
#define Log PfrLog
#include "../pufferlib/ocean/env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    /* Boot game once (loads ROM, runs to overworld, captures hot savestate) */
    static int game_booted = 0;
    if (!game_booted) {
        pfr_game_boot();
        game_booted = 1;
    }

    env->frames_per_step = (uint32_t)unpack(kwargs, "frames_per_step");
    env->max_steps = (uint32_t)unpack(kwargs, "max_steps");

    /* Optional: savestate path */
    PyObject* path_obj = PyDict_GetItemString(kwargs, "savestate_path");
    if (path_obj && PyUnicode_Check(path_obj)) {
        const char* path = PyUnicode_AsUTF8(path_obj);
        if (path) {
            strncpy(env->savestate_path, path, sizeof(env->savestate_path) - 1);
            env->savestate_path[sizeof(env->savestate_path) - 1] = '\0';
        }
    }

    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "badges", log->badges);
    assign_to_dict(dict, "exploration", log->exploration);
    assign_to_dict(dict, "party_level_sum", log->party_level_sum);
    return 0;
}

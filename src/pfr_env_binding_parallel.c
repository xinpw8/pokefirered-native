/*
 * pfr_env_binding_parallel.c — PufferLib Python/C binding for parallel PFRN env
 *
 * Uses pfr_env_parallel.h (no game headers) + pfr_so_instance.h for
 * SO-copy based parallelism. Each env gets its own dlopen'd game instance.
 */

#include "pfr_env_parallel.h"
#include "pfr_so_instance.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* PufferLib binding macros */
#define Env PfrParEnv
#define Log PfrParLog
#include "../pufferlib/ocean/env_binding.h"

/* Global instance pool — allocated once during init */
static PfrInstance *sInstances = NULL;
static int sNumInstances = 0;

static int my_init(Env *env, PyObject *args, PyObject *kwargs) {
    env->frames_per_step = (uint32_t)unpack(kwargs, "frames_per_step");
    env->max_steps = (uint32_t)unpack(kwargs, "max_steps");

    /* Savestate path */
    PyObject *path_obj = PyDict_GetItemString(kwargs, "savestate_path");
    if (path_obj && PyUnicode_Check(path_obj)) {
        const char *path = PyUnicode_AsUTF8(path_obj);
        if (path) {
            strncpy(env->savestate_path, path, sizeof(env->savestate_path) - 1);
            env->savestate_path[sizeof(env->savestate_path) - 1] = '\0';
        }
    }

    /* Assign a game instance to this env.
     * The instance pool must be created before env init.
     * env_id is the sequential index of this env within the vectorized batch. */
    int env_id = (int)unpack(kwargs, "env_id");
    if (env_id < 0 || env_id >= sNumInstances) {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_env_binding: env_id %d out of range [0, %d)\n",
// [DEBUG-STRIPPED]                 env_id, sNumInstances);
        return -1;
    }
    env->instance = &sInstances[env_id];

    return 0;
}

static int my_log(PyObject *dict, Log *log) {
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "badges", log->badges);
    assign_to_dict(dict, "exploration", log->exploration);
    assign_to_dict(dict, "party_level_sum", log->party_level_sum);
    return 0;
}

/*
 * Module-level function to initialize the SO instance pool.
 * Called from Python before creating any envs:
 *   module.init_instances(so_path, tmp_dir, num_envs)
 */
static PyObject *py_init_instances(PyObject *self, PyObject *args) {
    const char *so_path;
    const char *tmp_dir;
    int num_envs;

    if (!PyArg_ParseTuple(args, "ssi", &so_path, &tmp_dir, &num_envs))
        return NULL;

    if (sInstances) {
        pfr_instances_destroy(sInstances, sNumInstances);
        free(sInstances);
        sInstances = NULL;
        sNumInstances = 0;
    }

    sInstances = (PfrInstance *)calloc(num_envs, sizeof(PfrInstance));
    if (!sInstances) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate instance pool");
        return NULL;
    }

    if (pfr_instances_create(so_path, tmp_dir, sInstances, num_envs) != 0) {
        free(sInstances);
        sInstances = NULL;
        PyErr_SetString(PyExc_RuntimeError, "Failed to create SO instances");
        return NULL;
    }

    sNumInstances = num_envs;

    /* Boot each instance */
    for (int i = 0; i < num_envs; i++) {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_env_binding: booting instance %d/%d\n", i + 1, num_envs);
        sInstances[i].boot();
    }

// [DEBUG-STRIPPED]     fprintf(stderr, "pfr_env_binding: %d instances booted successfully\n", num_envs);
    Py_RETURN_NONE;
}

static PyObject *py_destroy_instances(PyObject *self, PyObject *args) {
    (void)args;
    if (sInstances) {
        pfr_instances_destroy(sInstances, sNumInstances);
        free(sInstances);
        sInstances = NULL;
        sNumInstances = 0;
    }
    Py_RETURN_NONE;
}

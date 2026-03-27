/*
 * pfrn_binding.h -- PufferLib 4.0 ocean binding for pokefirered-native
 *
 * Follows the NMMO3 pattern exactly:
 *   1. Include env header
 *   2. Define OBS_SIZE, ACT_SIZES, etc.
 *   3. #define Env/Log, #include env_binding.h
 *   4. Implement my_init() and my_log()
 *
 * This file is included by a .c compilation unit that also links
 * against pfr_so_instance.o for SO-copy game instances.
 */

#ifndef PFRN_BINDING_H
#define PFRN_BINDING_H

#include <dlfcn.h>
#include <string.h>

#include "pfrn_env.h"
#include "pfr_so_instance.h"

#include <Python.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- PufferLib binding macros ---- */

#define OBS_SIZE   226
#define NUM_ATNS   1
#define ACT_SIZES  {8}
#define OBS_TYPE   UNSIGNED_CHAR
#define ACT_TYPE   FLOAT

#define Env  PfrnEnv
#define Log  PfrnLog

/* Forward declare custom methods before including env_binding.h */
static PyObject *py_init_instances(PyObject *self, PyObject *args);
static PyObject *py_destroy_instances(PyObject *self, PyObject *args);
static PyObject *py_capture_frame(PyObject *self, PyObject *args);
static PyObject *py_save_state(PyObject *self, PyObject *args);
static PyObject *py_test_step(PyObject *self, PyObject *args);

#define MY_METHODS \
    {"init_instances", py_init_instances, METH_VARARGS, \
     "Create SO-copy game instances: init_instances(so_path, tmp_dir, num_envs)"}, \
    {"destroy_instances", py_destroy_instances, METH_VARARGS, \
     "Destroy all SO-copy game instances"}, \
    {"capture_frame", py_capture_frame, METH_VARARGS, \
     "Render current frame and return as numpy array (160,240,4) uint8"}, \
    {"save_state", py_save_state, METH_VARARGS, \
     "Save game state to file: save_state(idx, path)"}, \
    {"test_step", py_test_step, METH_VARARGS, "Debug: test step directly"}

#include "env_binding.h"

/* ---- Global instance pool ---- */

static PfrInstance *sInstances = NULL;
static int sNumInstances = 0;
static int sNextEnvId = 0;  /* auto-incrementing env_id for vec_init loop */

/* ---- my_init: called per env by env_binding.h vec_init ---- */

static int my_init(Env *env, PyObject *args, PyObject *kwargs) {
    env->frames_per_step = (uint32_t)unpack(kwargs, "frames_per_step");
    if (PyErr_Occurred()) return -1;

    env->max_steps = (uint32_t)unpack(kwargs, "max_steps");
    if (PyErr_Occurred()) return -1;

    /* Savestate path (string kwarg) */
    PyObject *path_obj = PyDict_GetItemString(kwargs, "savestate_path");
    if (path_obj && PyUnicode_Check(path_obj)) {
        const char *path = PyUnicode_AsUTF8(path_obj);
        if (path) {
            strncpy(env->savestate_path, path, sizeof(env->savestate_path) - 1);
            env->savestate_path[sizeof(env->savestate_path) - 1] = '\0';
        }
    }

    /* Assign game instance from pre-created pool.
     * vec_init calls my_init sequentially for i=0..num_envs-1 with the same
     * kwargs dict (only seed changes). We use a static counter to assign
     * each env its corresponding instance. The counter is reset when
     * init_instances is called. */
    int env_id = sNextEnvId++;

    if (env_id < 0 || env_id >= sNumInstances) {
        fprintf(stderr, "pfrn_binding: env_id %d out of range [0, %d)\n",
                env_id, sNumInstances);
        PyErr_Format(PyExc_ValueError,
                     "env_id %d out of range [0, %d). "
                     "Call init_instances() before vec_init().", env_id, sNumInstances);
        return -1;
    }
    env->instance = &sInstances[env_id];

    /* Initialize env internal state */
    pfrn_init(env);

    return 0;
}

/* ---- my_log: called by env_binding.h vec_log ---- */

static int my_log(PyObject *dict, Log *log) {
    assign_to_dict(dict, "episode_return", log->episode_return);
    assign_to_dict(dict, "episode_length", log->episode_length);
    assign_to_dict(dict, "badges", log->badges);
    assign_to_dict(dict, "exploration", log->exploration);
    assign_to_dict(dict, "party_level_sum", log->party_level_sum);
    return 0;
}

/* ---- Module-level functions for SO instance lifecycle ---- */

static PyObject *py_init_instances(PyObject *self, PyObject *args) {
    const char *so_path;
    const char *tmp_dir;
    int num_envs;

    if (!PyArg_ParseTuple(args, "ssi", &so_path, &tmp_dir, &num_envs))
        return NULL;

    /* Clean up previous pool if any */
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
    sNextEnvId = 0;  /* reset counter for vec_init loop */

    /* Boot each instance */
    for (int i = 0; i < num_envs; i++) {
        fprintf(stderr, "pfrn_binding: booting instance %d/%d\n", i + 1, num_envs);
        sInstances[i].boot();
    }

    fprintf(stderr, "pfrn_binding: %d instances booted successfully\n", num_envs);
    Py_RETURN_NONE;
}

static PyObject *py_destroy_instances(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;
    if (sInstances) {
        pfr_instances_destroy(sInstances, sNumInstances);
        free(sInstances);
        sInstances = NULL;
        sNumInstances = 0;
    }
    Py_RETURN_NONE;
}


static PyObject *py_capture_frame(PyObject *self, PyObject *args) {
    (void)self;
    int idx;
    if (!PyArg_ParseTuple(args, "i", &idx))
        return NULL;

    if (idx < 0 || idx >= sNumInstances) {
        PyErr_Format(PyExc_IndexError,
                     "Instance index %d out of range [0, %d)", idx, sNumInstances);
        return NULL;
    }

    /* Render the current GBA screen state (no game advance) */
    if (sInstances[idx].render_current_frame) {
        sInstances[idx].render_current_frame();
    }

    /* Allocate a numpy array: shape (160, 240, 4) uint8 */
    npy_intp dims[3] = {160, 240, 4};
    PyObject *arr = PyArray_SimpleNew(3, dims, NPY_UINT8);
    if (!arr)
        return NULL;

    uint32_t *buf = (uint32_t *)PyArray_DATA((PyArrayObject *)arr);

    /* Get framebuffer directly from game SO, bypassing g_renderer_initialized guard.
     * We use HostRendererGetFramebuffer (which just returns sFramebuffer pointer)
     * instead of pfr_game_copy_framebuffer (which checks g_renderer_initialized). */
    typedef const uint32_t *(*get_fb_fn)(void);
    get_fb_fn get_fb = (get_fb_fn)dlsym(sInstances[idx].dl_handle, "HostRendererGetFramebuffer");
    if (get_fb) {
        const uint32_t *src = get_fb();
        if (src) {
            memcpy(buf, src, 240 * 160 * sizeof(uint32_t));
        } else {
            memset(buf, 0, 240 * 160 * sizeof(uint32_t));
        }
    } else {
        /* Fallback to copy_framebuffer (may fail if renderer not init'd) */
        sInstances[idx].copy_framebuffer(buf, 240);
    }

    return arr;
}

static PyObject *py_save_state(PyObject *self, PyObject *args) {
    (void)self;
    int idx;
    const char *path;
    if (!PyArg_ParseTuple(args, "is", &idx, &path))
        return NULL;
    if (idx < 0 || idx >= sNumInstances) {
        PyErr_Format(PyExc_IndexError, "Instance %d out of range", idx);
        return NULL;
    }
    void *h = sInstances[idx].dl_handle;
    typedef int (*save_state_fn)(const char *p);
    save_state_fn save_st = (save_state_fn)dlsym(h, "pfr_game_save_state");
    if (!save_st) {
        PyErr_SetString(PyExc_RuntimeError, "pfr_game_save_state not found");
        return NULL;
    }
    int ret = save_st(path);
    return PyLong_FromLong(ret);
}


/* DEBUG: test step_frames directly from Python */
static PyObject *py_test_step(PyObject *self, PyObject *args) {
    (void)self;
    int idx, action, nsteps;
    if (!PyArg_ParseTuple(args, "iii", &idx, &action, &nsteps)) return NULL;
    if (idx < 0 || idx >= sNumInstances) {
        PyErr_Format(PyExc_IndexError, "idx %d out of range", idx);
        return NULL;
    }
    PfrInstance *inst = &sInstances[idx];
    uint16_t buttons = 0;
    if (action >= 0 && action < PFRN_NUM_ACTIONS) {
        buttons = sPfrnActionToButtons[action];
    }
    fprintf(stderr, "[TEST_STEP] idx=%d action=%d buttons=0x%04x nsteps=%d sf=%p sff=%p\n",
            idx, action, buttons, nsteps, (void*)inst->step_frames, (void*)inst->step_frames_fast);
    for (int i = 0; i < nsteps; i++) {
        inst->step_frames(buttons, 4);
    }
    uint8_t obs_buf[226];
    inst->extract_obs(obs_buf);
    int16_t px = (int16_t)(obs_buf[0] | (obs_buf[1] << 8));
    int16_t py = (int16_t)(obs_buf[2] | (obs_buf[3] << 8));
    fprintf(stderr, "[TEST_STEP] result pos=(%d,%d)\n", px, py);
    return Py_BuildValue("(ii)", (int)px, (int)py);
}

#endif /* PFRN_BINDING_H */

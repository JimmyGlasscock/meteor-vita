/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/ff7_boot_log.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <stdio.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0x98000000

extern so_module so_mod;

/** Phase 1: kubridge / shaCCG / DATA_PATH layout (see FF7-Data-Layout.md). */
static void soloader_verify_data_layout(void) {
    char path[384];

    if (!libshacccg_installed()) {
        l_fatal("libshacccg.suprx not found.");
        fatal_error("Error: libshacccg.suprx is not installed. "
                    "Install via ShaRKBR33D (see FF7-Data-Layout.md).");
    }
    l_success("libshacccg check passed.");

    sceClibPrintf("[ff7-vita] DATA_PATH=%s\n", DATA_PATH);
    sceClibPrintf("[ff7-vita] SO_PATH=%s\n", SO_PATH);

    if (!is_dir(DATA_PATH)) {
        l_fatal("DATA_PATH is missing or not a directory.");
        fatal_error("Create the data folder and copy game files:\n%s\n"
                    "(See FF7-Data-Layout.md.)", DATA_PATH);
    }
    l_success("DATA_PATH exists.");

    // Verify the OBB data tree is present at its canonical location.
    snprintf(path, sizeof(path), "%sff7_1.02/data", DATA_PATH);
    if (!is_dir(path)) {
        l_fatal("OBB data directory missing: ff7_1.02/data");
        fatal_error("Extract the FF7 OBB so that:\n%s/\nexists.\n"
                    "(See FF7-Data-Layout.md.)", path);
    }
    l_success("ff7_1.02/data directory found.");

    // Verify APK assets mirror (shaders are loaded at startup via AAssetManager).
    snprintf(path, sizeof(path), "%sassets", DATA_PATH);
    if (!is_dir(path)) {
        l_fatal("APK assets directory missing.");
        fatal_error("Copy the APK assets/ tree to:\n%s/\n"
                    "(Shaders/ required. See FF7-Data-Layout.md.)", path);
    }
    l_success("assets/ directory found.");

    // Documents/ is writable storage for saves and APP.LOG. Create it now
    // so the .so can write APP.LOG before the user ever saves.
    snprintf(path, sizeof(path), "%sDocuments", DATA_PATH);
    sceIoMkdir(path, 0777);

    // glsl/ and gxp/ are shader-cache scratch dirs created on demand.
    snprintf(path, sizeof(path), "%sglsl", DATA_PATH);
    sceIoMkdir(path, 0777);
    snprintf(path, sizeof(path), "%sgxp", DATA_PATH);
    sceIoMkdir(path, 0777);
}

void soloader_init_all() {
    ff7_boot_log("soloader_init_all: start");

	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");
    ff7_boot_log("kubridge ok");

    soloader_verify_data_layout();
    ff7_boot_log("data layout / shaCCG / assets ok");

    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }

    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        l_fatal("SO could not be loaded.");
        fatal_error("Error: could not load %s.", SO_PATH);
    }
    ff7_boot_log("so_file_load ok");

    settings_load();
    l_success("Settings loaded.");

    so_relocate(&so_mod);
    l_success("SO relocated.");

    resolve_imports(&so_mod);
    l_success("SO imports resolved.");
    ff7_boot_log("resolve_imports ok");

    ff7_boot_log("so_patch: start");
    so_patch();
    l_success("SO patched.");
    ff7_boot_log("so_patch ok");

    ff7_boot_log("so_flush_caches: start");
    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");
    ff7_boot_log("so_flush_caches ok");

    ff7_boot_log("so_initialize: start");
    so_initialize(&so_mod);
    l_success("SO initialized.");
    ff7_boot_log("so_initialize ok");

    ff7_boot_log("gl_preload: start");
    gl_preload();
    l_success("OpenGL preloaded.");
    ff7_boot_log("gl_preload ok");

    ff7_boot_log("jni_init: start");
    jni_init();
    l_success("FalsoJNI initialized.");
    ff7_boot_log("jni_init ok");

    controls_init();
    l_success("Controls initialized.");

    ff7_boot_log("soloader_init_all done");
}

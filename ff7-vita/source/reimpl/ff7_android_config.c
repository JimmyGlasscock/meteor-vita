/*
 * ff7_android_config.c — AConfiguration + ALooper stubs for libandroid.so.
 *
 * FF7 Android may call AConfiguration_* to query device locale/orientation,
 * and ALooper_* for its internal event pump.  Since the port drives everything
 * from the Vita's main loop and Java GLSurfaceView, these can all be stubs
 * that return plausible defaults (English, landscape 480p, HDPI).
 */

#include "ff7_android_config.h"
#include "../utils/logger.h"
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * AConfiguration
 *
 * The Android NDK only publishes an opaque AConfiguration*, so we define
 * our own struct and cast liberally.
 * ---------------------------------------------------------------------- */

typedef struct {
    char     language[2];   /* ISO 639-1 language code, e.g. "en" */
    char     country[2];    /* ISO 3166-1 alpha-2, e.g. "US" */
    int32_t  orientation;   /* ACONFIGURATION_ORIENTATION_LAND */
    int32_t  keyboard;      /* ACONFIGURATION_KEYBOARD_NOKEYS */
    int32_t  navigation;    /* ACONFIGURATION_NAVIGATION_NONAV */
    int32_t  screen_size;   /* ACONFIGURATION_SCREENSIZE_NORMAL */
    int32_t  screen_long;   /* ACONFIGURATION_SCREENLONG_NO */
    int32_t  density;       /* 240 = HDPI */
    int32_t  sdk_version;   /* 19 = KitKat */
    int32_t  touchscreen;   /* ACONFIGURATION_TOUCHSCREEN_FINGER */
} vita_aconfiguration_t;

static vita_aconfiguration_t *cfg_from(void *c) { return (vita_aconfiguration_t *)c; }

void *AConfiguration_new(void)
{
    vita_aconfiguration_t *c = (vita_aconfiguration_t *)calloc(1, sizeof(vita_aconfiguration_t));
    if (!c) return NULL;
    c->language[0]  = 'e'; c->language[1]  = 'n';
    c->country[0]   = 'U'; c->country[1]   = 'S';
    c->orientation  = 2;   /* ACONFIGURATION_ORIENTATION_LAND */
    c->keyboard     = 1;   /* ACONFIGURATION_KEYBOARD_NOKEYS */
    c->navigation   = 1;   /* ACONFIGURATION_NAVIGATION_NONAV */
    c->screen_size  = 2;   /* ACONFIGURATION_SCREENSIZE_NORMAL */
    c->screen_long  = 0x20;/* ACONFIGURATION_SCREENLONG_NO */
    c->density      = 240; /* hdpi */
    c->sdk_version  = 19;
    c->touchscreen  = 3;   /* ACONFIGURATION_TOUCHSCREEN_FINGER */
    return c;
}

void AConfiguration_delete(void *config)
{
    free(config);
}

void AConfiguration_fromAssetManager(void *config, void *am)
{
    /* nothing to do — our fake config already has the right defaults */
    (void)config; (void)am;
}

void AConfiguration_copy(void *dst, void *src)
{
    if (dst && src)
        memcpy(dst, src, sizeof(vita_aconfiguration_t));
}

int32_t AConfiguration_diff(void *c0, void *c1)
{
    (void)c0; (void)c1;
    return 0;
}

int32_t AConfiguration_match(void *base, void *requested)
{
    (void)base; (void)requested;
    return 1; /* always a match */
}

int32_t AConfiguration_isBetterThan(void *base, void *c0, void *c1)
{
    (void)base; (void)c0; (void)c1;
    return 0;
}

/* --- language / country -------------------------------------------------- */

void AConfiguration_getLanguage(void *config, char *outLanguage)
{
    if (outLanguage && config) {
        outLanguage[0] = cfg_from(config)->language[0];
        outLanguage[1] = cfg_from(config)->language[1];
    }
}

void AConfiguration_setLanguage(void *config, const char *language)
{
    if (config && language) {
        cfg_from(config)->language[0] = language[0];
        cfg_from(config)->language[1] = language[1];
    }
}

void AConfiguration_getCountry(void *config, char *outCountry)
{
    if (outCountry && config) {
        outCountry[0] = cfg_from(config)->country[0];
        outCountry[1] = cfg_from(config)->country[1];
    }
}

void AConfiguration_setCountry(void *config, const char *country)
{
    if (config && country) {
        cfg_from(config)->country[0] = country[0];
        cfg_from(config)->country[1] = country[1];
    }
}

/* --- orientation --------------------------------------------------------- */

int32_t AConfiguration_getOrientation(void *config)
{
    return config ? cfg_from(config)->orientation : 2;
}

void AConfiguration_setOrientation(void *config, int32_t orientation)
{
    if (config) cfg_from(config)->orientation = orientation;
}

/* --- keyboard ------------------------------------------------------------ */

int32_t AConfiguration_getKeyboard(void *config)
{
    return config ? cfg_from(config)->keyboard : 1;
}

void AConfiguration_setKeyboard(void *config, int32_t keyboard)
{
    if (config) cfg_from(config)->keyboard = keyboard;
}

int32_t AConfiguration_getNavigation(void *config)
{
    return config ? cfg_from(config)->navigation : 1;
}

void AConfiguration_setNavigation(void *config, int32_t navigation)
{
    if (config) cfg_from(config)->navigation = navigation;
}

/* --- screen -------------------------------------------------------------- */

int32_t AConfiguration_getScreenSize(void *config)
{
    return config ? cfg_from(config)->screen_size : 2;
}

void AConfiguration_setScreenSize(void *config, int32_t screenSize)
{
    if (config) cfg_from(config)->screen_size = screenSize;
}

int32_t AConfiguration_getScreenLong(void *config)
{
    return config ? cfg_from(config)->screen_long : 0x20;
}

void AConfiguration_setScreenLong(void *config, int32_t screenLong)
{
    if (config) cfg_from(config)->screen_long = screenLong;
}

int32_t AConfiguration_getDensity(void *config)
{
    return config ? cfg_from(config)->density : 240;
}

void AConfiguration_setDensity(void *config, int32_t density)
{
    if (config) cfg_from(config)->density = density;
}

int32_t AConfiguration_getTouchscreen(void *config)
{
    return config ? cfg_from(config)->touchscreen : 3;
}

void AConfiguration_setTouchscreen(void *config, int32_t touchscreen)
{
    if (config) cfg_from(config)->touchscreen = touchscreen;
}

/* --- SDK version --------------------------------------------------------- */

int32_t AConfiguration_getSdkVersion(void *config)
{
    return config ? cfg_from(config)->sdk_version : 19;
}

void AConfiguration_setSdkVersion(void *config, int32_t sdkVersion)
{
    if (config) cfg_from(config)->sdk_version = sdkVersion;
}

/* -------------------------------------------------------------------------
 * ALooper stubs
 *
 * FF7 uses Java's GLSurfaceView for its render loop; ALooper is unlikely to
 * be needed in practice.  These stubs prevent unresolved-symbol crashes.
 * ---------------------------------------------------------------------- */

void *ALooper_forThread(void)          { return NULL; }
void *ALooper_prepare(int opts)        { (void)opts; return (void *)1; }
void  ALooper_acquire(void *looper)    { (void)looper; }
void  ALooper_release(void *looper)    { (void)looper; }
void  ALooper_wake(void *looper)       { (void)looper; }

int ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData)
{
    (void)timeoutMillis; (void)outFd; (void)outEvents; (void)outData;
    return -1; /* ALOOPER_POLL_TIMEOUT */
}

int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData)
{
    (void)timeoutMillis; (void)outFd; (void)outEvents; (void)outData;
    return -1;
}

int ALooper_addFd(void *looper, int fd, int ident, int events,
                  void *callback, void *data)
{
    (void)looper; (void)fd; (void)ident; (void)events; (void)callback; (void)data;
    return 1;
}

int ALooper_removeFd(void *looper, int fd)
{
    (void)looper; (void)fd;
    return 1;
}

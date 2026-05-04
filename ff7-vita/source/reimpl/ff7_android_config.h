/*
 * ff7_android_config.h — AConfiguration + ALooper stubs for libandroid.so.
 */

#ifndef FF7_ANDROID_CONFIG_H
#define FF7_ANDROID_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* AConfiguration */
void    *AConfiguration_new(void);
void     AConfiguration_delete(void *config);
void     AConfiguration_fromAssetManager(void *config, void *am);
void     AConfiguration_copy(void *dst, void *src);
int32_t  AConfiguration_diff(void *c0, void *c1);
int32_t  AConfiguration_match(void *base, void *requested);
int32_t  AConfiguration_isBetterThan(void *base, void *c0, void *c1);

void     AConfiguration_getLanguage(void *config, char *outLanguage);
void     AConfiguration_setLanguage(void *config, const char *language);
void     AConfiguration_getCountry(void *config, char *outCountry);
void     AConfiguration_setCountry(void *config, const char *country);

int32_t  AConfiguration_getOrientation(void *config);
void     AConfiguration_setOrientation(void *config, int32_t orientation);
int32_t  AConfiguration_getKeyboard(void *config);
void     AConfiguration_setKeyboard(void *config, int32_t keyboard);
int32_t  AConfiguration_getNavigation(void *config);
void     AConfiguration_setNavigation(void *config, int32_t navigation);
int32_t  AConfiguration_getScreenSize(void *config);
void     AConfiguration_setScreenSize(void *config, int32_t screenSize);
int32_t  AConfiguration_getScreenLong(void *config);
void     AConfiguration_setScreenLong(void *config, int32_t screenLong);
int32_t  AConfiguration_getDensity(void *config);
void     AConfiguration_setDensity(void *config, int32_t density);
int32_t  AConfiguration_getTouchscreen(void *config);
void     AConfiguration_setTouchscreen(void *config, int32_t touchscreen);
int32_t  AConfiguration_getSdkVersion(void *config);
void     AConfiguration_setSdkVersion(void *config, int32_t sdkVersion);

/* ALooper */
void *ALooper_forThread(void);
void *ALooper_prepare(int opts);
void  ALooper_acquire(void *looper);
void  ALooper_release(void *looper);
void  ALooper_wake(void *looper);
int   ALooper_pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData);
int   ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData);
int   ALooper_addFd(void *looper, int fd, int ident, int events, void *callback, void *data);
int   ALooper_removeFd(void *looper, int fd);

#ifdef __cplusplus
}
#endif

#endif /* FF7_ANDROID_CONFIG_H */

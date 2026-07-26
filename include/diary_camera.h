/**
 * @file diary_camera.h
 * @brief LAN trigger for the external Insta360 snapshot service.
 */

#ifndef __DIARY_CAMERA_H__
#define __DIARY_CAMERA_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET diary_camera_init(void);
OPERATE_RET diary_camera_trigger(void);

/* Called by the camera worker immediately before the JPEG is queued to the UI. */
void flower_diary_photo_ready(void);
void flower_diary_photo_failed(void);

#ifdef __cplusplus
}
#endif

#endif

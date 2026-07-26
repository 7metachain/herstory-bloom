/**
 * @file diary_camera.c
 * @brief Asynchronous LAN trigger for the friend's Insta360 service.
 */

#include "diary_camera.h"

#include "http_client_interface.h"
#include "ai_ui_manage.h"
#include "tal_api.h"

#define DIARY_CAMERA_HOST       "172.168.1.34"
#define DIARY_CAMERA_PORT       8080
#define DIARY_CAMERA_PATH       "/snap"
#define DIARY_CAMERA_TIMEOUT_MS 20000
#define DIARY_CAMERA_MAX_JPEG  (512 * 1024)

static QUEUE_HANDLE sg_camera_queue;
static THREAD_HANDLE sg_camera_thread;
static bool sg_camera_initialized;

static void __diary_camera_task(void *arg)
{
    uint8_t request = 0;

    (void)arg;
    for (;;) {
        if (tal_queue_fetch(sg_camera_queue, &request, SEM_WAIT_FOREVER) != OPRT_OK) {
            continue;
        }

        http_client_response_t response = {0};
        PR_NOTICE("diary camera: HTTP GET http://%s:%d%s request_body=0 bytes",
                  DIARY_CAMERA_HOST, DIARY_CAMERA_PORT, DIARY_CAMERA_PATH);
        ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"正在请求照片", strlen("正在请求照片"));
        http_client_status_t status = http_client_request(
            &(const http_client_request_t){
                .host = DIARY_CAMERA_HOST,
                .port = DIARY_CAMERA_PORT,
                .path = DIARY_CAMERA_PATH,
                .method = "GET",
                .body = (const uint8_t *)"",
                .body_length = 0,
                .timeout_ms = DIARY_CAMERA_TIMEOUT_MS,
            },
            &response);

        if (status == HTTP_CLIENT_SUCCESS) {
            PR_NOTICE("diary camera: GET complete status=%d headers=%u body=%u buffer=%u",
                      response.status_code,
                      (unsigned)response.headers_length,
                      (unsigned)response.body_length,
                      (unsigned)response.buffer_length);
            PR_NOTICE("diary camera: GET http://%s:%d%s status=%d",
                      DIARY_CAMERA_HOST, DIARY_CAMERA_PORT, DIARY_CAMERA_PATH,
                      response.status_code);
            if (response.body && response.body_length > 0) {
                PR_NOTICE("diary camera: response bytes=%u first=%02X %02X last=%02X %02X",
                          (unsigned)response.body_length,
                          response.body[0], response.body[1],
                          response.body[response.body_length - 2],
                          response.body[response.body_length - 1]);
                if (response.status_code == 200 && response.body_length <= DIARY_CAMERA_MAX_JPEG &&
                    response.body_length >= 2 && response.body[0] == 0xFF && response.body[1] == 0xD8) {
                    PR_NOTICE("diary camera: received JPEG (%u bytes)", (unsigned)response.body_length);
                    ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"照片已收到，正在生成花卡片", strlen("照片已收到，正在生成花卡片"));
                    /* Queue the card first; the following photo message will
                     * then populate the image canvas on the same UI queue. */
                    flower_diary_photo_ready();
                    OPERATE_RET ui_rt = ai_ui_disp_msg(AI_UI_DISP_FLOWER_PHOTO,
                                                       (uint8_t *)response.body,
                                                       (int)response.body_length);
                    if (ui_rt != OPRT_OK) {
                        PR_ERR("diary camera: photo UI queue failed: %d", ui_rt);
                    }
                } else {
                    PR_DEBUG("diary camera response: %.*s", (int)response.body_length,
                             (const char *)response.body);
                    PR_WARN("diary camera: response is not a JPEG (status=%d, len=%u)",
                            response.status_code, (unsigned)response.body_length);
                    ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"照片格式错误，显示文字花卡片", strlen("照片格式错误，显示文字花卡片"));
                    flower_diary_photo_failed();
                }
            }
        } else {
            PR_ERR("diary camera request failed: %d", status);
            ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"照片请求失败，显示文字花卡片", strlen("照片请求失败，显示文字花卡片"));
            flower_diary_photo_failed();
        }

        http_client_free(&response);
    }
}

OPERATE_RET diary_camera_init(void)
{
    OPERATE_RET rt;

    if (sg_camera_initialized) {
        return OPRT_OK;
    }

    rt = tal_queue_create_init(&sg_camera_queue, sizeof(uint8_t), 2);
    if (rt != OPRT_OK) {
        return rt;
    }

    THREAD_CFG_T thread_cfg = {0};
    thread_cfg.thrdname = "diary_camera";
    thread_cfg.priority = THREAD_PRIO_3;
    thread_cfg.stackDepth = 4096;
    thread_cfg.psram_mode = 1;
    rt = tal_thread_create_and_start(&sg_camera_thread, NULL, NULL,
                                     __diary_camera_task, NULL, &thread_cfg);
    if (rt != OPRT_OK) {
        return rt;
    }

    sg_camera_initialized = true;
    PR_NOTICE("diary camera trigger ready: http://%s:%d%s",
              DIARY_CAMERA_HOST, DIARY_CAMERA_PORT, DIARY_CAMERA_PATH);
    return OPRT_OK;
}

OPERATE_RET diary_camera_trigger(void)
{
    uint8_t request = 1;
    OPERATE_RET rt;

    if (!sg_camera_initialized) {
        PR_ERR("diary camera: trigger rejected, worker not initialized");
        return OPRT_RESOURCE_NOT_READY;
    }
    rt = tal_queue_post(sg_camera_queue, &request, 0);
    PR_NOTICE("diary camera: trigger queued rt=%d", rt);
    return rt;
}

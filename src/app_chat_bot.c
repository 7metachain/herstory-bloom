/**
 * @file app_chat_bot.c
 * @brief app_chat_bot module is used to
 * @version 0.1
 * @date 2025-03-25
 */

#include "tal_api.h"

#include "netmgr.h"

#include "ai_chat_main.h"
#include "app_chat_bot.h"
#include "environment_mcp.h"
#include "environment_sensor.h"
#include "irrigation_controller.h"
#include "diary_camera.h"
#include "tuya_ai_agent.h"

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "tkl_wifi.h"
#endif

#if defined(ENABLE_PRINTER) && (ENABLE_PRINTER == 1)
#include "app_printer.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/
#define PRINTF_FREE_HEAP_TTIME (10 * 1000)
#define DISP_NET_STATUS_TIME   (1 * 1000)

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
***********************const declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static TIMER_ID sg_printf_heap_tm;

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
#include "ai_ui_chat_wechat.h"
static AI_UI_WIFI_STATUS_E sg_wifi_status = AI_UI_WIFI_STATUS_DISCONNECTED;
static TIMER_ID            sg_disp_status_tm;

#define FLOWER_USER_TEXT_MAX  256
#define FLOWER_REPLY_MAX      512
static char sg_flower_user_text[FLOWER_USER_TEXT_MAX];
static char sg_flower_reply[FLOWER_REPLY_MAX];
static char sg_flower_mood[32] = "平静";
static bool sg_diary_mode;
static bool sg_diary_entry_active;
static bool sg_diary_photo_pending;

static void __flower_copy(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static void __flower_append(char *dst, size_t dst_len, const char *src)
{
    size_t used;

    if (dst == NULL || src == NULL || dst_len == 0) {
        return;
    }
    used = strlen(dst);
    if (used + 1 < dst_len) {
        snprintf(dst + used, dst_len - used, "%s", src);
    }
}

static void __flower_show_card(void)
{
    AI_UI_FLOWER_CARD_T card = {0};
    OPERATE_RET rt;

    if (sg_flower_user_text[0] == '\0') {
        return;
    }

    snprintf(card.mood, sizeof(card.mood), "今天的心情：%s", sg_flower_mood[0] ? sg_flower_mood : "平静");
    snprintf(card.diary, sizeof(card.diary), "主人告诉我：\n%s\n\n我把这份心情收藏在叶片里，今天也会安静地陪着你。",
             sg_flower_user_text);
    if (sg_flower_reply[0] != '\0') {
        snprintf(card.reply, sizeof(card.reply), "花想对你说：\n%s", sg_flower_reply);
    } else {
        snprintf(card.reply, sizeof(card.reply), "花想对你说：\n谢谢你告诉我，我会一直陪着你。\n");
    }

    /* Use the latest value populated by the irrigation sensor thread. This
     * keeps the ASR/UI callback non-blocking while still showing real data. */
    ENVIRONMENT_READING_T reading = {0};
    if (environment_sensor_get_last(&reading)) {
        snprintf(card.environment, sizeof(card.environment),
                 "温度 %d.%02d°C  湿度 %u.%02d%%  气压 %u Pa",
                 reading.temperature_centi_c / 100,
                 abs(reading.temperature_centi_c % 100),
                 reading.humidity_centi_percent / 100,
                 reading.humidity_centi_percent % 100,
                 reading.pressure_pa);
    } else {
        snprintf(card.environment, sizeof(card.environment), "室内环境数据稍后更新");
    }

    /* Queue the card for the LVGL UI task; do not manipulate LVGL from the AI
     * callback thread. */
    rt = ai_ui_disp_msg(AI_UI_DISP_FLOWER_CARD, (uint8_t *)&card, sizeof(card));
    if (rt != OPRT_OK) {
        PR_ERR("flower card queue failed: %d", rt);
    }
}

static void __flower_capture_entry(const char *content)
{
    if (content == NULL || content[0] == '\0') {
        return;
    }

    sg_diary_mode = false;
    sg_diary_entry_active = false;
    sg_diary_photo_pending = true;
    sg_flower_reply[0] = '\0';
    sg_flower_mood[0] = '\0';
    __flower_copy(sg_flower_mood, sizeof(sg_flower_mood), "平静");
    __flower_copy(sg_flower_user_text, sizeof(sg_flower_user_text), content);

    PR_NOTICE("diary entry received, text=%s", sg_flower_user_text);
    if (diary_camera_trigger() != OPRT_OK) {
        PR_WARN("diary camera trigger queue unavailable");
    } else {
        ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"拍照请求已排队", strlen("拍照请求已排队"));
    }
    ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"正在拍摄并记录日志", strlen("正在拍摄并记录日志"));
}

void flower_diary_photo_ready(void)
{
    if (!sg_diary_photo_pending) {
        return;
    }

    sg_diary_photo_pending = false;
    __flower_show_card();
    PR_NOTICE("flower card queued after photo was received");
}

void flower_diary_photo_failed(void)
{
    if (!sg_diary_photo_pending) {
        return;
    }

    sg_diary_photo_pending = false;
    __flower_show_card();
    ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"照片暂不可用，已显示文字花卡片",
                   strlen("照片暂不可用，已显示文字花卡片"));
    PR_WARN("flower card fallback displayed because photo was unavailable");
}
#endif

/***********************************************************
***********************function define**********************
***********************************************************/
#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
extern void app_ui_action_register(void);
#endif

static void __printf_free_heap_tm_cb(TIMER_ID timer_id, void *arg)
{
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    uint32_t free_heap       = tal_system_get_free_heap_size();
    uint32_t free_psram_heap = tal_psram_get_free_heap_size();
    PR_INFO("Free heap size:%d, Free psram heap size:%d", free_heap, free_psram_heap);
#else
    uint32_t free_heap = tal_system_get_free_heap_size();
    PR_INFO("Free heap size:%d", free_heap);
#endif
}

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
static void __display_net_status_update(void)
{
    AI_UI_WIFI_STATUS_E wifi_status = AI_UI_WIFI_STATUS_DISCONNECTED;
    netmgr_status_e     net_status  = NETMGR_LINK_DOWN;

    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &net_status);
    if (net_status == NETMGR_LINK_UP) {
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
        // get rssi
        int8_t rssi = 0;
#ifndef PLATFORM_T5
        // BUG: Getting RSSI causes a crash on T5 platform
        tkl_wifi_station_get_conn_ap_rssi(&rssi);
#endif
        if (rssi >= -60) {
            wifi_status = AI_UI_WIFI_STATUS_GOOD;
        } else if (rssi >= -70) {
            wifi_status = AI_UI_WIFI_STATUS_FAIR;
        } else {
            wifi_status = AI_UI_WIFI_STATUS_WEAK;
        }
#else
        wifi_status = AI_UI_WIFI_STATUS_GOOD;
#endif
    } else {
        wifi_status = AI_UI_WIFI_STATUS_DISCONNECTED;
    }

    if (wifi_status != sg_wifi_status) {
        sg_wifi_status = wifi_status;
        ai_ui_disp_msg(AI_UI_DISP_NETWORK, (uint8_t *)&wifi_status, sizeof(AI_UI_WIFI_STATUS_E));
    }
}

static void __display_status_tm_cb(TIMER_ID timer_id, void *arg)
{
    __display_net_status_update();
}

#endif

static void __ai_chat_handle_event(AI_NOTIFY_EVENT_T *event)
{
#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    AI_NOTIFY_TEXT_T *text = NULL;
#endif

    if (event == NULL) {
        return;
    }

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    switch (event->type) {
    case AI_USER_EVT_ASR_OK:
        text = (AI_NOTIFY_TEXT_T *)event->data;
        ai_ui_wechat_hide_flower_card();

        if (text != NULL && text->data != NULL && strstr(text->data, "记录日志") != NULL) {
            /* This is a local mode command, not a question for the cloud LLM.
             * Keep the agent session intact (breaking it here can put binary
             * protocol/audio bytes on the debug stream); local state below
             * prevents a flower card until the diary text arrives. */
            (void)tuya_ai_output_stop(TRUE);
            PR_NOTICE("diary command recognized; waiting for diary content");
            const char *marker = strstr(text->data, "记录日志");
            const char *tail = marker + strlen("记录日志");
            while (*tail == ' ' || *tail == ',' || *tail == ':') {
                tail++;
            }
            sg_diary_mode = true;
            sg_diary_entry_active = false;
            sg_flower_user_text[0] = '\0';
            sg_flower_reply[0] = '\0';
            if (*tail != '\0') {
                __flower_capture_entry(tail);
            } else {
                ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, (uint8_t *)"日志模式：请说今天的日志", strlen("日志模式：请说今天的日志"));
                PR_NOTICE("diary mode entered; say the diary content now");
            }
            break;
        }

        if (!sg_diary_mode) {
            break;
        }

        if (text != NULL) {
            __flower_capture_entry(text->data);
        }
        break;
    case AI_USER_EVT_TEXT_STREAM_START:
    case AI_USER_EVT_TEXT_STREAM_DATA:
    case AI_USER_EVT_TEXT_STREAM_STOP:
        text = (AI_NOTIFY_TEXT_T *)event->data;
        if (sg_diary_entry_active && text != NULL && text->data != NULL && text->datalen > 0) {
            __flower_append(sg_flower_reply, sizeof(sg_flower_reply), text->data);
        }
        if (event->type == AI_USER_EVT_TEXT_STREAM_STOP && sg_diary_entry_active) {
            PR_NOTICE("flower card: NLG complete, reply_len=%d", (int)strlen(sg_flower_reply));
            __flower_show_card();
            sg_diary_entry_active = false;
        }
        break;
    case AI_USER_EVT_LLM_EMOTION:
    case AI_USER_EVT_EMOTION: {
        AI_NOTIFY_EMO_T *emo = (AI_NOTIFY_EMO_T *)event->data;
        if (emo != NULL && emo->name != NULL) {
            __flower_copy(sg_flower_mood, sizeof(sg_flower_mood), emo->name);
        }
        break;
    }
    default:
        break;
    }
#endif

    switch(event->type) {
        #if defined(ENABLE_PRINTER) && (ENABLE_PRINTER == 1)
        case AI_USER_EVT_GENERATE_PICTURE:
        case AI_USER_EVT_GET_PICTURE_FROM_APP: {
            #if defined(ENABLE_COMP_AI_PICTURE) && (ENABLE_COMP_AI_PICTURE == 1)
            app_print_img_from_album((const char *)event->data);
            #endif
        } break;
        #endif
        default:
        break;
    }

}

OPERATE_RET app_chat_bot_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    AI_CHAT_MODE_CFG_T ai_chat_cfg = {
        .default_mode = AI_CHAT_MODE_HOLD,
        .default_vol  = 70,
        .evt_cb       = __ai_chat_handle_event,
    };
    TUYA_CALL_ERR_RETURN(ai_chat_init(&ai_chat_cfg));

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    app_ui_action_register();
#endif

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
    TUYA_CALL_ERR_LOG(ai_video_init());
#endif

#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
    TUYA_CALL_ERR_RETURN(ai_mcp_init());
#if !defined(CONFIG_HERSTORY_DEBUG_DISABLE_I2C)
    TUYA_CALL_ERR_RETURN(environment_mcp_init());
#else
    PR_NOTICE("herstory debug mode: environment I2C/MCP disabled");
#endif
#endif

#if !defined(CONFIG_HERSTORY_DEBUG_DISABLE_I2C)
    TUYA_CALL_ERR_RETURN(irrigation_controller_init());
#else
    PR_NOTICE("herstory debug mode: irrigation sensor thread disabled");
#endif
    TUYA_CALL_ERR_RETURN(diary_camera_init());

#if defined(ENABLE_COMP_AI_PICTURE) && (ENABLE_COMP_AI_PICTURE == 1)
    TUYA_CALL_ERR_RETURN(ai_picture_init());
#endif

    // Free heap size
    tal_sw_timer_create(__printf_free_heap_tm_cb, NULL, &sg_printf_heap_tm);
    tal_sw_timer_start(sg_printf_heap_tm, PRINTF_FREE_HEAP_TTIME, TAL_TIMER_CYCLE);

#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_disp_msg(AI_UI_DISP_NETWORK, (uint8_t *)&sg_wifi_status, sizeof(AI_UI_WIFI_STATUS_E));

    ai_ui_disp_msg(AI_UI_DISP_STATUS, (uint8_t *)INITIALIZING, strlen(INITIALIZING));
    ai_ui_disp_msg(AI_UI_DISP_EMOTION, (uint8_t *)EMOJI_NEUTRAL, strlen(EMOJI_NEUTRAL));

    // display status update
    tal_sw_timer_create(__display_status_tm_cb, NULL, &sg_disp_status_tm);
    tal_sw_timer_start(sg_disp_status_tm, DISP_NET_STATUS_TIME, TAL_TIMER_CYCLE);
#endif

    return OPRT_OK;
}

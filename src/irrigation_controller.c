/**
 * @file irrigation_controller.c
 * @brief Automatic pump controller based on AHT20 air readings.
 *
 * The GPIO drives only the signal input of an external MOSFET/relay board.
 * The pump must use its own suitable power supply.
 */

#include "irrigation_controller.h"

#include "environment_sensor.h"
#include "tal_api.h"
#include "tkl_gpio.h"

#define PUMP_CONTROL_PIN        TUYA_GPIO_NUM_7 /* P11 pin 7: P07 */
#define SENSOR_STARTUP_DELAY_MS (15U * 1000U)
#define SENSOR_CHECK_PERIOD_MS  (60U * 1000U)

#ifndef IRRIGATION_TEMP_THRESHOLD_CENTI_C
#define IRRIGATION_TEMP_THRESHOLD_CENTI_C 2000
#endif

#ifndef IRRIGATION_HUMIDITY_THRESHOLD_CENTI_PERCENT
#define IRRIGATION_HUMIDITY_THRESHOLD_CENTI_PERCENT 4500
#endif

#ifndef IRRIGATION_PUMP_DURATION_MS
#define IRRIGATION_PUMP_DURATION_MS 5000
#endif

#ifndef IRRIGATION_COOLDOWN_MS
#define IRRIGATION_COOLDOWN_MS (10U * 60U * 1000U)
#endif

static THREAD_HANDLE sg_irrigation_thread = NULL;
static SYS_TIME_T sg_last_watering_ms = 0;
static volatile bool sg_manual_test_active = false;

static void __pump_set(bool enabled)
{
    tkl_gpio_write(PUMP_CONTROL_PIN,
                   enabled ? TUYA_GPIO_LEVEL_HIGH : TUYA_GPIO_LEVEL_LOW);
}

static void __irrigation_task(void *arg)
{
    ENVIRONMENT_READING_T reading = {0};
    SYS_TIME_T now_ms = 0;
    bool below_temperature = false;
    bool below_humidity = false;

    (void)arg;
    tal_system_sleep(SENSOR_STARTUP_DELAY_MS);

    while (1) {
        if (environment_sensor_read(&reading) == OPRT_OK) {
            below_temperature =
                reading.temperature_centi_c < IRRIGATION_TEMP_THRESHOLD_CENTI_C;
            below_humidity =
                reading.humidity_centi_percent <
                IRRIGATION_HUMIDITY_THRESHOLD_CENTI_PERCENT;
            now_ms = tal_system_get_millisecond();

            if (!sg_manual_test_active && below_temperature && below_humidity &&
                (sg_last_watering_ms == 0 ||
                 now_ms - sg_last_watering_ms >= IRRIGATION_COOLDOWN_MS)) {
                PR_NOTICE("irrigation start: temperature=%d.%02dC humidity=%u.%02u%%",
                          reading.temperature_centi_c / 100,
                          abs(reading.temperature_centi_c % 100),
                          reading.humidity_centi_percent / 100,
                          reading.humidity_centi_percent % 100);
                __pump_set(true);
                tal_system_sleep(IRRIGATION_PUMP_DURATION_MS);
                __pump_set(false);
                sg_last_watering_ms = tal_system_get_millisecond();
                PR_NOTICE("irrigation stopped after %ums",
                          IRRIGATION_PUMP_DURATION_MS);
            }
        } else {
            /* Fail safe: an unreadable sensor can never turn the pump on. */
            __pump_set(false);
        }

        tal_system_sleep(SENSOR_CHECK_PERIOD_MS);
    }
}

OPERATE_RET irrigation_controller_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_GPIO_BASE_CFG_T pump_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW,
    };
    THREAD_CFG_T thread_cfg = {
        .stackDepth = 3072,
        .priority = THREAD_PRIO_2,
        .thrdname = "irrigation",
    };

    TUYA_CALL_ERR_RETURN(tkl_gpio_init(PUMP_CONTROL_PIN, &pump_cfg));
    __pump_set(false);
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(
        &sg_irrigation_thread, NULL, NULL, __irrigation_task, NULL, &thread_cfg));

    PR_NOTICE("irrigation ready: P07, temp<%d.%02dC, humidity<%d.%02d%%, "
              "duration=%ums, cooldown=%ums",
              IRRIGATION_TEMP_THRESHOLD_CENTI_C / 100,
              IRRIGATION_TEMP_THRESHOLD_CENTI_C % 100,
              IRRIGATION_HUMIDITY_THRESHOLD_CENTI_PERCENT / 100,
              IRRIGATION_HUMIDITY_THRESHOLD_CENTI_PERCENT % 100,
              IRRIGATION_PUMP_DURATION_MS, IRRIGATION_COOLDOWN_MS);
    return OPRT_OK;
}

OPERATE_RET irrigation_pump_voice_test(void)
{
    if (sg_manual_test_active) {
        return OPRT_RESOURCE_NOT_READY;
    }

    sg_manual_test_active = true;
    PR_NOTICE("voice pump test start");
    __pump_set(true);
    tal_system_sleep(IRRIGATION_PUMP_DURATION_MS);
    __pump_set(false);
    sg_manual_test_active = false;
    PR_NOTICE("voice pump test stopped after %ums", IRRIGATION_PUMP_DURATION_MS);
    return OPRT_OK;
}

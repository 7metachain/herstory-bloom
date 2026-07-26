/**
 * @file environment_sensor.h
 * @brief AHT20 temperature/humidity and BMP280 pressure sensor.
 */

#ifndef __ENVIRONMENT_SENSOR_H__
#define __ENVIRONMENT_SENSOR_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t temperature_centi_c;
    uint32_t humidity_centi_percent;
    uint32_t pressure_pa;
    uint8_t bmp280_address;
} ENVIRONMENT_READING_T;

OPERATE_RET environment_sensor_init(void);
OPERATE_RET environment_sensor_read(ENVIRONMENT_READING_T *reading);

/* Return the most recent successful reading without touching the I2C bus. */
bool environment_sensor_get_last(ENVIRONMENT_READING_T *reading);

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file environment_sensor.c
 * @brief Minimal AHT20 + BMP280 driver for the T5AI I2C0 bus.
 *
 * Wiring:
 *   module VCC -> T5AI 3V3
 *   module GND -> T5AI GND
 *   module SCL -> T5AI P00 / GPIO0 / I2C1 SCL (P11 header)
 *   module SDA -> T5AI P01 / GPIO1 / I2C1 SDA (P11 header)
 */

#include "environment_sensor.h"

#include "tal_api.h"
#include "tkl_i2c.h"
#include "tkl_pinmux.h"

#define ENV_I2C_PORT       TUYA_I2C_NUM_1
#define ENV_I2C_SCL_PIN    TUYA_GPIO_NUM_0
#define ENV_I2C_SDA_PIN    TUYA_GPIO_NUM_1
#define AHT20_ADDRESS      0x38
#define BMP280_ADDRESS_0   0x76
#define BMP280_ADDRESS_1   0x77
#define BMP280_CHIP_ID_REG 0xD0
#define BMP280_CHIP_ID     0x58

typedef struct {
    uint16_t t1;
    int16_t t2;
    int16_t t3;
    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
    int32_t t_fine;
} BMP280_CALIBRATION_T;

static bool sg_initialized = false;
static uint8_t sg_bmp280_address = 0;
static BMP280_CALIBRATION_T sg_bmp_cal = {0};
static ENVIRONMENT_READING_T sg_last_reading = {0};
static bool sg_has_last_reading = false;

static uint16_t __u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t __s16_le(const uint8_t *data)
{
    return (int16_t)__u16_le(data);
}

static OPERATE_RET __read_registers(uint8_t address, uint8_t reg, uint8_t *data,
                                    uint32_t length)
{
    OPERATE_RET rt = tkl_i2c_master_send(ENV_I2C_PORT, address, &reg, 1, FALSE);
    if (rt != OPRT_OK) {
        return rt;
    }
    return tkl_i2c_master_receive(ENV_I2C_PORT, address, data, length, FALSE);
}

static OPERATE_RET __write_register(uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return tkl_i2c_master_send(ENV_I2C_PORT, address, data, sizeof(data), FALSE);
}

static OPERATE_RET __aht20_init(void)
{
    uint8_t status_command = 0x71;
    uint8_t status = 0;
    uint8_t init_command[3] = {0xBE, 0x08, 0x00};
    OPERATE_RET rt = OPRT_OK;

    rt = tkl_i2c_master_send(ENV_I2C_PORT, AHT20_ADDRESS, &status_command, 1, FALSE);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = tkl_i2c_master_receive(ENV_I2C_PORT, AHT20_ADDRESS, &status, 1, FALSE);
    if (rt != OPRT_OK) {
        return rt;
    }

    if ((status & 0x08) == 0) {
        TUYA_CALL_ERR_RETURN(tkl_i2c_master_send(ENV_I2C_PORT, AHT20_ADDRESS,
                                                init_command, sizeof(init_command), FALSE));
        tal_system_sleep(10);
    }
    return OPRT_OK;
}

static OPERATE_RET __bmp280_init(void)
{
    uint8_t addresses[] = {BMP280_ADDRESS_0, BMP280_ADDRESS_1};
    uint8_t chip_id = 0;
    uint8_t calibration[24] = {0};
    OPERATE_RET rt = OPRT_OK;
    uint32_t i = 0;

    for (i = 0; i < sizeof(addresses); i++) {
        rt = __read_registers(addresses[i], BMP280_CHIP_ID_REG, &chip_id, 1);
        if (rt == OPRT_OK && chip_id == BMP280_CHIP_ID) {
            sg_bmp280_address = addresses[i];
            break;
        }
    }
    if (sg_bmp280_address == 0) {
        PR_ERR("BMP280 not found at 0x76 or 0x77 (last chip id=0x%02X)", chip_id);
        return OPRT_NOT_FOUND;
    }

    TUYA_CALL_ERR_RETURN(__read_registers(sg_bmp280_address, 0x88,
                                         calibration, sizeof(calibration)));
    sg_bmp_cal.t1 = __u16_le(&calibration[0]);
    sg_bmp_cal.t2 = __s16_le(&calibration[2]);
    sg_bmp_cal.t3 = __s16_le(&calibration[4]);
    sg_bmp_cal.p1 = __u16_le(&calibration[6]);
    sg_bmp_cal.p2 = __s16_le(&calibration[8]);
    sg_bmp_cal.p3 = __s16_le(&calibration[10]);
    sg_bmp_cal.p4 = __s16_le(&calibration[12]);
    sg_bmp_cal.p5 = __s16_le(&calibration[14]);
    sg_bmp_cal.p6 = __s16_le(&calibration[16]);
    sg_bmp_cal.p7 = __s16_le(&calibration[18]);
    sg_bmp_cal.p8 = __s16_le(&calibration[20]);
    sg_bmp_cal.p9 = __s16_le(&calibration[22]);

    if (sg_bmp_cal.p1 == 0) {
        return OPRT_COM_ERROR;
    }

    /* Standby 500 ms, filter x4; temperature x1, pressure x4, normal mode. */
    TUYA_CALL_ERR_RETURN(__write_register(sg_bmp280_address, 0xF5, 0x90));
    TUYA_CALL_ERR_RETURN(__write_register(sg_bmp280_address, 0xF4, 0x2F));
    tal_system_sleep(50);
    return OPRT_OK;
}

static OPERATE_RET __aht20_read(int32_t *temperature_centi_c,
                                uint32_t *humidity_centi_percent)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t command[3] = {0xAC, 0x33, 0x00};
    uint8_t data[7] = {0};
    uint32_t raw_humidity = 0;
    uint32_t raw_temperature = 0;
    uint32_t retries = 0;

    TUYA_CALL_ERR_RETURN(tkl_i2c_master_send(ENV_I2C_PORT, AHT20_ADDRESS,
                                            command, sizeof(command), FALSE));
    tal_system_sleep(80);

    do {
        TUYA_CALL_ERR_RETURN(tkl_i2c_master_receive(ENV_I2C_PORT, AHT20_ADDRESS,
                                                   data, sizeof(data), FALSE));
        if ((data[0] & 0x80) == 0) {
            break;
        }
        tal_system_sleep(10);
    } while (++retries < 5);

    if ((data[0] & 0x80) != 0) {
        PR_ERR("AHT20 measurement timeout");
        return OPRT_COM_ERROR;
    }

    raw_humidity = ((uint32_t)data[1] << 12) |
                   ((uint32_t)data[2] << 4) | (data[3] >> 4);
    raw_temperature = ((uint32_t)(data[3] & 0x0F) << 16) |
                      ((uint32_t)data[4] << 8) | data[5];

    *humidity_centi_percent =
        (uint32_t)(((uint64_t)raw_humidity * 10000U) >> 20);
    *temperature_centi_c =
        (int32_t)(((int64_t)raw_temperature * 20000) >> 20) - 5000;

    if (*humidity_centi_percent > 10000U) {
        *humidity_centi_percent = 10000U;
    }
    return OPRT_OK;
}

static int32_t __bmp280_compensate_temperature(int32_t adc_temperature)
{
    int32_t var1 = ((((adc_temperature >> 3) - ((int32_t)sg_bmp_cal.t1 << 1))) *
                    (int32_t)sg_bmp_cal.t2) >> 11;
    int32_t var2 = (((((adc_temperature >> 4) - (int32_t)sg_bmp_cal.t1) *
                      ((adc_temperature >> 4) - (int32_t)sg_bmp_cal.t1)) >> 12) *
                    (int32_t)sg_bmp_cal.t3) >> 14;
    sg_bmp_cal.t_fine = var1 + var2;
    return (sg_bmp_cal.t_fine * 5 + 128) >> 8;
}

static uint32_t __bmp280_compensate_pressure(int32_t adc_pressure)
{
    int64_t var1 = (int64_t)sg_bmp_cal.t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)sg_bmp_cal.p6;
    int64_t pressure = 0;

    var2 += (var1 * (int64_t)sg_bmp_cal.p5) << 17;
    var2 += ((int64_t)sg_bmp_cal.p4) << 35;
    var1 = ((var1 * var1 * (int64_t)sg_bmp_cal.p3) >> 8) +
           ((var1 * (int64_t)sg_bmp_cal.p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)sg_bmp_cal.p1) >> 33;
    if (var1 == 0) {
        return 0;
    }

    pressure = 1048576 - adc_pressure;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)sg_bmp_cal.p9 * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = ((int64_t)sg_bmp_cal.p8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) +
               (((int64_t)sg_bmp_cal.p7) << 4);
    return (uint32_t)(pressure >> 8);
}

static OPERATE_RET __bmp280_read(uint32_t *pressure_pa)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t data[6] = {0};
    int32_t adc_pressure = 0;
    int32_t adc_temperature = 0;

    TUYA_CALL_ERR_RETURN(__read_registers(sg_bmp280_address, 0xF7,
                                         data, sizeof(data)));
    adc_pressure = ((int32_t)data[0] << 12) |
                   ((int32_t)data[1] << 4) | (data[2] >> 4);
    adc_temperature = ((int32_t)data[3] << 12) |
                      ((int32_t)data[4] << 4) | (data[5] >> 4);
    if (adc_pressure == 0x80000 || adc_temperature == 0x80000) {
        return OPRT_COM_ERROR;
    }

    (void)__bmp280_compensate_temperature(adc_temperature);
    *pressure_pa = __bmp280_compensate_pressure(adc_pressure);
    return (*pressure_pa == 0) ? OPRT_COM_ERROR : OPRT_OK;
}

OPERATE_RET environment_sensor_init(void)
{
    TUYA_IIC_BASE_CFG_T cfg = {
        .role = TUYA_IIC_MODE_MASTER,
        .speed = TUYA_IIC_BUS_SPEED_100K,
        .addr_width = TUYA_IIC_ADDRESS_7BIT,
    };
    OPERATE_RET rt = OPRT_OK;

    if (sg_initialized) {
        return OPRT_OK;
    }

    tkl_io_pinmux_config(ENV_I2C_SCL_PIN, TUYA_IIC1_SCL);
    tkl_io_pinmux_config(ENV_I2C_SDA_PIN, TUYA_IIC1_SDA);
    rt = tkl_i2c_init(ENV_I2C_PORT, &cfg);
    if (rt != OPRT_OK) {
        PR_ERR("environment I2C init failed: %d", rt);
        return rt;
    }

    rt = __aht20_init();
    if (rt != OPRT_OK) {
        PR_ERR("AHT20 not found at 0x38: %d", rt);
        return rt;
    }
    rt = __bmp280_init();
    if (rt != OPRT_OK) {
        return rt;
    }

    sg_initialized = true;
    PR_NOTICE("environment sensors ready: AHT20=0x38, BMP280=0x%02X, "
              "I2C1 SCL=P00/GPIO0 SDA=P01/GPIO1", sg_bmp280_address);
    return OPRT_OK;
}

OPERATE_RET environment_sensor_read(ENVIRONMENT_READING_T *reading)
{
    OPERATE_RET rt = OPRT_OK;

    if (reading == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!sg_initialized) {
        TUYA_CALL_ERR_RETURN(environment_sensor_init());
    }

    rt = __aht20_read(&reading->temperature_centi_c,
                      &reading->humidity_centi_percent);
    if (rt != OPRT_OK) {
        PR_ERR("AHT20 read failed: %d", rt);
        return rt;
    }
    rt = __bmp280_read(&reading->pressure_pa);
    if (rt != OPRT_OK) {
        PR_ERR("BMP280 read failed: %d", rt);
        return rt;
    }
    reading->bmp280_address = sg_bmp280_address;

    PR_NOTICE("environment: temperature=%d.%02dC, humidity=%u.%02u%%, "
              "pressure=%uPa",
              reading->temperature_centi_c / 100,
              abs(reading->temperature_centi_c % 100),
              reading->humidity_centi_percent / 100,
              reading->humidity_centi_percent % 100,
              reading->pressure_pa);
    sg_last_reading = *reading;
    sg_has_last_reading = true;
    return OPRT_OK;
}

bool environment_sensor_get_last(ENVIRONMENT_READING_T *reading)
{
    if (reading == NULL || !sg_has_last_reading) {
        return false;
    }
    *reading = sg_last_reading;
    return true;
}

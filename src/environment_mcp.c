/**
 * @file environment_mcp.c
 * @brief Conversational MCP tool for AHT20 + BMP280 readings.
 */

#include "environment_mcp.h"

#include "ai_mcp_server.h"
#include "cJSON.h"
#include "environment_sensor.h"
#include "irrigation_controller.h"
#include "tal_api.h"

static OPERATE_RET __read_environment(const MCP_PROPERTY_LIST_T *properties,
                                      MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    ENVIRONMENT_READING_T reading = {0};
    cJSON *json = NULL;
    OPERATE_RET rt = OPRT_OK;

    (void)properties;
    (void)user_data;

    rt = environment_sensor_read(&reading);
    if (rt != OPRT_OK) {
        /*
         * Return a structured tool result instead of failing the MCP call. This
         * prevents the LLM from falling back to an outdoor weather/location
         * query when the local sensor is temporarily unavailable.
         */
        json = cJSON_CreateObject();
        if (json == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        cJSON_AddBoolToObject(json, "sensor_available", false);
        cJSON_AddNumberToObject(json, "error_code", rt);
        cJSON_AddStringToObject(
            json, "error",
            "The local AHT20/BMP280 sensor could not be read over I2C1.");
        cJSON_AddStringToObject(
            json, "response_instruction",
            "Answer in Chinese that the local indoor sensor is currently not readable. "
            "Do not ask for a city or location and do not query outdoor weather.");
        ai_mcp_return_value_set_json(ret_val, json);
        return OPRT_OK;
    }

    json = cJSON_CreateObject();
    if (json == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    cJSON_AddBoolToObject(json, "sensor_available", true);
    cJSON_AddNumberToObject(json, "temperature_c",
                            reading.temperature_centi_c / 100.0);
    cJSON_AddNumberToObject(json, "relative_humidity_percent",
                            reading.humidity_centi_percent / 100.0);
    cJSON_AddNumberToObject(json, "pressure_hpa", reading.pressure_pa / 100.0);
    cJSON_AddStringToObject(
        json, "response_instruction",
        "Answer in concise Chinese for speech synthesis. Report only the fields the "
        "user asked for. If the user asks generally about the room environment, report "
        "temperature, relative humidity, and air pressure. Use 摄氏度, 百分比, and "
        "百帕 as units. These are current indoor sensor readings, not weather data.");

    ai_mcp_return_value_set_json(ret_val, json);
    return OPRT_OK;
}

static OPERATE_RET __pump_water(const MCP_PROPERTY_LIST_T *properties,
                                MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    cJSON *json = NULL;
    OPERATE_RET rt = OPRT_OK;

    (void)properties;
    (void)user_data;

    rt = irrigation_pump_voice_test();
    json = cJSON_CreateObject();
    if (json == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    cJSON_AddBoolToObject(json, "success", rt == OPRT_OK);
    cJSON_AddNumberToObject(json, "duration_seconds",
                            IRRIGATION_PUMP_DURATION_MS / 1000);
    cJSON_AddStringToObject(
        json, "response_instruction",
        rt == OPRT_OK
            ? "Answer briefly in Chinese: 已出水，水泵已自动关闭。"
            : "Answer briefly in Chinese: 水泵正在执行其他任务，请稍后再试。");
    ai_mcp_return_value_set_json(ret_val, json);
    return OPRT_OK;
}

static OPERATE_RET __register_environment_tool(void)
{
    OPERATE_RET rt = AI_MCP_TOOL_ADD(
        "read_environment_sensor",
        "Read the current physical AHT20 and BMP280 sensors connected to this device. "
        "This is a LOCAL INDOOR hardware reading and NEVER requires city, GPS, weather, "
        "or any location information. "
        "MUST call this tool whenever the user asks about current temperature, room "
        "temperature, humidity, air pressure, indoor environment, whether it is hot, "
        "cold, humid, or dry, including Chinese questions such as '现在温度多少', "
        "'湿度是多少', '气压是多少', '室内环境怎么样', '现在热不热', or any "
        "semantically equivalent request. Never guess and never reuse an old reading. "
        "The result provides temperature_c, relative_humidity_percent, and pressure_hpa.",
        __read_environment, NULL);
    if (rt != OPRT_OK) {
        PR_ERR("environment MCP tool register failed: %d", rt);
        return rt;
    }

    rt = AI_MCP_TOOL_ADD(
        "test_water_pump",
        "Turn on the local flowerpot water pump for a short, safe test and then "
        "automatically turn it off. MUST call this tool whenever the user says exactly "
        "'出水', '请出水', '测试水泵', '浇一点水', or a semantically equivalent direct "
        "command to run the pump. This action does not require weather, location, or "
        "sensor data. Do not merely describe what to do; call the tool.",
        __pump_water, NULL);
    if (rt != OPRT_OK) {
        PR_ERR("pump MCP tool register failed: %d", rt);
        return rt;
    }

    PR_NOTICE("environment MCP tool registered");
    PR_NOTICE("voice pump MCP tool registered");
    return OPRT_OK;
}

static OPERATE_RET __on_mqtt_connected(void *data)
{
    (void)data;
    return __register_environment_tool();
}

OPERATE_RET environment_mcp_init(void)
{
    /*
     * Always register the MCP tool. Sensor initialization is retried when the
     * tool is called, so a loose wire during boot does not permanently remove
     * the environment capability from the conversation.
     */
    return tal_event_subscribe(EVENT_MQTT_CONNECTED, "environment_mcp",
                               __on_mqtt_connected, SUBSCRIBE_TYPE_ONETIME);
}

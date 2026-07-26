/**
 * @file environment_mcp.h
 * @brief Register the local AHT20/BMP280 MCP tool.
 */

#ifndef __ENVIRONMENT_MCP_H__
#define __ENVIRONMENT_MCP_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET environment_mcp_init(void);

#ifdef __cplusplus
}
#endif

#endif

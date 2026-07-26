#ifndef __IRRIGATION_CONTROLLER_H__
#define __IRRIGATION_CONTROLLER_H__

#include "tuya_cloud_types.h"

#ifndef IRRIGATION_PUMP_DURATION_MS
#define IRRIGATION_PUMP_DURATION_MS 5000
#endif

OPERATE_RET irrigation_controller_init(void);
OPERATE_RET irrigation_pump_voice_test(void);

#endif

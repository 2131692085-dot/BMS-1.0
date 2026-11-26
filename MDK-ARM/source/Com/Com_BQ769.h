#ifndef __COM_BQ769__
#define __COM_BQ769__

#include "main.h"
#include "stdbool.h"

/**
 * @brief 电阻转换为温度
 * 
 * @param uiADCV 
 * @return int8_t 
 */
int8_t Com_BQ769_GetTemperatureByRes(int res );

/**
 * @brief 单一节电池电压转换为SOC
 * 
 */
uint8_t Com_BQ769_GetSocByVol(uint16_t vol);

#endif // __COM_BQ769__

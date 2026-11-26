#ifndef __INT_BMS_H__
#define __INT_BMS_H__

#include "stm32f1xx_hal.h"

void Int_BMS_Init(void);

void Int_BMS_PrintInfo(void);

void Int_BMS_Monitor(void);

void Int_BMS_Balance(void);

typedef struct
{
    float cell_vol[5];
    float total_vol;
    float current;
    int8_t temperature;
    uint8_t power_level_soc;
} BMS_Info;

#endif /* __INT_BMS_H__ */

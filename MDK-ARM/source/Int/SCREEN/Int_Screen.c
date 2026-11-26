#include "Int_Screen.h"
#include "Int_OLED.h"
#include <stdio.h>
#include "Int_BMS.h"

extern BMS_Info bms_info;

void Int_Screen_Init(void)
{
   Inf_OLED_Init();
   Inf_OLED_Clear();
}

void Int_Screen_Render(void)
{
   for (size_t i = 0; i < 5; i++)
   {
      Inf_OLED_ShowChinese(8+i*16, 8, i,16,1);
   }

   //渲染总电压
   uint8_t total_voltage[10]={0};
   sprintf((char*)total_voltage, "V: %3.1fV", bms_info.total_vol);
   Inf_OLED_ShowString(8, 24, total_voltage, 16, 1);
   Inf_OLED_Refresh();
}

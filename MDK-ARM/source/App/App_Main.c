#include "App_Main.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "Int_BMS.h"
#include "Int_OLED.h"
#include "Int_Screen.h"


void bms_task(void *pvParameters);
void oled_task(void *pvParameters);

typedef enum {
    TASK_PRIORITY_0,
    TASK_PRIORITY_1,
    TASK_PRIORITY_2,
    TASK_PRIORITY_3,
    TASK_PRIORITY_4,
} TASK_PRIORITY;

/**
 * 入口函数
 */
void App_main(void) {
    printf("BMS Project Start \n");

    // 在调度器启动前完成 OLED 初始化，此时 I2C 独占，不会被 bms_task 抢占
    Int_Screen_Init();

    xTaskCreate(bms_task, "bms_task", 512, NULL, TASK_PRIORITY_4, NULL);
    //xTaskCreate(oled_task, "oled_task", 512, NULL, TASK_PRIORITY_4, NULL);
    vTaskStartScheduler();
}

void oled_task(void *pvParameters)
{
  while (1)
  {
    Int_Screen_Render();
    vTaskDelay(3000);
  }
}


void bms_task(void *pvParameters)
{

  Int_BMS_Init();
  Int_BMS_Balance();
  while (1)
  {  
    // Int_BMS_PrintInfo();
    // vTaskDelay(3000);
  }
}

#include "Int_BMS.h"
#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2c.h"
#include "Int_BQ769_BSP.h"
#include <stdio.h>
#include "Com_BQ769.h"


// R/W
#define I2C_ADDRESS 0x08
#define I2C_ADDRESS_W ((0x08 << 1) + 0) 
#define I2C_ADDRESS_R ((0x08 << 1) + 1)

#define OV_VLAUE 4.2
#define UV_VLAUE 3.2
#define VOL_RANGE_MAX 4.2
#define VOL_RANGE_MIN 3.2

int8_t adc_offset=0;
double adc_gain=0;


RegisterGroup register_all;

BMS_Info bms_info;

uint8_t crc8(uint8_t *data, uint16_t length)
{
  uint8_t i;
  uint8_t crc = 0; // Initial value
  while (length--)
  {
    crc ^= *data++; // crc ^= *data; data++;
    for (i = 0; i < 8; i++)
    {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x07;
      else
        crc <<= 1;
    }
  }
  return crc;
}


/**
 * BMS管理芯片 - I2C数据通信 (CRC)
 */
void __Int_BMS_I2C_WriteData(uint16_t mem_address, uint8_t data) 
{
  uint8_t crc_data[3]={I2C_ADDRESS_W, mem_address, data};
  uint8_t crc = crc8(crc_data, 3);
  uint8_t data_buffer[2]={data, crc};
  taskENTER_CRITICAL();
  HAL_I2C_Mem_Write(&hi2c2, I2C_ADDRESS_W, mem_address,I2C_MEMADD_SIZE_8BIT,  data_buffer, 2, 1000);
  taskEXIT_CRITICAL();
  vTaskDelay(5);
}

void __Int_BMS_I2C_ReadData(uint16_t mem_address, uint8_t *data, uint16_t data_length)
{
  uint8_t *tmp_data=pvPortCalloc(data_length*2,sizeof(uint8_t));
  taskENTER_CRITICAL();
  HAL_I2C_Mem_Read(&hi2c2, I2C_ADDRESS_R, mem_address,I2C_MEMADD_SIZE_8BIT, tmp_data, data_length*2, 1000);
  taskEXIT_CRITICAL();
  for(uint16_t i=0;i<data_length;i++)
  {
    uint8_t rec_data=tmp_data[i*2];
    uint8_t rec_crc=tmp_data[i*2+1]; 

    uint8_t crc = 0;
    if (i==0)
    {
      uint8_t crc_data[2]={I2C_ADDRESS_R, rec_data};
      crc = crc8(crc_data, 2);
    }
    else
    {
      uint8_t crc_data[1]={rec_data};
      crc = crc8(crc_data, 1);
    }
    if (crc!=rec_crc)
    {
      printf("CRC error\n");
      return;
    }
    else
    {
      data[i]=rec_data;
    }
    
  }
  vPortFree(tmp_data);
}

/**
 * BMS管理芯片 - 进入低功耗模式
 *     使用IIC向寄存器写入2次数据
 *     第一次写入：SYS_CTRL1.SHUT_A = 0, SYS_CTRL1.SHUT_B = 1
 *     第二次写入：SYS_CTRL1.SHUT_A = 1, SYS_CTRL1.SHUT_B = 0
 */
void __Int_BMS_Enter_Ship(void) {

    register_all.SysCtrl1.SysCtrl1Byte = 0;
    register_all.SysCtrl1.SysCtrl1Bit.SHUT_A = 0;
    register_all.SysCtrl1.SysCtrl1Bit.SHUT_B = 0;
    __Int_BMS_I2C_WriteData(BQ_SYS_CTRL1, register_all.SysCtrl1.SysCtrl1Byte); // 0, 0
    register_all.SysCtrl1.SysCtrl1Bit.SHUT_A = 0;
    register_all.SysCtrl1.SysCtrl1Bit.SHUT_B = 1;
    __Int_BMS_I2C_WriteData(BQ_SYS_CTRL1, register_all.SysCtrl1.SysCtrl1Byte); // 0, 1
    register_all.SysCtrl1.SysCtrl1Bit.SHUT_A = 1;
    register_all.SysCtrl1.SysCtrl1Bit.SHUT_B = 0;
    __Int_BMS_I2C_WriteData(BQ_SYS_CTRL1, register_all.SysCtrl1.SysCtrl1Byte);  // 1, 0
}

/**
 * BMS管理芯片 - 进入正常模式
 *    必须在 TS1 引脚上检测到 BOOT 信号（> 1 VBOOT）才能从 SHIP 模式启动 → 正常模式
 */
void __Int_BMS_Enter_Normal(void) {
    // !1. STM32 WKUP PA15 : 拉高
    HAL_GPIO_WritePin(BMS_WKUP_GPIO_Port, BMS_WKUP_Pin, GPIO_PIN_SET);
    // 延迟时间
    vTaskDelay(5);
    // !2. STM32 WKUP PA15 : 拉低
    HAL_GPIO_WritePin(BMS_WKUP_GPIO_Port, BMS_WKUP_Pin, GPIO_PIN_RESET);
    // 延迟时间
    vTaskDelay(5);
}

void __Int_BMS_Read_ADCOffset(void)
{
  __Int_BMS_I2C_ReadData(BQ_ADCOFFSET, (uint8_t *)&adc_offset, 1);
  printf("adc_offset: %d\n", adc_offset);
}

void __Int_BMS_Read_ADCGain(void)
{
  uint8_t adc_gain1=0;
  uint8_t adc_gain2=0;
  __Int_BMS_I2C_ReadData(BQ_ADCGAIN1, &adc_gain1, 1);
  __Int_BMS_I2C_ReadData(BQ_ADCGAIN2, &adc_gain2, 1);

  uint8_t gain=((adc_gain1&0x0C)<<1)|(adc_gain2>>5);
  adc_gain=(gain+365)/1000.0;

  printf("adc_gain: %f\n", adc_gain);
}

/**
 * BMS管理芯片 - 参数配置
 */
void __Int_BMS_Config(void) 
{
  //查数据手册配置参数
  //1. CELLBAL1
  //! CELLBAL1 复位值全 0，0 就是"关闭该节均衡"
  // !——初始化阶段本来就不希望任何一节在放电，所以不写正好是想要的状态。它是做均衡功能时动态写的，不是上电配一次就完事的寄存器。

  //!2. SYS_CTRL1
  register_all.SysCtrl1.SysCtrl1Byte = 0;
  register_all.SysCtrl1.SysCtrl1Bit.ADC_EN = 1;
  register_all.SysCtrl1.SysCtrl1Bit.TEMP_SEL = 1;
  __Int_BMS_I2C_WriteData(BQ_SYS_CTRL1, register_all.SysCtrl1.SysCtrl1Byte);

  //!3. SYS_CTRL2
  register_all.SysCtrl2.SysCtrl2Byte = 0;
  register_all.SysCtrl2.SysCtrl2Bit.DELAY_DIS=0;
  register_all.SysCtrl2.SysCtrl2Bit.CC_EN = 1;
  register_all.SysCtrl2.SysCtrl2Bit.CHG_ON = 1;
  register_all.SysCtrl2.SysCtrl2Bit.DSG_ON = 1;
  __Int_BMS_I2C_WriteData(BQ_SYS_CTRL2, register_all.SysCtrl2.SysCtrl2Byte);


  //!4. PROTECT1
  register_all.Protect1.Protect1Byte = 0;
  register_all.Protect1.Protect1Bit.RSNS = 1;
  register_all.Protect1.Protect1Bit.SCD_DELAY=0x1;
  register_all.Protect1.Protect1Bit.SCD_THRESH=0x2;
  __Int_BMS_I2C_WriteData(BQ_PROTECT1, register_all.Protect1.Protect1Byte);


  //!5. PROTECT2
  register_all.Protect2.Protect2Byte = 0;
  register_all.Protect2.Protect2Bit.OCD_DELAY=0x1;
  register_all.Protect2.Protect2Bit.OCD_THRESH=0xB;
  __Int_BMS_I2C_WriteData(BQ_PROTECT2, register_all.Protect2.Protect2Byte);

  //!6. PROTECT3
  register_all.Protect3.Protect3Byte = 0;
  register_all.Protect3.Protect3Bit.OV_DELAY=0x3;
  register_all.Protect3.Protect3Bit.UV_DELAY=0x2;
  __Int_BMS_I2C_WriteData(BQ_PROTECT3, register_all.Protect3.Protect3Byte);

  //!7. OV_TRIP

  //!8. UV_TRIP

  __Int_BMS_Read_ADCOffset();
  __Int_BMS_Read_ADCGain();
  // 单位统一：OV/UV 宏是 V，*1000 转 mV；adc_offset 是 mV；adc_gain 是 mV/LSB
  // 结果 ov_full/uv_full = OV_TRIP_FULL / UV_TRIP_FULL 的 14 位 LSB 值
  uint16_t ov_full=(OV_VLAUE*1000.0-adc_offset)/adc_gain;
  uint16_t uv_full=(UV_VLAUE*1000.0-adc_offset)/adc_gain;

  // 手册 8.3.1.2.1：OV_TRIP_FULL = 10-XXXX-XXXX-1000（0x2008~0x2FF8）
  //                 UV_TRIP_FULL = 01-XXXX-XXXX-0000（0x1000~0x1FF0）
  // 超出窗口时 >>4 会把高 2 位前缀丢掉，欠压点会掉到 1.6V 才触发，故限幅
  if (ov_full > 0x2FF8) ov_full = 0x2FF8;
  if (uv_full > 0x1FF0) uv_full = 0x1FF0;

  // 地址直接传寄存器地址；>>4 后取中间 8 位
  __Int_BMS_I2C_WriteData(BQ_OV_TRIP, ((ov_full>>4)&0xFF));
  __Int_BMS_I2C_WriteData(BQ_UV_TRIP, ((uv_full>>4)&0xFF));

  //!9. CC_CFG
  __Int_BMS_I2C_WriteData(BQ_CC_CFG, 0x19);

  //10. BAT_HI

  //11. BAT_LO

  //12.TS1_HI

  //13.TS1_LO

  //14. CC_HI

  //15. CC_LO

  //16. ADCGAIN1

  //17. ADCGAIN2

  //18. ADCOFFSET


  
}


void Int_BMS_Init(void)
{
  //进入SHIP模式
  __Int_BMS_Enter_Ship();
  vTaskDelay(300);

  //进入正常模式
  // __Int_BMS_Enter_Normal();
  // vTaskDelay(300);

  //参数配置
  __Int_BMS_Config();


}

//打印电芯的电压
void __Int_BMS_Monitor_CellVoltage(void)
{
  uint8_t reg_vol[10]={0};
  __Int_BMS_I2C_ReadData(BQ_VC1_HI, reg_vol, 10);
  printf("\n");
  for(uint8_t i=0;i<5;i++)
  {
    uint8_t vol_high=reg_vol[i*2];
    uint8_t vol_low=reg_vol[i*2+1];
    uint16_t adc_vol=((vol_high&0x3F)<<8)|vol_low;
    double vol=(adc_gain*adc_vol+adc_offset)/1000;
    printf("%d cell vol :%6.5f V\n", i+1, vol);
    bms_info.cell_vol[i]=vol;
  }
  
}

//打印电池组的电压
void __Int_BMS_Monitor_BatteryVoltage(void)
{
  printf("\n");
  uint8_t reg_vol[2]={0};
  __Int_BMS_I2C_ReadData(BQ_BAT_HI, reg_vol, 2);
  uint8_t vol_high=reg_vol[0];
  uint8_t vol_low =reg_vol[1];
  uint16_t adc_vol=((vol_high<<8)|vol_low);
  double vol=(4*adc_gain*adc_vol+(5*adc_offset))/1000;
  printf("battery vol: %6.5f V\n", vol);
  bms_info.total_vol=vol;
}

//打印电池组的电流
void __Int_BMS_Monitor_BatteryCurrent(void)
{
  uint8_t reg_vol[2]={0};
  __Int_BMS_I2C_ReadData(BQ_CC_HI, reg_vol, 2);
  uint8_t vol_high=reg_vol[0];
  uint8_t vol_low =reg_vol[1];
  int16_t adc_vol=((vol_high<<8)|vol_low);

  float current=(adc_vol*8.44)/(5*1000);
  bms_info.current=current;
  printf("battery current: %6.5f A\n", current);
}

//打印温度
void __Int_BMS_Monitor_Temperature(void)
{
  uint8_t reg_vol[2]={0};
  __Int_BMS_I2C_ReadData(BQ_TS1_HI, reg_vol, 2);
  uint8_t vol_high=reg_vol[0];
  uint8_t vol_low =reg_vol[1];
  int16_t adc_vol=(((vol_high&0x3F)<<8)|vol_low);

  float VTSX=adc_vol*0.000382;
  float RTS=(10000*VTSX)/(3.3-VTSX);

  int8_t temp=Com_BQ769_GetTemperatureByRes(RTS);
  printf("temp: %d T\n", temp);
  bms_info.temperature=temp;
}

//打印电池组的SOC
void __Int_BMS_Monitor_BatterySOC(void)
{
    float sum = 0;
    for (size_t i = 0; i < 5; i++)
    {
        bms_info.power_level_soc = Com_BQ769_GetSocByVol(bms_info.cell_vol[i] * 1000);
        sum += bms_info.power_level_soc;
    }
    float bat_soc = sum / 5; // 拿每个电池的soc的均值 当作整个电池组的百分比的
    // 根据温度对soc进行修正
    int8_t bat_temperature=bms_info.temperature;
    if (bat_temperature >= 0 && bat_temperature <= 40)
    {
        bat_soc = bat_soc * (1 + (-0.005) * (bat_temperature - 25));
    }
    else if (bat_temperature < 0)
    {
        bat_soc = bat_soc * (1 + (-0.005) * (0 - 25) + (-0.005 + 0.002) * (bat_temperature - 0));
    }
    else if (bat_temperature > 40)
    {
        bat_soc = bat_soc * (1 + (-0.005) * (40 - 25) + (-0.005 - 0.003) * (bat_temperature - 40));
    }

    printf("bat_soc:%1.2f %%\r\n", bat_soc);
    bms_info.power_level_soc=bat_soc;
}

void Int_BMS_PrintInfo(void)
{
  while(1)
  {
    __Int_BMS_Monitor_CellVoltage();
    printf("------------------------------------\n");
    __Int_BMS_Monitor_BatteryVoltage();
    printf("------------------------------------\n");
    __Int_BMS_Monitor_BatteryCurrent();
    printf("------------------------------------\n");
    __Int_BMS_Monitor_Temperature();
    printf("------------------------------------\n");
    __Int_BMS_Monitor_BatterySOC();
    vTaskDelay(1000);
  }
}


void Int_BMS_Monitor(void)
{
  __Int_BMS_Monitor_CellVoltage();
  __Int_BMS_Monitor_BatteryVoltage();
  __Int_BMS_Monitor_BatteryCurrent();
  __Int_BMS_Monitor_Temperature();
  __Int_BMS_Monitor_BatterySOC();

}

void Int_BMS_Balance(void)
{
   uint8_t bal_state[5]={0,0,0,0,0};
   float cell_vol[5]={4.2,4.0,3.9,3.7,4.1};
   uint8_t max_index=0;
   uint8_t min_index=0;
    for(uint8_t i=0;i<5;i++)
    {
        if(cell_vol[i]>cell_vol[max_index])
        {
            max_index=i;
        }
        if(cell_vol[i]<cell_vol[min_index])
        {
            min_index=i;
        }
    }
    printf("max_index: %d, min_index: %d\n", max_index, min_index);
    if (cell_vol[max_index]>VOL_RANGE_MAX)
    {
      printf("vol too high , no need to balance\n");
      return;
    }
    if (cell_vol[min_index]<VOL_RANGE_MIN)
    {
      printf("vol too low , no need to balance\n");
      return;
    }
    if ((cell_vol[max_index]-cell_vol[min_index])<0.03)
    {
      printf("vol range is small , no need to balance\n");
      return;
    }
    if((cell_vol[max_index]-cell_vol[min_index])>1)
    {
      printf("vol range is big , no need to balance\n");
      return;
    }
    if(bms_info.temperature>55)
    {
      printf("temp too high , no need to balance\n");
      return;
    }
    for(uint8_t i=0;i<5;i++)
    {
        if(i==min_index)
        {
          continue;
        }
        else
        {
          if(cell_vol[i]-cell_vol[min_index]>0.03)
          {
            bal_state[i]=1;
          }
        }
    }
    if(max_index==0)
    {
      bal_state[max_index+1]=0;
    }
    else if(max_index==4)
    {
      bal_state[max_index-1]=0;
    }
    else 
    {
      bal_state[max_index+1]=0;
      bal_state[max_index-1]=0;
    }
    
    for(uint8_t i=0;i<5;i+=2)
    {
      if(i+1>5-1)
      {
        if(bal_state[i-1]==1)
        {
          bal_state[i]=0;
        }
      }
      else
      {
        if(bal_state[i]==1&&bal_state[i+1]==1)
        {
          if(i==0)
          {
            if(cell_vol[i]>=cell_vol[i+1])
            {
              bal_state[i+1]=0;
            }
            else
            {
              bal_state[i]=0;
            }
          }
          else
          {
            if(bal_state[i-1]==1)
            {
              bal_state[i]=0;
            }
            else
            {
              if(cell_vol[i]>=cell_vol[i+1])
              {
              bal_state[i+1]=0;
              }
              else
              {
              bal_state[i]=0;
              }
            }
          }
        }
      }
    }
 printf("bal_state: %d,%d,%d,%d,%d\r\n", bal_state[0], bal_state[1], bal_state[2], bal_state[3], bal_state[4]);
}

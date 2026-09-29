/**
  ******************************************************************************
  * @file    stm32f4xx_hal_msp.c
  * @brief   HAL 的 MCU 支持包（MSP）初始化
  *
  *  本工程把每个外设的引脚复用和时钟使能都写在各自的 BSP 文件里
  *  （bsp_sdram.c / bsp_lcd.c / bsp_adc.c / bsp_dcmi.c / dht11.c），
  *  这样"一个外设的配置只在一个文件里"，改引脚不会漏改。
  *
  *  所以这里只保留 HAL_Init() 需要的全局 MSP：SYSCFG 与 PWR 时钟。
  *  HAL_LTDC_MspInit / HAL_ADC_MspInit / HAL_DCMI_MspInit 等都使用
  *  HAL 里的 __weak 空实现。
  ******************************************************************************
  */

#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

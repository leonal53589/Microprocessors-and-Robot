/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   基础 GPIO：4 个板载按键
  *
  *  其余引脚（SDRAM、LTDC、DHT11、ADC、DCMI、SCCB）分别由各自的 BSP
  *  初始化函数配置，避免这里和 BSP 抢同一批引脚（本实验引脚资源很紧张）。
  ******************************************************************************
  */

#include "gpio.h"
#include "pinmap.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();

  /* ---- 4 个板载按键：K0/K1/K2 低电平有效（上拉），WK_UP 高电平有效（下拉） ---- */
  gpio.Mode  = GPIO_MODE_INPUT;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pull  = GPIO_PULLUP;
  gpio.Pin   = KEY0_PIN | KEY1_PIN;
  HAL_GPIO_Init(GPIOH, &gpio);

  gpio.Pull = GPIO_PULLUP;
  gpio.Pin  = KEY2_PIN;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pull = GPIO_PULLDOWN;
  gpio.Pin  = KEYUP_PIN;
  HAL_GPIO_Init(GPIOA, &gpio);
}

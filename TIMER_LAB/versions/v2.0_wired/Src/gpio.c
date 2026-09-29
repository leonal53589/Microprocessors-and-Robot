/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   基础 GPIO：4 个板载按键 + 实验二的提示 LED
  *
  *  其余引脚（OLED、PWM、捕获、BKIN）分别由各自的 BSP 初始化函数配置，
  *  避免这里和 BSP 抢同一批引脚。
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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  /* ---- 4 个板载按键：输入，低电平有效的上拉 / 高电平有效的下拉 ---- */
  gpio.Mode  = GPIO_MODE_INPUT;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pull = GPIO_PULLUP;
  gpio.Pin  = KEY0_PIN | KEY1_PIN;
  HAL_GPIO_Init(GPIOH, &gpio);

  gpio.Pull = GPIO_PULLUP;
  gpio.Pin  = KEY2_PIN;
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pull = GPIO_PULLDOWN;
  gpio.Pin  = KEYUP_PIN;
  HAL_GPIO_Init(GPIOA, &gpio);

  /* 注意：实验二的提示 LED 不在这里初始化。
   * 因为 PB1 在实验三里是 TIM3_CH4 复用输出，两个模式会互相覆盖，
   * 所以由 app.c 在进入实验二时重新配置（react_led_init）。 */
}

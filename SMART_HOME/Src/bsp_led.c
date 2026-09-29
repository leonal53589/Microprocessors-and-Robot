/**
  ******************************************************************************
  * @file    bsp_led.c
  * @brief   板载双色 LED 驱动
  *
  *  硬件（《阿波罗V2 硬件参考手册》LED 原理图）：
  *      VCC3.3 --[R65 510R]-- DS1(绿) -- PB0
  *      VCC3.3 --[R62 510R]-- DS0(红) -- PB1
  *  电流由 VCC 经限流电阻、LED 流入 MCU 引脚（灌电流），所以：
  *      输出低电平 = 点亮，输出高电平 = 熄灭
  ******************************************************************************
  */

#include "bsp_led.h"
#include "pinmap.h"

static uint8_t s_green_on;
static uint8_t s_red_on;

void BSP_Led_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  gpio.Pin   = LED_GREEN_PIN | LED_RED_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);

  /* 上电默认全灭：绿灯灭、红灯灭（红灯是"故障/报警"色，不能默认亮） */
  BSP_Led_Green(0U);
  BSP_Led_Red(0U);
}

void BSP_Led_Green(uint8_t on)
{
  HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN,
                    (on != 0U) ? LED_ON_LEVEL : LED_OFF_LEVEL);
  s_green_on = (on != 0U) ? 1U : 0U;
}

void BSP_Led_Red(uint8_t on)
{
  HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN,
                    (on != 0U) ? LED_ON_LEVEL : LED_OFF_LEVEL);
  s_red_on = (on != 0U) ? 1U : 0U;
}

uint8_t BSP_Led_GreenIsOn(void)
{
  return s_green_on;
}

uint8_t BSP_Led_RedIsOn(void)
{
  return s_red_on;
}

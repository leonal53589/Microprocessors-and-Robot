/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */
/* 注意：本工程原来的 PF8 输出配置（曾标注为 BEEP）已删除。
 * 手册 2.2.6 明确：PF8 是 W25Q256 Flash 的 SPI5_MISO，
 * 把它配成推挽输出会与 Flash 的输出对拉（总线争用）。
 * 真正的蜂鸣器挂在 PCF8574 的 P0 上，由 pcf8574.c 通过软件 IIC 控制。
 * 详见《阿波罗V2 硬件参考手册》2.1.24 与 2.2.6。                     */
/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, LED3_Pin|LED5_Pin|LED6_Pin|LED7_Pin
                          |LED0_Pin|LED1_Pin|LED2_Pin|LED4_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : LED3_Pin LED5_Pin LED6_Pin LED7_Pin
                           LED0_Pin LED1_Pin LED2_Pin LED4_Pin */
  GPIO_InitStruct.Pin = LED3_Pin|LED5_Pin|LED6_Pin|LED7_Pin
                          |LED0_Pin|LED1_Pin|LED2_Pin|LED4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : KEY2_Pin */
  GPIO_InitStruct.Pin = KEY2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(KEY2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : KEY1_Pin KEY0_Pin */
  GPIO_InitStruct.Pin = KEY1_Pin|KEY0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */

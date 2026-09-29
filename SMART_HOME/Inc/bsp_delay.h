/**
  ******************************************************************************
  * @file    bsp_delay.h
  * @brief   微秒/毫秒级忙等延时（DWT 周期计数器）
  *
  *  为什么不用 HAL_Delay：
  *    HAL_Delay 只能到 1ms，而 DHT11 的单总线时序要求 20~40us 级精度；
  *    DHT11 读期间还要关中断，不能依赖 SysTick。
  *    DWT->CYCCNT 由内核时钟直接驱动，不占外设、不受中断影响，最合适。
  ******************************************************************************
  */

#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 使能 DWT 周期计数器（在 SystemClock_Config 之后调用一次即可） */
void BSP_DelayInit(void);

/* 忙等延时 */
void BSP_DelayUs(uint32_t us);
void BSP_DelayMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DELAY_H */

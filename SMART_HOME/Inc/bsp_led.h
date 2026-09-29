/**
  ******************************************************************************
  * @file    bsp_led.h
  * @brief   板载双色 LED（PB0 绿 DS1 / PB1 红 DS0，低电平点亮）
  *
  *  本文件只提供"点亮/熄灭"这类原子动作，报警灯怎么闪由 app.c 的状态机决定，
  *  这样驱动层不含业务逻辑。
  ******************************************************************************
  */

#ifndef __BSP_LED_H
#define __BSP_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void BSP_Led_Init(void);

/* on != 0 点亮，on == 0 熄灭 */
void BSP_Led_Green(uint8_t on);
void BSP_Led_Red(uint8_t on);

/* 读取当前状态（0/1），方便自检与报告记录 */
uint8_t BSP_Led_GreenIsOn(void);
uint8_t BSP_Led_RedIsOn(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H */

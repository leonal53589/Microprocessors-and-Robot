/**
  ******************************************************************************
  * @file    bsp_key.h
  * @brief   板载按键扫描：20ms 时间戳消抖 + 只在"按下"边沿返回事件
  *
  *  与实验二 TIMER_LAB 的做法保持一致（同一块板、同一套按键习惯），
  *  这样从实验二移植过来的操作手感不变。
  ******************************************************************************
  */

#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  KEY_EVENT_NONE = 0,
  KEY_EVENT_K0,          /* PH3  ：切换页面 / 确认 */
  KEY_EVENT_K1,          /* PH2  ：重试摄像头 / 清除提示 */
  KEY_EVENT_K2,          /* PC13 ：预留 */
  KEY_EVENT_UP           /* PA0  ：切换页面（与 K0 等效，方便单手操作） */
} KeyEvent;

void      BSP_Key_Init(void);
KeyEvent  BSP_Key_Scan(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H */

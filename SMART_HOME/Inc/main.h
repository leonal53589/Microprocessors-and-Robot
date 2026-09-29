/**
  ******************************************************************************
  * @file    main.h
  * @brief   SMART_HOME 公共头文件
  *  引脚分配统一放在 pinmap.h 里，本文件只保留 HAL 与错误处理声明。
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

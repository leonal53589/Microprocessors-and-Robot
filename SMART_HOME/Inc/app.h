/**
  ******************************************************************************
  * @file    app.h
  * @brief   应用层：中控首页 / 摄像头预览页 与阈值预警状态机
  ******************************************************************************
  */

#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void App_Init(void);
void App_Task(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */

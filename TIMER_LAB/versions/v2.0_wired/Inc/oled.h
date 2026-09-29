/**
  ******************************************************************************
  * @file    oled.h
  * @brief   正点原子 0.96' OLED (SSD1306, 128x64) 驱动
  *          接口模式由 pinmap.h 里的 OLED_IF_MODE 选择（8080 / 4线SPI / IIC）
  ******************************************************************************
  */

#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "pinmap.h"

#define OLED_WIDTH      128U
#define OLED_HEIGHT     64U
#define OLED_PAGES      (OLED_HEIGHT / 8U)

/* 写标志 */
#define OLED_CMD        0U
#define OLED_DATA       1U

void    OLED_Init(void);
void    OLED_Clear(void);                       /* 清 GRAM（需再 Refresh） */
void    OLED_Refresh(void);                     /* GRAM -> 屏幕 */

/* 在 (x, y) 像素坐标显示字符串；自动识别 ASCII(8x16) 与 UTF-8 中文(16x16) */
void    OLED_ShowString(uint8_t x, uint8_t y, const char *str);
/* 显示有符号整数（最多 8 位） */
void    OLED_ShowInt(uint8_t x, uint8_t y, int32_t num);

/* 返回 0 表示未检测到 OLED（仅 IIC 模式能探测；8080/SPI 无法回读） */
uint8_t OLED_Probe(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */

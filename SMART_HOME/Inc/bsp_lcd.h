/**
  ******************************************************************************
  * @file    bsp_lcd.h
  * @brief   7 寸 RGB 屏驱动（面板 1024x600，界面 800x480 居中；LTDC + 外部 SDRAM 帧缓冲）
  *
  *  分层：
  *      bsp_sdram.c  把 SDRAM 打开（帧缓冲的物理内存）
  *      bsp_lcd.c    LTDC 时序/图层 + 画点/画框/画字符串（本文件）
  *      bsp_font.c   点阵字库查询
  *      app.c        界面布局与局部刷新策略
  *
  *  为什么可以"局部刷新不闪"：
  *      LTDC 是硬件持续扫描 SDRAM 里的帧缓冲，CPU 改哪个像素屏上就变哪个像素，
  *      所以只重画数值所在的小矩形即可，不需要"清屏 + 整屏重画"。
  ******************************************************************************
  */

#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "bsp_font.h"

/* ---- RGB565 颜色（LCD_COLOR(r,g,b) 把 8/8/8 压到 5/6/5） ---- */
#define LCD_COLOR(r, g, b)  ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | (((b) & 0xF8U) >> 3)))

#define LCD_BLACK       LCD_COLOR(0,   0,   0)
#define LCD_WHITE       LCD_COLOR(255, 255, 255)
#define LCD_RED         LCD_COLOR(255, 0,   0)
#define LCD_GREEN       LCD_COLOR(0,   200, 0)
#define LCD_BLUE        LCD_COLOR(0,   0,   255)
#define LCD_YELLOW      LCD_COLOR(255, 220, 0)
#define LCD_ORANGE      LCD_COLOR(255, 140, 0)
#define LCD_GRAY        LCD_COLOR(120, 120, 120)
#define LCD_LIGHTGRAY   LCD_COLOR(200, 200, 200)
#define LCD_DARKGRAY    LCD_COLOR(60,  60,  60)
#define LCD_NAVY        LCD_COLOR(20,  40,  90)
#define LCD_TEAL        LCD_COLOR(0,   120, 120)

/* 初始化：配置 LTDC 引脚、PLLSAI 时钟、LTDC 时序与第 1 层、打开背光
 * 返回 0 表示成功（SDRAM 必须先初始化好） */
uint8_t BSP_LCD_Init(void);

/* 背光（PB5） */
void BSP_LCD_Backlight(uint8_t on);

/* 直接拿到帧缓冲首地址（摄像头预览等需要直接写屏时用） */
uint16_t *BSP_LCD_GetFrameBuffer(void);

/* ---- 基本绘图（全部带裁剪，越界不会写坏内存） ---- */
void BSP_LCD_Clear(uint16_t color);
void BSP_LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void BSP_LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void BSP_LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void BSP_LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
void BSP_LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

/* ---- 文本（背景不透明，因此局部刷新不会留下残影）
 *      返回值为本次绘制的像素宽度 ---- */
uint16_t BSP_LCD_DrawString(uint16_t x, uint16_t y, const char *text,
                            uint16_t fg, uint16_t bg, LcdFontSize size);
uint16_t BSP_LCD_DrawInt(uint16_t x, uint16_t y, int32_t value,
                         uint16_t fg, uint16_t bg, LcdFontSize size);
uint16_t BSP_LCD_DrawFixed2(uint16_t x, uint16_t y, float value,
                            uint16_t fg, uint16_t bg, LcdFontSize size);

/* 居中绘制一整行文字（按像素宽度算），返回起始 x */
uint16_t BSP_LCD_DrawStringCentered(uint16_t y, const char *text,
                                    uint16_t fg, uint16_t bg, LcdFontSize size);

/* ---- 图像搬运 ---- */
/* 1:1 拷贝 w*h 的 RGB565 图像到 (x,y) */
void BSP_LCD_BlitRGB565(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *src);
/* 2 倍最近邻放大拷贝（摄像头 QVGA -> 640x480 预览） */
void BSP_LCD_BlitRGB565x2(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *src);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LCD_H */

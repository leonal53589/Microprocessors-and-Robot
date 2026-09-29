/**
  ******************************************************************************
  * @file    bsp_font.h
  * @brief   点阵字库访问接口（数据在 Inc/lcdfont.h，由 tools/gen_font.py 生成）
  *
  *  两套字号：
  *      LCD_FONT_SMALL : ASCII 12x24 / 汉字 24x24   —— 正文与数值
  *      LCD_FONT_LARGE : ASCII 16x32 / 汉字 32x32   —— 标题
  *  UTF-8 直接可用：界面上写 "温度" 这样的字面量即可，不用查编码。
  ******************************************************************************
  */

#ifndef __BSP_FONT_H
#define __BSP_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  LCD_FONT_SMALL = 0,
  LCD_FONT_LARGE = 1
} LcdFontSize;

/* 单个字符的像素宽度 / 字库行高 */
uint16_t LCD_FontAsciiWidth(LcdFontSize size);
uint16_t LCD_FontCnWidth(LcdFontSize size);
uint16_t LCD_FontHeight(LcdFontSize size);

/* 取字形点阵首地址，找不到返回 NULL */
const uint8_t *LCD_FontFindAscii(uint8_t ch, LcdFontSize size);
const uint8_t *LCD_FontFindCn(const char *utf8, LcdFontSize size);

/* UTF-8 首字节 -> 该字符占几个字节（1~4） */
uint8_t LCD_FontUtf8Len(uint8_t lead);

/* 一整串文字（中英混排）的像素宽度，用于居中排版 */
uint16_t LCD_FontTextWidth(const char *utf8, LcdFontSize size);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_FONT_H */

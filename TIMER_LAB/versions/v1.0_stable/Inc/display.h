/**
  ******************************************************************************
  * @file    display.h
  * @brief   统一显示接口（把底层 OLED 写命令/写数据 与上层文本显示分离）
  *
  *  实验要求：至少封装【清屏】【指定位置显示字符串】【显示整数】三个接口。
  *  屏幕 128x64，按 16 像素行高可放 4 行文本。
  ******************************************************************************
  */

#ifndef __DISPLAY_H
#define __DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define DISP_LINE_H     16U
#define DISP_LINES      4U

/* ============ 实验要求的基础三接口 ============ */
void Disp_Clear(void);                                  /* 清屏并立即刷新   */
void Disp_String(uint8_t x, uint8_t y, const char *str);/* 指定像素位置字符串 */
void Disp_Int(uint8_t x, uint8_t y, int32_t num);       /* 指定位置显示整数  */

/* ============ 便利接口 ============ */
void Disp_Init(void);
void Disp_Refresh(void);
void Disp_Line(uint8_t line, const char *str);                            /* 整行字符串 */
void Disp_LineInt(uint8_t line, const char *label, int32_t num);          /* 标签+整数 */
void Disp_Fixed1(uint8_t x, uint8_t y, int32_t value_x10);                /* 保留 1 位小数 */

#ifdef __cplusplus
}
#endif

#endif /* __DISPLAY_H */

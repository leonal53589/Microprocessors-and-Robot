/**
  ******************************************************************************
  * @file    bsp_dcmi.h
  * @brief   摄像头数据通路：DCMI + DMA2（把 OV5640 的像素搬进 SDRAM）
  *
  *  分工：
  *      ov5640.c  —— 通过 SCCB 配置传感器（输出 RGB565 QVGA）
  *      bsp_dcmi.c —— DCMI 接口 + DMA 搬运（本文件）
  *      bsp_lcd.c —— 把 SDRAM 里的图像放大显示到 RGB 屏
  *
  *  为什么用"单帧抓取 + 处理完再启动下一帧"而不是连续模式：
  *      连续模式需要 DMA 双缓冲（多缓冲模式），代码复杂度高；
  *      本实验是"预览"，每秒 10 帧左右就足够，而且单帧模式下
  *      CPU 永远读的是已经搬完的缓冲区，不会读到半帧图像（不会撕裂）。
  ******************************************************************************
  */

#ifndef __BSP_DCMI_H
#define __BSP_DCMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 配置 DCMI 引脚（AF13）、DMA2_Stream1_Channel1、两个中断，返回 0 成功 */
uint8_t BSP_DCMI_Init(void);

/* 启动一帧抓取：buf 为 SDRAM 中的 RGB565 缓冲，length_words = 字节数/4 */
uint8_t BSP_DCMI_StartFrame(uint16_t *buf, uint32_t length_words);

/* 停止抓取（切换页面/出错时调用） */
void BSP_DCMI_Stop(void);

/* 取走"新帧完成"标志：返回 1 表示刚搬完一帧；返回 0 表示还没有新数据 */
uint8_t BSP_DCMI_TakeFrame(void);

/* 采集出错标志（DMA 错误 / DCMI 溢出等） */
uint8_t  BSP_DCMI_HasError(void);
void     BSP_DCMI_ClearError(void);

/* 累计完成帧数（报告里可以用来估算帧率） */
uint32_t BSP_DCMI_FrameCount(void);

/* 就地交换 RGB565 的高低字节（DCMI 8 位模式下如果颜色明显不对就调用它） */
void BSP_DCMI_SwapBytes(uint16_t *buf, uint32_t pixels);

/* 中断入口：由 stm32f4xx_it.c 转发，句柄保持在本文件内部 */
void BSP_DCMI_IRQHandler(void);
void BSP_DCMI_DmaIRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DCMI_H */

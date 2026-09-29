/**
  ******************************************************************************
  * @file    bsp_sdram.h
  * @brief   外部 SDRAM（W9825G6KH，32MB，16 位）驱动
  *
  *  为什么要 SDRAM：
  *      7 寸屏 800x480 的 RGB565 单帧 = 800*480*2 = 750KiB，
  *      而 STM32F429IGT6 片内 SRAM 只有 192KiB + 64KiB CCM，
  *      一帧都放不下，所以帧缓冲必须放在外部 SDRAM 里，
  *      摄像头图像缓冲同样放在 SDRAM（见 pinmap.h 的地址分配）。
  ******************************************************************************
  */

#ifndef __BSP_SDRAM_H
#define __BSP_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 初始化 FMC + SDRAM 器件，返回 0 表示成功 */
uint8_t BSP_SDRAM_Init(void);

/* 读写自检：在帧缓冲以外的区域做多种图案测试，返回 0 表示全部通过 */
uint8_t BSP_SDRAM_Test(void);

/* 当前的刷新计数器值（写报告时可以记录一下） */
uint32_t BSP_SDRAM_RefreshCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDRAM_H */

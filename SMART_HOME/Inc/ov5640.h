/**
  ******************************************************************************
  * @file    ov5640.h
  * @brief   正点原子阿波罗 V2 + ATK-OV5640 摄像头模块驱动（SCCB 软件模拟 + 传感器配置）
  *
  *  硬件连接（依据《阿波罗V2 硬件参考手册》MCU 引脚分配表 / 《开发指南》DCMI 章节）
  *  ---------------------------------------------------------------------------
  *  模块   : ATK-OV5640，直接插在板载 P6 “OLED/CAMERA” 2x9 排针上
  *  时钟   : 模块【自带 24MHz 有源晶振】，所以主控不需要输出 XCLK，
  *           PA8（MCO1）保持不动，本驱动也不去配置任何时钟输出
  *  SCCB   : SCL = PB4，SDA = PB3，软件模拟（板上已有 4.7K 外部上拉）
  *           PB3/PB4 与 JTAG 的 JTDO / NJTRST 复用，本驱动当成普通 GPIO 用，
  *           因此调试【只能用 SWD】，不能再挂 JTAG（阿波罗板默认就是 SWD）
  *  复位   : RST = PA15，低电平有效；模块【没有 PWDN 引脚】，不需要电源使能脚
  *  DCMI   : PC6 PC7 PC8 PC9 PC11 PD3 PB8 PB9 / PA6(PCLK) / PH8(HREF) / PB7(VSYNC)
  *           这些引脚以及 DCMI 外设寄存器全部由 bsp_dcmi.c 负责配置，
  *           本文件只做 SCCB 通信 + 传感器内部寄存器配置，绝不碰 DCMI
  *  输出   : 固定 RGB565（0x4300 = 0x61），默认 QVGA 320x240
  ******************************************************************************
  */

#ifndef __OV5640_H
#define __OV5640_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ---------------------------------------------------------------- 返回状态 */
typedef enum {
  OV5640_OK = 0,
  OV5640_ERR_SCCB,      /* SDA/SCL 没有响应（器件不在线 / 丢上拉 / 接线错） */
  OV5640_ERR_ID,        /* 读回 ID != 0x5640，不是 OV5640                    */
  OV5640_ERR_TIMEOUT    /* 等待超时（本驱动只在重试耗尽时使用）              */
} OV5640_Status;

/* ---------------------------------------------- 本驱动默认输出尺寸（RGB565） */
#define OV5640_WIDTH   320U      /* QVGA  */
#define OV5640_HEIGHT  240U

/* OV5640_SetOutputSize() 目前支持的两种尺寸 */
#define OV5640_QVGA_WIDTH   320U
#define OV5640_QVGA_HEIGHT  240U
#define OV5640_VGA_WIDTH    640U
#define OV5640_VGA_HEIGHT   480U

/* 0x300A(高) + 0x300B(低) 拼出来的产品 ID */
#define OV5640_CHIP_ID      0x5640U

/* -------------------------------------------------------------------- API */
OV5640_Status OV5640_Init(void);                            /* 复位 + 查 ID + 配 QVGA RGB565 */
OV5640_Status OV5640_Probe(uint16_t *pid);                  /* 读 16 位产品 ID               */
OV5640_Status OV5640_SetOutputSize(uint16_t w, uint16_t h); /* 320x240 或 640x480            */
uint8_t       OV5640_IsReady(void);                         /* 1 = 已成功 Init               */
OV5640_Status OV5640_WriteReg(uint16_t reg, uint8_t val);   /* 裸 SCCB 写，便于调参          */
OV5640_Status OV5640_ReadReg(uint16_t reg, uint8_t *val);   /* 裸 SCCB 读                    */

#ifdef __cplusplus
}
#endif

#endif /* __OV5640_H */

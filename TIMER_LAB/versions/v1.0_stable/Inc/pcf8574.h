/**
  ******************************************************************************
  * @file    pcf8574.h
  * @brief   正点原子阿波罗 V2 板载 PCF8574（IIC IO 扩展芯片）驱动
  *
  *  硬件（《阿波罗V2 硬件参考手册》2.1.14 / 2.1.24）
  *  ---------------------------------------------------------------------------
  *  总线   : IIC_SCL = PH4, IIC_SDA = PH5  （板载 4.7K 上拉，与 24C02 /
  *           AP3216C / SH3001 / ST480MC / ES8388 共用一条总线）
  *  地址   : A2/A1/A0 全部接地 -> 7 位地址 0x20
  *  扩展口 :
  *      P0  BEEP        有源蜂鸣器（经 S8550 PNP 高边开关，低电平响）
  *      P1  AP_INT      光环境传感器中断
  *      P2  DCMI_PWDN   OLED/摄像头模块电源
  *      P3  USB_PWR     USB HOST 供电控制
  *      P4  EXIO        多余 IO，引到 P3 排针
  *      P5  6D_INT      六轴传感器中断
  *      P6  RS485_RE    RS485 收发方向
  *      P7  ETH_RESET   网口复位（经三极管反向）
  *
  *  为什么用软件 IIC：手册 2.2.7 明确写"我们并不提倡使用硬件 IIC，
  *  因为 STM32 的 IIC 非常不好用！请慎用。"
  ******************************************************************************
  */

#ifndef __PCF8574_H
#define __PCF8574_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ---------------------------------------------------------------- 地址 */
#define PCF8574_ADDR7       0x20U                              /* 7 位地址 */
#define PCF8574_ADDR_W      ((uint8_t)(PCF8574_ADDR7 << 1))    /* 写 */ 
#define PCF8574_ADDR_R      ((uint8_t)((PCF8574_ADDR7 << 1) | 1U)) /* 读 */

/* ------------------------------------------------------------ 位定义 */
#define PCF8574_P0          0x01U   /* BEEP       有源蜂鸣器       */
#define PCF8574_P1          0x02U   /* AP_INT     光环境传感器中断 */
#define PCF8574_P2          0x04U   /* DCMI_PWDN  OLED/摄像头电源  */
#define PCF8574_P3          0x08U   /* USB_PWR    USB HOST 供电    */
#define PCF8574_P4          0x10U   /* EXIO       引出 P3 排针     */
#define PCF8574_P5          0x20U   /* 6D_INT     六轴传感器中断   */
#define PCF8574_P6          0x40U   /* RS485_RE   RS485 方向       */
#define PCF8574_P7          0x80U   /* ETH_RESET  网口复位         */

/* ------------------------------------------------------------- 蜂鸣器 */
/* 原理图（手册图 2.1.24.1）：
 *     BEEP --[R72 1K]-- B
 *     VCC3.3 ---------- E   Q4 = S8550（PNP）
 *                       C ---- BEEP+
 *     GND -------------------- BEEP-
 * 基极拉低 -> 导通 -> 响；基极拉高 -> 截止 -> 不响。          */
#define PCF8574_BEEP_BIT    PCF8574_P0
#define PCF8574_BEEP_ON     0U      /* P0 = 0 -> 响   */
#define PCF8574_BEEP_OFF    1U      /* P0 = 1 -> 不响 */

/* -------------------------------------------------------------- 接口 */
uint8_t PCF8574_Init(void);                              /* 0 = 成功 */
uint8_t PCF8574_Probe(void);                             /* 0 = 器件在线 */
uint8_t PCF8574_Write(uint8_t value);                    /* 0 = 成功 */
uint8_t PCF8574_Read(uint8_t *value);                    /* 0 = 成功 */
uint8_t PCF8574_WriteBit(uint8_t bitmask, uint8_t level);/* 0 = 成功 */
uint8_t PCF8574_ReadBit(uint8_t bitmask, uint8_t *level);

#ifdef __cplusplus
}
#endif

#endif /* __PCF8574_H */

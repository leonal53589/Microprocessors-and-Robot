/**
  ******************************************************************************
  * @file    bsp_adc.h
  * @brief   电子电压表：ADC1_IN5（PA5，板上 RV1 电位器 0~3.3V）
  *
  *  为什么用多次平均：
  *      RV1 是机械电位器，滑动端有接触噪声；ADC 本身也有 ±1~2 LSB 抖动。
  *      12 位 3.3V 参考下 1 LSB ≈ 0.806mV，单次读数末位会跳，
  *      平均 16 次后显示到 0.01V 就稳定多了（这也是报告里要分析的"读数波动"）。
  ******************************************************************************
  */

#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 初始化 ADC1 通道 5，返回 0 成功 */
uint8_t BSP_ADC_Init(void);

/* ADC 是否已上电稳定（0/1） */
uint8_t BSP_ADC_IsReady(void);

/* 单次读取，失败返回 0xFFFF */
uint16_t BSP_ADC_ReadRaw(void);

/* 连续读 count 次取平均（自动丢弃失败的样本），失败返回 0xFFFF */
uint16_t BSP_ADC_ReadAverage(uint8_t count);

/* 原始值 -> 毫伏 / 伏特： V = N * VDDA / 4095 */
uint16_t BSP_ADC_ToMilliVolt(uint16_t raw);
float    BSP_ADC_ToVolt(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_H */

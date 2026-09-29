/**
  ******************************************************************************
  * @file    dht11.h
  * @brief   DHT11 温湿度传感器（单总线，PB12）
  *
  *  为什么用"关中断 + DWT 计时"的方式读：
  *      DHT11 的位判定靠高电平持续时间（26~28us 表示 0，70us 表示 1），
  *      如果被中断打断就会读错，所以这 5ms 左右要关中断；
  *      又因为不能依赖 SysTick（它也被关了），所以用 DWT 周期计数器计时。
  ******************************************************************************
  */

#ifndef __DHT11_H
#define __DHT11_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  DHT11_OK = 0,             /* 读取成功且校验正确            */
  DHT11_ERR_PARAM,          /* 传入空指针                    */
  DHT11_ERR_NO_RESPONSE,    /* 传感器没应答（未接/线序错）   */
  DHT11_ERR_TIMEOUT,        /* 时序超时（读的过程中断了）    */
  DHT11_ERR_CHECKSUM,       /* 校验和不对                    */
  DHT11_ERR_RANGE           /* 数据超出量程，视为无效        */
} DHT11_Status;

typedef struct
{
  uint8_t  humidity;        /* %RH  整数（DHT11 无小数）      */
  uint8_t  temperature;     /* ℃    整数（DHT11 无小数）      */
  uint8_t  raw[5];          /* 原始 5 字节，便于报告分析      */
  uint32_t timestamp_ms;    /* 本次成功读取的时刻             */
} DHT11_Data;

/* 配置 PB12（开漏输出 + 上拉，读时切成输入） */
void DHT11_Init(void);

/* 读一次。周期必须 >= 1s（DATASHEET 要求），本工程 2.5s 采一次。
 * 返回 DHT11_OK 时 out 内容有效，其它情况 out 不被修改。 */
DHT11_Status DHT11_Read(DHT11_Data *out);

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H */

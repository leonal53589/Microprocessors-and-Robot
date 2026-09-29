/**
  ******************************************************************************
  * @file    dht11.c
  * @brief   DHT11 单总线时序实现
  *
  *  引脚：PB12（IO 分配表：IIC_INT / 1WIRE_DQ，板上已有 4.7K 上拉）
  *
  *  一次完整通信（《阿波罗开发指南》第 45 章 + 器件手册）：
  *      主机：拉低 >= 18ms 作为起始信号，然后释放总线
  *      从机：拉低 80us 应答，再拉高 80us
  *      数据：40 位，每位 = 50us 低电平 + 26~28us(0) / 70us(1) 高电平
  *      字节：湿度整数、湿度小数、温度整数、温度小数、校验和
  *      校验：前 4 字节之和的低 8 位 == 校验和
  *
  *  整个"应答 + 40 位数据"过程约 4~5ms，期间关中断保证时序。
  *  采集周期由 app.c 控制（2.5s），远大于器件要求的 1s。
  ******************************************************************************
  */

#include "dht11.h"
#include "pinmap.h"
#include "bsp_delay.h"

#define DHT_TIMEOUT_US      120U     /* 所有等待阶段的超时（远大于 80us） */
#define DHT_BIT_ONE_MIN_US   45U     /* 高电平 > 45us 判为 1（0 是 26~28us） */

static void dht_output_low(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin   = DHT11_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_OD;      /* 开漏：不会和从机对拉 */
  gpio.Pull  = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DHT11_PORT, &gpio);
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
}

static void dht_release(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin  = DHT11_PIN;
  gpio.Mode = GPIO_MODE_INPUT;           /* 释放总线，靠外部上拉拉高 */
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static uint8_t dht_level(void)
{
  return (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint32_t dht_us_per_tick(void)
{
  return SystemCoreClock / 1000000U;
}

/* 等待总线到达指定电平，返回 0 成功、1 超时（单位 us） */
static uint8_t dht_wait_level(uint8_t level, uint32_t timeout_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t limit = timeout_us * dht_us_per_tick();

  while (dht_level() != level)
  {
    if ((DWT->CYCCNT - start) > limit)
    {
      return 1U;
    }
  }
  return 0U;
}

/* 测当前高电平持续了多少 us（进入前总线已经是高） */
static uint32_t dht_measure_high_us(uint32_t timeout_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t limit = timeout_us * dht_us_per_tick();

  while (dht_level() != 0U)
  {
    if ((DWT->CYCCNT - start) > limit)
    {
      break;
    }
  }
  return (DWT->CYCCNT - start) / dht_us_per_tick();
}

void DHT11_Init(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();
  dht_release();               /* 空闲状态：输入 + 上拉 */
}

DHT11_Status DHT11_Read(DHT11_Data *out)
{
  uint8_t  data[5] = {0, 0, 0, 0, 0};
  uint8_t  i;
  uint8_t  bit;
  uint32_t high_us;
  DHT11_Status status = DHT11_OK;

  if (out == NULL)
  {
    return DHT11_ERR_PARAM;
  }

  /* ---- 1) 主机起始信号：拉低 20ms（>= 18ms），然后释放 ---- */
  dht_output_low();
  BSP_DelayMs(20U);
  dht_release();

  /* ---- 2) 从应答到数据结束，全程关中断保证微秒级时序 ---- */
  __disable_irq();

  if (dht_wait_level(0U, DHT_TIMEOUT_US) != 0U)      /* 等从机拉低 */
  {
    status = DHT11_ERR_NO_RESPONSE;
    goto done;
  }
  if (dht_wait_level(1U, DHT_TIMEOUT_US) != 0U)      /* 80us 应答低电平结束 */
  {
    status = DHT11_ERR_TIMEOUT;
    goto done;
  }
  if (dht_wait_level(0U, DHT_TIMEOUT_US) != 0U)      /* 80us 应答高电平结束 */
  {
    status = DHT11_ERR_TIMEOUT;
    goto done;
  }

  /* ---- 3) 40 位数据：每位先有 50us 低电平，再按高电平长短区分 0/1 ---- */
  for (i = 0U; i < 40U; i++)
  {
    if (dht_wait_level(1U, DHT_TIMEOUT_US) != 0U)    /* 跳过 50us 低电平 */
    {
      status = DHT11_ERR_TIMEOUT;
      goto done;
    }

    high_us = dht_measure_high_us(DHT_TIMEOUT_US);
    bit = (high_us >= DHT_BIT_ONE_MIN_US) ? 1U : 0U;

    data[i >> 3] = (uint8_t)((data[i >> 3] << 1) | bit);
  }

  /* 最后一位的低电平结束后总线回到空闲高电平，不必额外等待 */

done:
  __enable_irq();

  if (status != DHT11_OK)
  {
    return status;
  }

  /* ---- 4) 校验和 + 量程检查 ---- */
  {
    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    if (sum != data[4])
    {
      return DHT11_ERR_CHECKSUM;
    }
  }

  if ((data[0] > 100U) || (data[2] > 60U))
  {
    return DHT11_ERR_RANGE;
  }

  out->humidity     = data[0];
  out->temperature  = data[2];
  out->raw[0] = data[0];
  out->raw[1] = data[1];
  out->raw[2] = data[2];
  out->raw[3] = data[3];
  out->raw[4] = data[4];
  out->timestamp_ms = HAL_GetTick();

  return DHT11_OK;
}

/**
  ******************************************************************************
  * @file    pcf8574.c
  * @brief   正点原子阿波罗 V2 板载 PCF8574 驱动（软件模拟 IIC）
  *
  *  总线：SCL = PH4，SDA = PH5（开漏输出 + 板载 4.7K 上拉）
  *  速率：约 100 kHz（PCF8574 标准模式上限）
  *  延时：用 DWT 周期计数器，不依赖调试器是否连接，也不需要手工试参数
  ******************************************************************************
  */

#include "pcf8574.h"

/* ==========================================================================
 *                            软件模拟 IIC 底层
 * ========================================================================== */

#define I2C_SCL_PORT        GPIOH
#define I2C_SCL_PIN         GPIO_PIN_4
#define I2C_SDA_PORT        GPIOH
#define I2C_SDA_PIN         GPIO_PIN_5

#define I2C_HALF_DELAY_US   5U      /* 半周期 5us -> 约 100kHz */

#define SCL_HIGH()   HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_SET)
#define SCL_LOW()    HAL_GPIO_WritePin(I2C_SCL_PORT, I2C_SCL_PIN, GPIO_PIN_RESET)
#define SDA_HIGH()   HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_SET)
#define SDA_LOW()    HAL_GPIO_WritePin(I2C_SDA_PORT, I2C_SDA_PIN, GPIO_PIN_RESET)
#define SDA_READ()   HAL_GPIO_ReadPin(I2C_SDA_PORT, I2C_SDA_PIN)

/** 使能 DWT 周期计数器（纯软件使能，无需连接调试器） */
static void DWT_Enable(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/** 精确微秒级延时 */
static void I2C_Delay(void)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = (uint32_t)(I2C_HALF_DELAY_US * (SystemCoreClock / 1000000U));

  while ((uint32_t)(DWT->CYCCNT - start) < ticks)
  {
    /* busy wait */
  }
}

/** 配置 SCL/SDA 为开漏输出（依赖板载上拉电阻） */
static void SoftI2C_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOH_CLK_ENABLE();

  DWT_Enable();

  GPIO_InitStruct.Pin   = I2C_SCL_PIN | I2C_SDA_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;      /* 开漏 */
  GPIO_InitStruct.Pull  = GPIO_PULLUP;              /* 兜底，主靠板载 4.7K */
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  SCL_HIGH();
  SDA_HIGH();
}

/** 起始条件：SCL 为高时 SDA 由高变低 */
static void SoftI2C_Start(void)
{
  SDA_HIGH();
  SCL_HIGH();
  I2C_Delay();
  SDA_LOW();
  I2C_Delay();
  SCL_LOW();
  I2C_Delay();
}

/** 停止条件：SCL 为高时 SDA 由低变高 */
static void SoftI2C_Stop(void)
{
  SDA_LOW();
  SCL_HIGH();
  I2C_Delay();
  SDA_HIGH();
  I2C_Delay();
}

/** 发送一个字节，返回 0 = 收到 ACK，1 = NAK */
static uint8_t SoftI2C_WriteByte(uint8_t byte)
{
  uint8_t ack;

  for (uint8_t i = 0U; i < 8U; i++)
  {
    if ((byte & 0x80U) != 0U) { SDA_HIGH(); } else { SDA_LOW(); }
    byte = (uint8_t)(byte << 1);

    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    SCL_LOW();
    I2C_Delay();
  }

  /* 第 9 个时钟：主机释放 SDA，读从机应答 */
  SDA_HIGH();
  I2C_Delay();
  SCL_HIGH();
  I2C_Delay();

  ack = (SDA_READ() == GPIO_PIN_RESET) ? 0U : 1U;

  SCL_LOW();
  I2C_Delay();

  return ack;
}

/** 接收一个字节，ack=0 主机回 ACK（继续读），ack=1 回 NAK（结束） */
static uint8_t SoftI2C_ReadByte(uint8_t ack)
{
  uint8_t byte = 0U;

  SDA_HIGH();                       /* 释放 SDA */

  for (uint8_t i = 0U; i < 8U; i++)
  {
    byte = (uint8_t)(byte << 1);

    SCL_HIGH();
    I2C_Delay();
    if (SDA_READ() == GPIO_PIN_SET)
    {
      byte |= 0x01U;
    }
    SCL_LOW();
    I2C_Delay();
  }

  if (ack == 0U) { SDA_LOW(); } else { SDA_HIGH(); }
  I2C_Delay();
  SCL_HIGH();
  I2C_Delay();
  SCL_LOW();
  I2C_Delay();
  SDA_HIGH();

  return byte;
}

/* ==========================================================================
 *                            PCF8574 器件层
 * ========================================================================== */

/* 影子寄存器：
 * 初值 0xFF = 全部为高 = 上电默认的"输入/释放"状态。
 * 之所以不先读回再改：PCF8574 是准双向口，读回的是引脚真实电平，
 * 而 P1~P7 上挂着 AP_INT / DCMI_PWDN / USB_PWR / RS485_RE / ETH_RESET
 * 等外部信号，若把被外部拉低的位误当作 0 写回去，就会和外部电路对拉。
 * 因此只维护我们自己的位，其余永久保持高（等价于不驱动）。            */
static uint8_t pcf8574_shadow = 0xFFU;

/** 写整个字节到 PCF8574，返回 0 = 成功 */
uint8_t PCF8574_Write(uint8_t value)
{
  SoftI2C_Start();

  if (SoftI2C_WriteByte(PCF8574_ADDR_W) != 0U) { SoftI2C_Stop(); return 1U; }
  if (SoftI2C_WriteByte(value)           != 0U) { SoftI2C_Stop(); return 2U; }

  SoftI2C_Stop();

  pcf8574_shadow = value;
  return 0U;
}

/** 读回 PCF8574 引脚电平（读到的就是引脚真实状态） */
uint8_t PCF8574_Read(uint8_t *value)
{
  if (value == NULL) { return 3U; }

  SoftI2C_Start();

  if (SoftI2C_WriteByte(PCF8574_ADDR_R) != 0U) { SoftI2C_Stop(); return 1U; }

  *value = SoftI2C_ReadByte(1U);    /* 最后一字节：回 NAK */

  SoftI2C_Stop();
  return 0U;
}

/** 只改指定位，其余位保持影子值 */
uint8_t PCF8574_WriteBit(uint8_t bitmask, uint8_t level)
{
  uint8_t value = pcf8574_shadow;

  if (level != 0U) { value |=  bitmask; }
  else             { value &= (uint8_t)(~bitmask); }

  if (value == pcf8574_shadow)
  {
    return 0U;                      /* 无变化，不占用总线 */
  }

  return PCF8574_Write(value);
}

/** 读某一位 */
uint8_t PCF8574_ReadBit(uint8_t bitmask, uint8_t *level)
{
  uint8_t value;
  uint8_t ret = PCF8574_Read(&value);

  if (ret == 0U)
  {
    *level = ((value & bitmask) != 0U) ? 1U : 0U;
  }
  return ret;
}

/** 探测器件是否在线，返回 0 = 有应答 */
uint8_t PCF8574_Probe(void)
{
  uint8_t ack;

  SoftI2C_Start();
  ack = SoftI2C_WriteByte(PCF8574_ADDR_W);
  SoftI2C_Stop();

  return ack;
}

/**
  * @brief  初始化 PCF8574
  * @retval 0 = 成功；1 = 器件无应答（IIC 接线/上拉/地址问题）
  */
uint8_t PCF8574_Init(void)
{
  SoftI2C_Init();

  pcf8574_shadow = 0xFFU;

  if (PCF8574_Probe() != 0U)
  {
    return 1U;
  }

  /* 上电写全高：蜂鸣器不响，其余扩展口保持释放态 */
  return PCF8574_Write(0xFFU);
}

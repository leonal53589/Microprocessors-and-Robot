/**
  ******************************************************************************
  * @file    oled.c
  * @brief   SSD1306 (128x64) 驱动
  *
  *  三种接口模式（同一连接器 P6，由模块 BS1/BS2 决定，改 pinmap.h 的
  *  OLED_IF_MODE 即可切换）：
  *      8080 并口 : CS=PB7 RS=PB4 WR=PH8 RD=PB3 RST=PA15
  *                  D0~D7 = PC6 PC7 PC8 PC9 PC11 PD3 PB8 PB9
  *      4线 SPI   : CS=PB7 DC=PB4 SCLK=PC6(D0) SDIN=PC7(D1) RST=PA15
  *      IIC       : SCL=PC6(D0) SDA=PC7(D1) RST=PA15  地址 0x78
  ******************************************************************************
  */

#include "oled.h"
#include "oledfont.h"       /* ⚠️ 字库在此头文件里是"定义"，只能被本 .c 包含 */

/* ==========================================================================
 * 底层：引脚操作与写字节
 * ========================================================================== */

#define OLED_PIN_SET(port, pin)     ((port)->BSRR = (uint32_t)(pin))
#define OLED_PIN_CLR(port, pin)     ((port)->BSRR = (uint32_t)((uint32_t)(pin) << 16))
#define OLED_PIN_WR(port, pin, v)   ((v) ? OLED_PIN_SET(port, pin) : OLED_PIN_CLR(port, pin))

#define OLED_CS(v)      OLED_PIN_WR(OLED_CS_PORT,  OLED_CS_PIN,  (v))
#define OLED_RS(v)      OLED_PIN_WR(OLED_RS_PORT,  OLED_RS_PIN,  (v))
#define OLED_WR(v)      OLED_PIN_WR(OLED_WR_PORT,  OLED_WR_PIN,  (v))
#define OLED_RD(v)      OLED_PIN_WR(OLED_RD_PORT,  OLED_RD_PIN,  (v))
#define OLED_RST(v)     OLED_PIN_WR(OLED_RST_PORT, OLED_RST_PIN, (v))
#define OLED_SCLK(v)    OLED_PIN_WR(OLED_D0_PORT,  OLED_D0_PIN,  (v))
#define OLED_SDIN(v)    OLED_PIN_WR(OLED_D1_PORT,  OLED_D1_PIN,  (v))

static uint8_t g_gram[OLED_PAGES][OLED_WIDTH];

#if (OLED_IF_MODE == OLED_IF_8080)
/* 8080 并口数据线输出 */
static void oled_data_out(uint8_t d)
{
  OLED_PIN_WR(OLED_D0_PORT, OLED_D0_PIN, (d >> 0) & 1U);
  OLED_PIN_WR(OLED_D1_PORT, OLED_D1_PIN, (d >> 1) & 1U);
  OLED_PIN_WR(OLED_D2_PORT, OLED_D2_PIN, (d >> 2) & 1U);
  OLED_PIN_WR(OLED_D3_PORT, OLED_D3_PIN, (d >> 3) & 1U);
  OLED_PIN_WR(OLED_D4_PORT, OLED_D4_PIN, (d >> 4) & 1U);
  OLED_PIN_WR(OLED_D5_PORT, OLED_D5_PIN, (d >> 5) & 1U);
  OLED_PIN_WR(OLED_D6_PORT, OLED_D6_PIN, (d >> 6) & 1U);
  OLED_PIN_WR(OLED_D7_PORT, OLED_D7_PIN, (d >> 7) & 1U);
}
#elif (OLED_IF_MODE == OLED_IF_IIC)
/* --- IIC 模式：最小软件 IIC（SCL = D0 = PC6, SDA = D1 = PC7） --- */
static void oled_iic_delay(void)
{
  for (volatile uint32_t i = 0U; i < 30U; i++) { __NOP(); }
}

static void oled_iic_start(void)
{
  OLED_SDIN(1); OLED_SCLK(1); oled_iic_delay();
  OLED_SDIN(0); oled_iic_delay();
  OLED_SCLK(0); oled_iic_delay();
}

static void oled_iic_stop(void)
{
  OLED_SDIN(0); OLED_SCLK(1); oled_iic_delay();
  OLED_SDIN(1); oled_iic_delay();
}

static void oled_iic_wr_byte(uint8_t b)
{
  for (uint8_t i = 0U; i < 8U; i++)
  {
    OLED_SDIN((b & 0x80U) ? 1U : 0U);
    b = (uint8_t)(b << 1);
    oled_iic_delay();
    OLED_SCLK(1); oled_iic_delay();
    OLED_SCLK(0); oled_iic_delay();
  }
  OLED_SDIN(1);                    /* 释放 SDA 收 ACK（忽略） */
  oled_iic_delay();
  OLED_SCLK(1); oled_iic_delay();
  OLED_SCLK(0); oled_iic_delay();
}
#endif

/**
  * @brief  向 OLED 写一个字节
  * @param  dat     : 数据
  * @param  is_data : OLED_CMD 写命令 / OLED_DATA 写数据
  */
static void oled_wr_byte(uint8_t dat, uint8_t is_data)
{
#if (OLED_IF_MODE == OLED_IF_8080)
  OLED_RS(is_data);
  OLED_CS(0);
  OLED_WR(0);
  oled_data_out(dat);
  OLED_WR(1);                       /* WR 上升沿写入 SSD1306 */
  OLED_CS(1);
  OLED_RS(1);

#elif (OLED_IF_MODE == OLED_IF_SPI)
  OLED_CS(0);
  OLED_RS(is_data);                 /* DC：0 命令 / 1 数据 */
  for (uint8_t i = 0U; i < 8U; i++)
  {
    OLED_SDIN((dat & 0x80U) ? 1U : 0U);   /* 高位在前 */
    dat = (uint8_t)(dat << 1);
    OLED_SCLK(0);
    OLED_SCLK(1);                         /* 上升沿移入 */
  }
  OLED_CS(1);
  OLED_RS(1);

#else  /* IIC */
  oled_iic_start();
  oled_iic_wr_byte(0x78U);                /* 从机地址（SA0 = 0） */
  oled_iic_wr_byte(is_data ? 0x40U : 0x00U);  /* 控制字节 */
  oled_iic_wr_byte(dat);
  oled_iic_stop();
#endif
}

/* ==========================================================================
 * GPIO 初始化（按接口模式）
 * ========================================================================== */

static void oled_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  /* 所有模式都要用到的：RST */
  gpio.Pin = OLED_RST_PIN;
  HAL_GPIO_Init(OLED_RST_PORT, &gpio);

#if (OLED_IF_MODE == OLED_IF_8080)
  gpio.Pin = OLED_CS_PIN;  HAL_GPIO_Init(OLED_CS_PORT, &gpio);
  gpio.Pin = OLED_RS_PIN;  HAL_GPIO_Init(OLED_RS_PORT, &gpio);
  gpio.Pin = OLED_WR_PIN;  HAL_GPIO_Init(OLED_WR_PORT, &gpio);
  gpio.Pin = OLED_RD_PIN;  HAL_GPIO_Init(OLED_RD_PORT, &gpio);

  gpio.Pin = OLED_D0_PIN;  HAL_GPIO_Init(OLED_D0_PORT, &gpio);
  gpio.Pin = OLED_D1_PIN;  HAL_GPIO_Init(OLED_D1_PORT, &gpio);
  gpio.Pin = OLED_D2_PIN;  HAL_GPIO_Init(OLED_D2_PORT, &gpio);
  gpio.Pin = OLED_D3_PIN;  HAL_GPIO_Init(OLED_D3_PORT, &gpio);
  gpio.Pin = OLED_D4_PIN;  HAL_GPIO_Init(OLED_D4_PORT, &gpio);
  gpio.Pin = OLED_D5_PIN;  HAL_GPIO_Init(OLED_D5_PORT, &gpio);
  gpio.Pin = OLED_D6_PIN;  HAL_GPIO_Init(OLED_D6_PORT, &gpio);
  gpio.Pin = OLED_D7_PIN;  HAL_GPIO_Init(OLED_D7_PORT, &gpio);

  OLED_RD(1);
  OLED_WR(1);
  OLED_CS(1);
  OLED_RS(1);

#elif (OLED_IF_MODE == OLED_IF_SPI)
  gpio.Pin = OLED_CS_PIN;   HAL_GPIO_Init(OLED_CS_PORT, &gpio);
  gpio.Pin = OLED_RS_PIN;   HAL_GPIO_Init(OLED_RS_PORT, &gpio);
  gpio.Pin = OLED_D0_PIN;   HAL_GPIO_Init(OLED_D0_PORT, &gpio);  /* SCLK */
  gpio.Pin = OLED_D1_PIN;   HAL_GPIO_Init(OLED_D1_PORT, &gpio);  /* SDIN */

  OLED_CS(1);
  OLED_RS(1);
  OLED_SCLK(1);
  OLED_SDIN(1);

#else  /* IIC：开漏 */
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Pin  = OLED_D0_PIN | OLED_D1_PIN;      /* SCL, SDA */
  HAL_GPIO_Init(GPIOC, &gpio);

  OLED_SCLK(1);
  OLED_SDIN(1);
#endif
}

/* ==========================================================================
 * SSD1306 初始化
 * ========================================================================== */

static void oled_init_cmd(void)
{
  oled_wr_byte(0xAEU, OLED_CMD);   /* 关闭显示 */
  oled_wr_byte(0xD5U, OLED_CMD);   /* 设置时钟分频因子 / 震荡频率 */
  oled_wr_byte(0x80U, OLED_CMD);
  oled_wr_byte(0xA8U, OLED_CMD);   /* 设置驱动路数 */
  oled_wr_byte(0x3FU, OLED_CMD);   /* 1/64 duty */
  oled_wr_byte(0xD3U, OLED_CMD);   /* 设置显示偏移 */
  oled_wr_byte(0x00U, OLED_CMD);
  oled_wr_byte(0x40U, OLED_CMD);   /* 设置显示开始行 */
  oled_wr_byte(0x8DU, OLED_CMD);   /* 电荷泵设置 */
  oled_wr_byte(0x14U, OLED_CMD);   /* 开启电荷泵 */
  oled_wr_byte(0x20U, OLED_CMD);   /* 设置内存地址模式 */
  oled_wr_byte(0x02U, OLED_CMD);   /* 页地址模式 */
  oled_wr_byte(0xA1U, OLED_CMD);   /* 段重定义 */
  oled_wr_byte(0xC8U, OLED_CMD);   /* COM 扫描方向：重定义 */
  oled_wr_byte(0xDAU, OLED_CMD);   /* COM 硬件引脚配置 */
  oled_wr_byte(0x12U, OLED_CMD);
  oled_wr_byte(0x81U, OLED_CMD);   /* 对比度 */
  oled_wr_byte(0xEFU, OLED_CMD);
  oled_wr_byte(0xD9U, OLED_CMD);   /* 预充电周期 */
  oled_wr_byte(0xF1U, OLED_CMD);
  oled_wr_byte(0xDBU, OLED_CMD);   /* VCOMH 电压倍率 */
  oled_wr_byte(0x30U, OLED_CMD);
  oled_wr_byte(0xA4U, OLED_CMD);   /* 全局显示开启 */
  oled_wr_byte(0xA6U, OLED_CMD);   /* 正常显示（非反相） */
  oled_wr_byte(0xAFU, OLED_CMD);   /* 开启显示 */
}

void OLED_Init(void)
{
  oled_gpio_init();

  /* 硬件复位 */
  OLED_RST(0);
  HAL_Delay(20U);
  OLED_RST(1);
  HAL_Delay(20U);

  oled_init_cmd();

  OLED_Clear();
  OLED_Refresh();
}

uint8_t OLED_Probe(void)
{
#if (OLED_IF_MODE == OLED_IF_IIC)
  /* IIC 模式下可以靠 ACK 判断器件在不在 */
  oled_iic_start();
  oled_iic_wr_byte(0x78U);
  oled_iic_stop();
  return 0U;      /* 简化处理：不做严格判定 */
#else
  return 0U;
#endif
}

/* ==========================================================================
 * GRAM 与显示
 * ========================================================================== */

void OLED_Clear(void)
{
  for (uint8_t page = 0U; page < OLED_PAGES; page++)
  {
    for (uint8_t col = 0U; col < OLED_WIDTH; col++)
    {
      g_gram[page][col] = 0x00U;
    }
  }
}

void OLED_Refresh(void)
{
  for (uint8_t page = 0U; page < OLED_PAGES; page++)
  {
    oled_wr_byte((uint8_t)(0xB0U + page), OLED_CMD);   /* 页地址 */
    oled_wr_byte(0x00U, OLED_CMD);                     /* 列低地址 */
    oled_wr_byte(0x10U, OLED_CMD);                     /* 列高地址 */
    for (uint8_t col = 0U; col < OLED_WIDTH; col++)
    {
      oled_wr_byte(g_gram[page][col], OLED_DATA);
    }
  }
}

static void gram_pixel(uint8_t x, uint8_t y, uint8_t on)
{
  if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
  {
    return;
  }
  if (on != 0U)
  {
    g_gram[y >> 3][x] |= (uint8_t)(1U << (y & 7U));
  }
  else
  {
    g_gram[y >> 3][x] &= (uint8_t)~(1U << (y & 7U));
  }
}

/* ASCII 8x16 */
static void oled_draw_ascii(uint8_t x, uint8_t y, uint8_t ch)
{
  const uint8_t *p;

  if ((ch < OLED_ASCII_FIRST) || (ch > OLED_ASCII_LAST))
  {
    ch = (uint8_t)' ';
  }
  p = &oled_asc2_1608[(uint16_t)(ch - OLED_ASCII_FIRST) * 16U];

  for (uint8_t col = 0U; col < 8U; col++)
  {
    for (uint8_t row = 0U; row < 16U; row++)
    {
      uint8_t byte = p[((row >> 3) * 8U) + col];
      gram_pixel((uint8_t)(x + col), (uint8_t)(y + row),
                 (uint8_t)((byte >> (row & 7U)) & 1U));
    }
  }
}

/* 中文 16x16（按 UTF-8 三字节匹配索引表） */
static const uint8_t *oled_cn_lookup(const char *s)
{
  for (uint16_t i = 0U; i < OLED_CN_COUNT; i++)
  {
    const char *t = oled_cn_index[i].utf8;
    if ((t[0] == s[0]) && (t[1] == s[1]) && (t[2] == s[2]))
    {
      return &oled_cn_1616[oled_cn_index[i].offset];
    }
  }
  return NULL;
}

static void oled_draw_cn(uint8_t x, uint8_t y, const uint8_t *p)
{
  for (uint8_t col = 0U; col < 16U; col++)
  {
    for (uint8_t row = 0U; row < 16U; row++)
    {
      uint8_t byte = p[((row >> 3) * 16U) + col];
      gram_pixel((uint8_t)(x + col), (uint8_t)(y + row),
                 (uint8_t)((byte >> (row & 7U)) & 1U));
    }
  }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
  while (*str != '\0')
  {
    if ((uint8_t)*str < 0x80U)
    {
      oled_draw_ascii(x, y, (uint8_t)*str);
      x = (uint8_t)(x + 8U);
      str++;
    }
    else
    {
      const uint8_t *p = oled_cn_lookup(str);
      if (p != NULL)
      {
        oled_draw_cn(x, y, p);
      }
      else
      {
        oled_draw_ascii(x, y, (uint8_t)'?');    /* 字库里没有的字 */
      }
      x = (uint8_t)(x + 16U);
      str += 3;                                  /* UTF-8 汉字 3 字节 */
    }

    if (x >= OLED_WIDTH)
    {
      break;                                     /* 越界直接截断 */
    }
  }
}

void OLED_ShowInt(uint8_t x, uint8_t y, int32_t num)
{
  char    buf[12];
  uint8_t i = 0U;
  uint8_t n = 0U;
  uint32_t v;
  uint8_t  neg = 0U;

  if (num < 0)
  {
    neg = 1U;
    v   = (uint32_t)(-(num + 1)) + 1U;           /* 避免 INT32_MIN 溢出 */
  }
  else
  {
    v = (uint32_t)num;
  }

  if (v == 0U)
  {
    buf[i++] = '0';
  }
  else
  {
    while ((v > 0U) && (i < 10U))
    {
      buf[i++] = (char)('0' + (v % 10U));
      v /= 10U;
    }
  }
  if (neg != 0U)
  {
    buf[i++] = '-';
  }

  /* 反转成正常顺序输出 */
  while (i > 0U)
  {
    i--;
    oled_draw_ascii((uint8_t)(x + n * 8U), y, (uint8_t)buf[i]);
    n++;
  }
}

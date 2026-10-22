/**
  ******************************************************************************
  * @file    bsp_lcd.c
  * @brief   7 寸 RGB 屏（面板 1024x600，界面 800x480 居中）+ 绘图 / 文本 / 图像搬运
  *
  *  硬件要点：
  *    1) 帧缓冲在外部 SDRAM（bsp_sdram.c 必须先初始化），LTDC 通过 FMC 读它；
  *    2) LTDC 像素时钟由 PLLSAI 提供：HSE/PLLM=1MHz -> x360 = 360MHz VCO
  *       -> /PLLSAIR(2) = 180MHz -> /PLLSAIDivR(4) = 45MHz（1024x600 面板）；
  *    3) 引脚用 AF14，面板时序参数集中在 pinmap.h（换屏只改那几个宏）；
  *    4) 背光 PB5 在 LTDC 跑起来之后再打开，避免上电白屏闪一下。
  *
  *  本工程的约定：外设时钟使能与 GPIO 复用都写在各自的 BSP 文件里，
  *  hal_msp.c 只保留最基础的 HAL_MspInit()，避免"配置散落两处"导致漏改。
  ******************************************************************************
  */

#include "bsp_lcd.h"
#include "pinmap.h"

#define LCD_FB  ((uint16_t *)LCD_FB_ADDR)

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
} BspPin;

/* LTDC 的 20 根信号线（IO 分配表"RGBLCD 接口"列），全部 AF14 */
static const BspPin s_ltdc_pins[] =
{
  { LCD_R3_PORT,    LCD_R3_PIN    }, { LCD_R4_PORT,    LCD_R4_PIN    },
  { LCD_R5_PORT,    LCD_R5_PIN    }, { LCD_R6_PORT,    LCD_R6_PIN    },
  { LCD_R7_PORT,    LCD_R7_PIN    },
  { LCD_G2_PORT,    LCD_G2_PIN    }, { LCD_G3_PORT,    LCD_G3_PIN    },
  { LCD_G4_PORT,    LCD_G4_PIN    }, { LCD_G5_PORT,    LCD_G5_PIN    },
  { LCD_G6_PORT,    LCD_G6_PIN    }, { LCD_G7_PORT,    LCD_G7_PIN    },
  { LCD_B3_PORT,    LCD_B3_PIN    }, { LCD_B4_PORT,    LCD_B4_PIN    },
  { LCD_B5_PORT,    LCD_B5_PIN    }, { LCD_B6_PORT,    LCD_B6_PIN    },
  { LCD_B7_PORT,    LCD_B7_PIN    },
  { LCD_CLK_PORT,   LCD_CLK_PIN   }, { LCD_DE_PORT,    LCD_DE_PIN    },
  { LCD_VSYNC_PORT, LCD_VSYNC_PIN }, { LCD_HSYNC_PORT, LCD_HSYNC_PIN }
};

static LTDC_HandleTypeDef s_hltdc;

/* -------------------------------------------------------------------------- */
/* 初始化                                                                      */
/* -------------------------------------------------------------------------- */

static void lcd_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();

  /* LTDC 的 20 根信号线 */
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;   /* 45MHz 像素时钟，必须高档 */
  gpio.Alternate = LCD_AF;

  for (uint32_t i = 0U; i < (sizeof(s_ltdc_pins) / sizeof(s_ltdc_pins[0])); i++)
  {
    gpio.Pin = s_ltdc_pins[i].pin;
    HAL_GPIO_Init(s_ltdc_pins[i].port, &gpio);
  }

  /* 背光：普通推挽输出，先关掉 */
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin   = LCD_BL_PIN;
  HAL_GPIO_Init(LCD_BL_PORT, &gpio);
  BSP_LCD_Backlight(0U);
}

static void lcd_clock_init(void)
{
  RCC_PeriphCLKInitTypeDef periph = {0};

  /* LTDC 时钟源 = PLLSAIR，再用 PLLSAIDivR 分频 */
  periph.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  periph.PLLSAI.PLLSAIN       = LCD_PLLSAI_N;
  periph.PLLSAI.PLLSAIR       = LCD_PLLSAI_R;
  periph.PLLSAI.PLLSAIQ       = LCD_PLLSAI_Q;
  periph.PLLSAIDivR           = LCD_PLLSAIDIVR;

  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
  {
    Error_Handler();
  }
}

uint8_t BSP_LCD_Init(void)
{
  LTDC_LayerCfgTypeDef layer = {0};

  lcd_gpio_init();
  lcd_clock_init();

  /* SDRAM 上电后内容是随机的，先把整屏刷成底色，再让 LTDC 开始扫描 */
  BSP_LCD_Clear(LCD_NAVY);

  __HAL_RCC_LTDC_CLK_ENABLE();

  s_hltdc.Instance = LTDC;
  /* 面板极性：行/场同步低有效、DE 高有效、像素时钟上升沿采样
   * （与所用 1024x600 面板手册一致；换屏若花屏先查这四位） */
  s_hltdc.Init.HSPolarity       = LTDC_HSPOLARITY_AL;
  s_hltdc.Init.VSPolarity       = LTDC_VSPOLARITY_AL;
  s_hltdc.Init.DEPolarity       = LTDC_DEPOLARITY_AL;
  s_hltdc.Init.PCPolarity       = LTDC_PCPOLARITY_IPC;
  /* 下面这些寄存器值都是"累计值 - 1"，见 LTDC 章节的时序图 */
  s_hltdc.Init.HorizontalSync     = LCD_HSW - 1U;
  s_hltdc.Init.VerticalSync       = LCD_VSW - 1U;
  s_hltdc.Init.AccumulatedHBP     = LCD_HSW + LCD_HBP - 1U;
  s_hltdc.Init.AccumulatedVBP     = LCD_VSW + LCD_VBP - 1U;
  s_hltdc.Init.AccumulatedActiveW = LCD_HSW + LCD_HBP + LCD_PANEL_W - 1U;
  s_hltdc.Init.AccumulatedActiveH = LCD_VSW + LCD_VBP + LCD_PANEL_H - 1U;
  s_hltdc.Init.TotalWidth         = LCD_HSW + LCD_HBP + LCD_PANEL_W + LCD_HFP - 1U;
  s_hltdc.Init.TotalHeigh         = LCD_VSW + LCD_VBP + LCD_PANEL_H + LCD_VFP - 1U;
  s_hltdc.Init.Backcolor.Red      = 0U;
  s_hltdc.Init.Backcolor.Green    = 0U;
  s_hltdc.Init.Backcolor.Blue     = 0U;

  if (HAL_LTDC_Init(&s_hltdc) != HAL_OK)
  {
    return 1U;
  }

  /* 第 1 层铺满整屏，RGB565，帧缓冲指向 SDRAM */
  /* 面板原生 1024x600，界面按 800x480 绘制：窗口居中放置，四周留黑边 */
  layer.WindowX0       = LCD_WIN_X0;
  layer.WindowX1       = LCD_WIN_X1;
  layer.WindowY0       = LCD_WIN_Y0;
  layer.WindowY1       = LCD_WIN_Y1;
  layer.PixelFormat    = LTDC_PIXEL_FORMAT_RGB565;
  layer.Alpha          = 255U;
  layer.Alpha0         = 0U;
  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  layer.FBStartAdress  = LCD_FB_ADDR;
  layer.ImageWidth     = LCD_WIDTH;
  layer.ImageHeight    = LCD_HEIGHT;
  layer.Backcolor.Red   = 0U;
  layer.Backcolor.Green = 0U;
  layer.Backcolor.Blue  = 0U;

  if (HAL_LTDC_ConfigLayer(&s_hltdc, &layer, 0U) != HAL_OK)
  {
    return 2U;
  }

  BSP_LCD_Clear(LCD_NAVY);
  BSP_LCD_Backlight(1U);

  return 0U;
}

void BSP_LCD_Backlight(uint8_t on)
{
  HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, (on != 0U) ? LCD_BL_ON : LCD_BL_OFF);
}

uint16_t *BSP_LCD_GetFrameBuffer(void)
{
  return LCD_FB;
}

/* -------------------------------------------------------------------------- */
/* 基本绘图                                                                    */
/* -------------------------------------------------------------------------- */

void BSP_LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if ((x < LCD_WIDTH) && (y < LCD_HEIGHT))
  {
    LCD_FB[(uint32_t)y * LCD_WIDTH + x] = color;
  }
}

void BSP_LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  uint16_t x0 = x;
  uint16_t y0 = y;
  uint16_t x1;
  uint16_t y1;

  if ((w == 0U) || (h == 0U))
  {
    return;
  }

  /* 裁剪到屏幕内：界面代码写错坐标也不会写坏 SDRAM */
  x1 = (uint16_t)((uint32_t)x + w - 1U);
  y1 = (uint16_t)((uint32_t)y + h - 1U);
  if ((x0 >= LCD_WIDTH) || (y0 >= LCD_HEIGHT))
  {
    return;
  }
  if (x1 >= LCD_WIDTH)
  {
    x1 = LCD_WIDTH - 1U;
  }
  if (y1 >= LCD_HEIGHT)
  {
    y1 = LCD_HEIGHT - 1U;
  }

  for (uint16_t row = y0; row <= y1; row++)
  {
    uint16_t *p = &LCD_FB[(uint32_t)row * LCD_WIDTH + x0];
    for (uint16_t col = x0; col <= x1; col++)
    {
      *p++ = color;
    }
  }
}

void BSP_LCD_DrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
  BSP_LCD_FillRect(x, y, w, 1U, color);
}

void BSP_LCD_DrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
  BSP_LCD_FillRect(x, y, 1U, h, color);
}

void BSP_LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if ((w == 0U) || (h == 0U))
  {
    return;
  }
  BSP_LCD_DrawHLine(x, y, w, color);
  BSP_LCD_DrawHLine(x, (uint16_t)(y + h - 1U), w, color);
  BSP_LCD_DrawVLine(x, y, h, color);
  BSP_LCD_DrawVLine((uint16_t)(x + w - 1U), y, h, color);
}

void BSP_LCD_Clear(uint16_t color)
{
  BSP_LCD_FillRect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

/* -------------------------------------------------------------------------- */
/* 文本                                                                        */
/* -------------------------------------------------------------------------- */

/* 画一个字形：位图每行 ceil(宽/8) 字节、MSB 在左；背景一并填掉，避免残影 */
static void lcd_draw_glyph(uint16_t x, uint16_t y, const uint8_t *bitmap,
                           uint16_t gw, uint16_t gh, uint16_t fg, uint16_t bg)
{
  uint16_t bytes_per_row = (uint16_t)((gw + 7U) / 8U);

  for (uint16_t row = 0U; row < gh; row++)
  {
    const uint8_t *line = &bitmap[(uint32_t)row * bytes_per_row];
    uint16_t py = (uint16_t)(y + row);

    if (py >= LCD_HEIGHT)
    {
      break;
    }

    for (uint16_t col = 0U; col < gw; col++)
    {
      uint8_t byte = line[col >> 3];
      uint8_t on = (uint8_t)((byte >> (7U - (col & 7U))) & 0x01U);
      uint16_t px = (uint16_t)(x + col);

      if (px < LCD_WIDTH)
      {
        LCD_FB[(uint32_t)py * LCD_WIDTH + px] = (on != 0U) ? fg : bg;
      }
    }
  }
}

uint16_t BSP_LCD_DrawString(uint16_t x, uint16_t y, const char *text,
                            uint16_t fg, uint16_t bg, LcdFontSize size)
{
  uint16_t cx = x;

  if (text == NULL)
  {
    return 0U;
  }

  while (*text != '\0')
  {
    uint8_t len = LCD_FontUtf8Len((uint8_t)*text);

    if (len == 1U)
    {
      uint16_t w = LCD_FontAsciiWidth(size);
      uint16_t h = LCD_FontHeight(size);
      const uint8_t *g = LCD_FontFindAscii((uint8_t)*text, size);

      if (g != NULL)
      {
        lcd_draw_glyph(cx, y, g, w, h, fg, bg);
      }
      else
      {
        BSP_LCD_FillRect(cx, y, w, h, bg);   /* 字库缺字：留白而不是残影 */
      }
      cx = (uint16_t)(cx + w);
    }
    else
    {
      uint16_t w = LCD_FontCnWidth(size);
      uint16_t h = LCD_FontHeight(size);
      const uint8_t *g = LCD_FontFindCn(text, size);

      if (g != NULL)
      {
        lcd_draw_glyph(cx, y, g, w, h, fg, bg);
      }
      else
      {
        BSP_LCD_FillRect(cx, y, w, h, bg);
      }
      cx = (uint16_t)(cx + w);
    }

    text += len;
  }

  return (uint16_t)(cx - x);
}

uint16_t BSP_LCD_DrawInt(uint16_t x, uint16_t y, int32_t value,
                         uint16_t fg, uint16_t bg, LcdFontSize size)
{
  char     digits[13];
  char     out[15];
  uint8_t  n = 0U;
  uint8_t  m = 0U;
  uint32_t u;

  if (value < 0)
  {
    out[m++] = '-';
    u = (uint32_t)(-value);
  }
  else
  {
    u = (uint32_t)value;
  }

  do
  {
    digits[n++] = (char)('0' + (u % 10U));
    u /= 10U;
  } while ((u != 0U) && (n < 12U));

  while (n > 0U)
  {
    out[m++] = digits[--n];
  }
  out[m] = '\0';

  return BSP_LCD_DrawString(x, y, out, fg, bg, size);
}

uint16_t BSP_LCD_DrawFixed2(uint16_t x, uint16_t y, float value,
                            uint16_t fg, uint16_t bg, LcdFontSize size)
{
  char     digits[13];
  char     out[17];
  uint8_t  n = 0U;
  uint8_t  m = 0U;
  int32_t  scaled;
  int32_t  ip;
  int32_t  fp;

  if (value < 0.0f)
  {
    value = -value;
    scaled = (int32_t)(value * 100.0f + 0.5f);
    if (scaled != 0)
    {
      out[m++] = '-';
    }
  }
  else
  {
    scaled = (int32_t)(value * 100.0f + 0.5f);
  }

  ip = scaled / 100;
  fp = scaled % 100;

  do
  {
    digits[n++] = (char)('0' + (ip % 10));
    ip /= 10;
  } while ((ip != 0) && (n < 12U));

  while (n > 0U)
  {
    out[m++] = digits[--n];
  }

  out[m++] = '.';
  out[m++] = (char)('0' + (fp / 10));
  out[m++] = (char)('0' + (fp % 10));
  out[m] = '\0';

  return BSP_LCD_DrawString(x, y, out, fg, bg, size);
}

uint16_t BSP_LCD_DrawStringCentered(uint16_t y, const char *text,
                                    uint16_t fg, uint16_t bg, LcdFontSize size)
{
  uint16_t width = LCD_FontTextWidth(text, size);
  uint16_t x = (uint16_t)((LCD_WIDTH - width) / 2U);

  BSP_LCD_DrawString(x, y, text, fg, bg, size);

  return x;
}

/* -------------------------------------------------------------------------- */
/* 图像搬运                                                                    */
/* -------------------------------------------------------------------------- */

void BSP_LCD_BlitRGB565(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *src)
{
  if (src == NULL)
  {
    return;
  }

  for (uint16_t row = 0U; row < h; row++)
  {
    uint16_t py = (uint16_t)(y + row);

    if (py >= LCD_HEIGHT)
    {
      break;
    }

    for (uint16_t col = 0U; col < w; col++)
    {
      uint16_t px = (uint16_t)(x + col);

      if (px >= LCD_WIDTH)
      {
        break;
      }
      LCD_FB[(uint32_t)py * LCD_WIDTH + px] = src[(uint32_t)row * w + col];
    }
  }
}

void BSP_LCD_BlitRGB565x2(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *src)
{
  if (src == NULL)
  {
    return;
  }

  /* 目的区域 = 2w x 2h，逐行做"水平复制一份 + 整行写两遍" */
  for (uint16_t row = 0U; row < h; row++)
  {
    uint16_t py0 = (uint16_t)(y + row * 2U);
    uint16_t py1 = (uint16_t)(py0 + 1U);

    if (py0 >= LCD_HEIGHT)
    {
      break;
    }

    uint16_t *d0 = &LCD_FB[(uint32_t)py0 * LCD_WIDTH];
    uint16_t *d1 = (py1 < LCD_HEIGHT) ? &LCD_FB[(uint32_t)py1 * LCD_WIDTH] : NULL;
    const uint16_t *s = &src[(uint32_t)row * w];

    for (uint16_t col = 0U; col < w; col++)
    {
      uint16_t px0 = (uint16_t)(x + col * 2U);
      uint16_t px1 = (uint16_t)(px0 + 1U);
      uint16_t c = s[col];

      if (px0 < LCD_WIDTH)
      {
        d0[px0] = c;
        if (d1 != NULL)
        {
          d1[px0] = c;
        }
      }
      if (px1 < LCD_WIDTH)
      {
        d0[px1] = c;
        if (d1 != NULL)
        {
          d1[px1] = c;
        }
      }
    }
  }
}

/**
  ******************************************************************************
  * @file    display.c
  * @brief   统一显示接口实现（上层只调这里，不直接碰 OLED 写命令/写数据）
  ******************************************************************************
  */

#include "display.h"
#include "oled.h"

void Disp_Init(void)
{
  OLED_Init();
}

void Disp_Clear(void)
{
  OLED_Clear();
  OLED_Refresh();
}

void Disp_String(uint8_t x, uint8_t y, const char *str)
{
  OLED_ShowString(x, y, str);
}

void Disp_Int(uint8_t x, uint8_t y, int32_t num)
{
  OLED_ShowInt(x, y, num);
}

void Disp_Refresh(void)
{
  OLED_Refresh();
}

void Disp_Line(uint8_t line, const char *str)
{
  if (line >= DISP_LINES)
  {
    return;
  }
  OLED_ShowString(0U, (uint8_t)(line * DISP_LINE_H), str);
}

void Disp_LineInt(uint8_t line, const char *label, int32_t num)
{
  uint8_t x = 0U;

  if (line >= DISP_LINES)
  {
    return;
  }

  OLED_ShowString(0U, (uint8_t)(line * DISP_LINE_H), label);

  /* 计算标签占用的像素宽度：ASCII 8，汉字 16 */
  while (*label != '\0')
  {
    if ((uint8_t)*label < 0x80U)
    {
      x = (uint8_t)(x + 8U);
      label++;
    }
    else
    {
      x = (uint8_t)(x + 16U);
      label += 3;
    }
  }

  OLED_ShowInt(x, (uint8_t)(line * DISP_LINE_H), num);
}

static uint8_t disp_digits(int32_t v)
{
  uint8_t  d = 0U;
  uint32_t a;

  if (v < 0)
  {
    d = 1U;
    a = (uint32_t)(-(v + 1)) + 1U;
  }
  else
  {
    a = (uint32_t)v;
  }

  do
  {
    d++;
    a /= 10U;
  } while (a > 0U);

  return d;
}

void Disp_Fixed1(uint8_t x, uint8_t y, int32_t value_x10)
{
  int32_t iv = value_x10 / 10;
  int32_t fr = value_x10 % 10;
  uint8_t neg = 0U;

  /* 注意：C 的除法是向零截断，-5/10 = 0、-5%10 = -5，
   * 如果直接对 iv 取绝对值，-0.5 会把负号丢掉，所以这里单独处理符号 */
  if (value_x10 < 0)
  {
    neg = 1U;
    iv  = -iv;
    fr  = -fr;
  }

  if (neg != 0U)
  {
    OLED_ShowString(x, y, "-");
    x = (uint8_t)(x + 8U);
  }

  OLED_ShowInt(x, y, iv);
  x = (uint8_t)(x + disp_digits(iv) * 8U);
  OLED_ShowString(x, y, ".");
  OLED_ShowInt((uint8_t)(x + 8U), y, fr);
}

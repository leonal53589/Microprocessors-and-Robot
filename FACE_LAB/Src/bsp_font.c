/**
  ******************************************************************************
  * @file    bsp_font.c
  * @brief   点阵字库查询：ASCII 单字节、汉字按 UTF-8 字节串匹配
  *
  *  点阵打包格式（由 tools/gen_font.py 生成，必须与这里严格对应）：
  *      按行打包、每字节 MSB 在左、每行 ceil(宽/8) 字节
  *      取第 row 行第 col 列的像素：
  *          byte = bitmap[row * bytes_per_row + (col >> 3)]
  *          on   = (byte >> (7 - (col & 7))) & 1
  ******************************************************************************
  */

#include "bsp_font.h"
#include "lcdfont.h"

uint16_t LCD_FontAsciiWidth(LcdFontSize size)
{
  return (size == LCD_FONT_LARGE) ? 16U : 12U;
}

uint16_t LCD_FontCnWidth(LcdFontSize size)
{
  return (size == LCD_FONT_LARGE) ? 32U : 24U;
}

uint16_t LCD_FontHeight(LcdFontSize size)
{
  return (size == LCD_FONT_LARGE) ? 32U : 24U;
}

uint8_t LCD_FontUtf8Len(uint8_t lead)
{
  if (lead < 0x80U)
  {
    return 1U;
  }
  if ((lead & 0xE0U) == 0xC0U)
  {
    return 2U;
  }
  if ((lead & 0xF0U) == 0xE0U)
  {
    return 3U;
  }
  if ((lead & 0xF8U) == 0xF0U)
  {
    return 4U;
  }
  return 1U;                /* 非法首字节，当成单字节跳过，避免死循环 */
}

const uint8_t *LCD_FontFindAscii(uint8_t ch, LcdFontSize size)
{
  if ((ch < LCD_FONT_ASCII_FIRST) || (ch > LCD_FONT_ASCII_LAST))
  {
    return NULL;
  }

  if (size == LCD_FONT_LARGE)
  {
    return &lcd_asc_1632[(uint32_t)(ch - LCD_FONT_ASCII_FIRST) * 64U];
  }
  return &lcd_asc_1224[(uint32_t)(ch - LCD_FONT_ASCII_FIRST) * 48U];
}

const uint8_t *LCD_FontFindCn(const char *utf8, LcdFontSize size)
{
  const LcdCnItem *table;
  const uint8_t   *data;
  uint16_t         count;
  uint8_t          len;

  if (utf8 == NULL)
  {
    return NULL;
  }

  len = LCD_FontUtf8Len((uint8_t)utf8[0]);
  if (len == 1U)
  {
    return NULL;            /* ASCII 走 LCD_FontFindAscii */
  }

  if (size == LCD_FONT_LARGE)
  {
    table = lcd_cn_index_large;
    count = LCD_CN_COUNT_LARGE;
    data  = lcd_cn_3232;
  }
  else
  {
    table = lcd_cn_index_small;
    count = LCD_CN_COUNT_SMALL;
    data  = lcd_cn_2424;
  }

  /* 汉字数量在 200 个量级，线性查找足够快（界面刷新只有几处文字） */
  for (uint16_t i = 0U; i < count; i++)
  {
    uint8_t k = 0U;
    while ((k < len) && (table[i].utf8[k] != '\0') && (table[i].utf8[k] == utf8[k]))
    {
      k++;
    }
    if ((k == len) && (table[i].utf8[k] == '\0'))
    {
      /* 索引里存的是字节偏移（生成脚本里用 len(data) 记录），直接取即可 */
      return &data[table[i].offset];
    }
  }

  return NULL;
}

uint16_t LCD_FontTextWidth(const char *utf8, LcdFontSize size)
{
  uint16_t width = 0U;

  if (utf8 == NULL)
  {
    return 0U;
  }

  while (*utf8 != '\0')
  {
    uint8_t len = LCD_FontUtf8Len((uint8_t)*utf8);

    if (len == 1U)
    {
      if ((*utf8 >= ' ') && (*utf8 <= '~'))
      {
        width = (uint16_t)(width + LCD_FontAsciiWidth(size));
      }
    }
    else
    {
      width = (uint16_t)(width + LCD_FontCnWidth(size));
    }

    utf8 += len;
  }

  return width;
}

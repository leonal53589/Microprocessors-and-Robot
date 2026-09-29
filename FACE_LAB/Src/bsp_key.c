/**
  ******************************************************************************
  * @file    bsp_key.c
  * @brief   按键消抖与事件提取
  *
  *  实现要点：
  *    1) 每个按键独立记录 last_raw / stable / change_ms；
  *    2) 原始电平变化时刷新时间戳，只有连续稳定 20ms 才更新 stable；
  *    3) 只在 stable 由"未按下"变为"按下"时返回一次事件 —— 长按不会重复触发。
  ******************************************************************************
  */

#include "bsp_key.h"
#include "pinmap.h"

#define KEY_DEBOUNCE_MS   20U

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  uint8_t       active_low;   /* 1 = 低电平表示按下 */
  uint8_t       last_raw;     /* 上一次的原始电平（0/1） */
  uint8_t       stable;       /* 消抖后的电平（0/1） */
  uint32_t      change_ms;    /* 原始电平最后一次变化的时刻 */
  KeyEvent      event;
} KeyState;

static KeyState s_keys[4];

static uint8_t key_pressed(const KeyState *k)
{
  uint8_t level = (HAL_GPIO_ReadPin(k->port, k->pin) == GPIO_PIN_SET) ? 1U : 0U;
  return (k->active_low != 0U) ? (uint8_t)(level == 0U) : (uint8_t)(level != 0U);
}

void BSP_Key_Init(void)
{
  uint32_t now = HAL_GetTick();

  s_keys[0].port = KEY0_PORT;  s_keys[0].pin = KEY0_PIN;  s_keys[0].active_low = 1U; s_keys[0].event = KEY_EVENT_K0;
  s_keys[1].port = KEY1_PORT;  s_keys[1].pin = KEY1_PIN;  s_keys[1].active_low = 1U; s_keys[1].event = KEY_EVENT_K1;
  s_keys[2].port = KEY2_PORT;  s_keys[2].pin = KEY2_PIN;  s_keys[2].active_low = 1U; s_keys[2].event = KEY_EVENT_K2;
  s_keys[3].port = KEYUP_PORT; s_keys[3].pin = KEYUP_PIN; s_keys[3].active_low = 0U; s_keys[3].event = KEY_EVENT_UP;

  for (uint32_t i = 0U; i < 4U; i++)
  {
    s_keys[i].last_raw  = key_pressed(&s_keys[i]);
    s_keys[i].stable    = s_keys[i].last_raw;
    s_keys[i].change_ms = now;
  }
}

KeyEvent BSP_Key_Scan(uint32_t now_ms)
{
  KeyEvent result = KEY_EVENT_NONE;

  for (uint32_t i = 0U; i < 4U; i++)
  {
    KeyState *k = &s_keys[i];
    uint8_t raw = key_pressed(k);

    if (raw != k->last_raw)
    {
      k->last_raw  = raw;
      k->change_ms = now_ms;
      continue;                       /* 还在抖动，等它稳定 */
    }

    if ((raw != k->stable) && ((now_ms - k->change_ms) >= KEY_DEBOUNCE_MS))
    {
      k->stable = raw;
      if (raw != 0U)
      {
        result = k->event;            /* 只在按下边沿返回一次 */
      }
    }
  }

  return result;
}

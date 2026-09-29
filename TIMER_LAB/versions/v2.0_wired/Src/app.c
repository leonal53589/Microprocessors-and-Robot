/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用层：菜单 + 五个定时器实验
  *
  *   实验一 OLED 基础显示与统一显示接口        （display.c / oled.c）
  *   实验二 随机反应速度测试器                  （TIM6 1ms 基准）
  *   实验三 三色流水呼吸灯                      （TIM3 三路 PWM 1kHz）
  *   实验四 频率与占空比自测仪                  （TIM4 输出 + TIM3 输入捕获）
  *   实验五 N 次闪光与硬件急停                  （TIM1 RCR+OPM+MOE+BKIN）
  *
  *   设计原则：
  *     * 所有定时器中断里只做"计数 / 记录时间戳 / 置标志"，不刷新显示、不打印
  *     * 显示统一走 display.c 的三个基础接口
  *     * 按键 20ms 时间戳消抖，只在按下边沿产生事件，长按不重复触发
  ******************************************************************************
  */

#include "app.h"
#include "pinmap.h"
#include "display.h"
#include "bsp_tim.h"
#include "pcf8574.h"
#include "config.h"

/* ==========================================================================
 * 按键
 * ========================================================================== */

#define KEY_NUM     4U
#define KE_KEY0     0U
#define KE_KEY1     1U
#define KE_KEY2     2U
#define KE_UP       3U

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  GPIO_PinState active;
} KeyPin;

static const KeyPin s_keys[KEY_NUM] =
{
  {KEY0_PORT,  KEY0_PIN,  GPIO_PIN_RESET},   /* PH3  低电平有效 */
  {KEY1_PORT,  KEY1_PIN,  GPIO_PIN_RESET},   /* PH2  低电平有效 */
  {KEY2_PORT,  KEY2_PIN,  GPIO_PIN_RESET},   /* PC13 低电平有效 */
  {KEYUP_PORT, KEYUP_PIN, GPIO_PIN_SET}      /* PA0  高电平有效 */
};

static GPIO_PinState s_key_raw[KEY_NUM];
static GPIO_PinState s_key_stable[KEY_NUM];
static uint32_t      s_key_changed[KEY_NUM];

static void keys_init(void)
{
  uint32_t now = HAL_GetTick();

  for (uint8_t i = 0U; i < KEY_NUM; i++)
  {
    s_key_raw[i]    = HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin);
    s_key_stable[i] = s_key_raw[i];
    s_key_changed[i] = now;
  }
}

/* 返回 0..3 = 该键刚按下；-1 = 无事件 */
static int8_t key_scan(void)
{
  uint32_t now = HAL_GetTick();

  for (uint8_t i = 0U; i < KEY_NUM; i++)
  {
    GPIO_PinState raw = HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin);

    if (raw != s_key_raw[i])
    {
      s_key_raw[i]     = raw;
      s_key_changed[i] = now;
    }

    if (((uint32_t)(now - s_key_changed[i]) >= 20U) && (raw != s_key_stable[i]))
    {
      s_key_stable[i] = raw;
      if (raw == s_keys[i].active)
      {
        return (int8_t)i;
      }
    }
  }

  return -1;
}

/* ==========================================================================
 * 蜂鸣器（板载 PCF8574 P0，低电平响）与提示 LED
 * ========================================================================== */

static uint32_t s_beep_off = 0U;

static void beep(uint32_t ms)
{
  (void)PCF8574_WriteBit(PCF8574_BEEP_BIT, PCF8574_BEEP_ON);
  s_beep_off = HAL_GetTick() + ms;
}

static void beep_task(void)
{
  if ((s_beep_off != 0U) && ((int32_t)(HAL_GetTick() - s_beep_off) >= 0))
  {
    (void)PCF8574_WriteBit(PCF8574_BEEP_BIT, PCF8574_BEEP_OFF);
    s_beep_off = 0U;
  }
}

/* ---- 实验二的提示灯 ----
 * 主用【板载 DS0（PB1，红色）】—— 不用接任何线就能看到；
 * 同时并联驱动 PE10，方便以后外接一只更醒目的 LED。
 * 都是低电平点亮。
 * 注意：PB1 在实验三里是 TIM3_CH4，所以这里要重新配置成普通推挽输出。 */
static void react_led_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = REACT_LED_PIN;
  HAL_GPIO_Init(REACT_LED_PORT, &gpio);

  gpio.Pin = REACT_LED_EXT_PIN;
  HAL_GPIO_Init(REACT_LED_EXT_PORT, &gpio);

  HAL_GPIO_WritePin(REACT_LED_PORT,     REACT_LED_PIN,     GPIO_PIN_SET);   /* 灭 */
  HAL_GPIO_WritePin(REACT_LED_EXT_PORT, REACT_LED_EXT_PIN, GPIO_PIN_SET);
}

#define REACT_LED(on)  do {                                                    \
    HAL_GPIO_WritePin(REACT_LED_PORT, REACT_LED_PIN,                           \
                      (on) ? GPIO_PIN_RESET : GPIO_PIN_SET);                   \
    HAL_GPIO_WritePin(REACT_LED_EXT_PORT, REACT_LED_EXT_PIN,                   \
                      (on) ? GPIO_PIN_RESET : GPIO_PIN_SET);                   \
  } while (0)

/* ==========================================================================
 * 简易随机数（线性同余）
 * ========================================================================== */

static uint32_t s_rand = 0x1234ABCDU;

static uint32_t rnd(void)
{
  s_rand = (s_rand * 1103515245UL) + 12345UL;
  return (s_rand >> 16);
}

static void rnd_seed(uint32_t seed)
{
  s_rand = seed | 1U;
  (void)rnd();
}

/* ==========================================================================
 * 模式与调度
 * ========================================================================== */

typedef enum
{
  MODE_MENU = 0,
  MODE_EXP1,
  MODE_EXP2,
  MODE_EXP3,
  MODE_EXP4,
  MODE_EXP5
} AppMode;

static AppMode s_mode      = MODE_MENU;
static uint8_t s_need_draw = 1U;

/* 菜单 */
#define MENU_NUM    5U
static const char *const s_menu[MENU_NUM] =
{
  "1 OLED显示",
  "2 反应速度",
  "3 三色呼吸",
  "4 频率占空比",
  "5 N次闪光急停"
};
static uint8_t s_menu_sel = 0U;

/* --------------------------------------------------------------------------
 * 实验一：OLED 显示
 * -------------------------------------------------------------------------- */
static uint32_t s_e1_count = 0U;
static uint32_t s_e1_last  = 0U;

/* --------------------------------------------------------------------------
 * 实验二：随机反应速度测试器
 * -------------------------------------------------------------------------- */
typedef enum
{
  R_IDLE = 0,
  R_WAIT,
  R_GO,
  R_RESULT,
  R_FALSE
} ReactState;

static ReactState s_r_state  = R_IDLE;
static uint32_t   s_r_wait   = 0U;     /* 随机等待时长 */
static uint32_t   s_r_t0     = 0U;     /* 提示灯亮起时刻 */
static uint32_t   s_r_last    = 0U;    /* 本次反应时间 */
static uint32_t   s_r_wait_t0 = 0U;    /* 等待阶段起点 */
static uint32_t   s_r_best    = 0U;    /* 最好成绩，0 = 还没有 */
static uint8_t    s_r_have    = 0U;    /* 是否已经测过至少一次 */

/* --------------------------------------------------------------------------
 * 实验三：三色流水呼吸灯
 * -------------------------------------------------------------------------- */
#define RGB_MODE_NUM   3U
static uint8_t  s_rgb_mode  = 0U;
static uint16_t s_rgb_phase = 0U;
static uint32_t s_rgb_last  = 0U;
static uint8_t  s_rgb_run   = 1U;

static const char *const s_rgb_name[RGB_MODE_NUM] =
{
  "同步呼吸",
  "三色流水",
  "警灯闪烁"
};

/* --------------------------------------------------------------------------
 * 实验四：频率与占空比自测仪
 * -------------------------------------------------------------------------- */
typedef struct
{
  uint32_t freq;
  uint8_t  duty;
} MeterPoint;

static const MeterPoint s_meter_pts[3] =
{
  {100U,   25U},
  {1000U,  50U},
  {10000U, 75U}
};
#define METER_NUM   3U

static uint8_t  s_meter_idx  = 0U;
static uint8_t  s_meter_draw = 1U;
static uint32_t s_meter_last = 0U;

/* --------------------------------------------------------------------------
 * 实验五：N 次闪光与硬件急停
 * -------------------------------------------------------------------------- */
static uint8_t s_flash_n     = 3U;
static uint8_t s_flash_state = 0U;      /* 0=待机 1=运行 2=完成 3=已刹车 */

/* ==========================================================================
 * 实验一
 * ========================================================================== */
static void exp1_render(void)
{
  Disp_Clear();
  Disp_Line(0U, EXP_TITLE);

  /* 姓名 / 学号：内容来自 Inc/config.h，改那里重新编译即可 */
  Disp_String(0U, 1U * DISP_LINE_H, "姓名 ");
  Disp_String(40U, 1U * DISP_LINE_H, STUDENT_NAME);

  Disp_String(0U, 2U * DISP_LINE_H, "学号 ");
  Disp_String(40U, 2U * DISP_LINE_H, STUDENT_ID);

  Disp_String(0U, 3U * DISP_LINE_H, "计数 ");
  Disp_Int(40U, 3U * DISP_LINE_H, (int32_t)s_e1_count);

  Disp_Refresh();
}

static void exp1_task(int8_t key)
{
  if (key == (int8_t)KE_KEY1)
  {
    s_mode = MODE_MENU;
    s_need_draw = 1U;
    return;
  }

  if ((uint32_t)(HAL_GetTick() - s_e1_last) >= 1000U)
  {
    s_e1_last = HAL_GetTick();
    s_e1_count++;
    exp1_render();                      /* 每秒刷新一次递增计数 */
  }
}

/* ==========================================================================
 * 实验二
 * ========================================================================== */
static void exp2_render(void)
{
  Disp_Clear();
  Disp_Line(0U, "2 反应速度");

  switch (s_r_state)
  {
    case R_IDLE:
      Disp_Line(1U, "按K0开始");
      if (s_r_have != 0U)
      {
        Disp_String(0U, 2U * DISP_LINE_H, "最好 ");
        Disp_Int(40U, 2U * DISP_LINE_H, (int32_t)s_r_best);
        Disp_String(72U, 2U * DISP_LINE_H, "ms");
      }
      Disp_Line(3U, "等红灯亮再按K0");
      break;

    case R_WAIT:
      Disp_Line(1U, "请等待");
      Disp_Line(2U, "不要抢跑");
      Disp_Line(3U, "等红灯亮再按K0");
      break;

    case R_GO:
      Disp_Line(1U, "按K0！");
      break;

    case R_RESULT:
      /* 本次成绩 + 最好成绩 */
      Disp_String(0U, 1U * DISP_LINE_H, "本次 ");
      Disp_Int(40U, 1U * DISP_LINE_H, (int32_t)s_r_last);
      Disp_String(72U, 1U * DISP_LINE_H, "ms");
      Disp_String(0U, 2U * DISP_LINE_H, "最好 ");
      Disp_Int(40U, 2U * DISP_LINE_H, (int32_t)s_r_best);
      Disp_String(72U, 2U * DISP_LINE_H, "ms");
      Disp_Line(3U, "按K0再测");
      break;

    case R_FALSE:
      Disp_Line(1U, "抢跑！");
      Disp_Line(2U, "等红灯亮再按");
      Disp_Line(3U, "按K0重来");
      break;

    default:
      break;
  }

  Disp_Refresh();
}

static void exp2_enter(void)
{
  react_led_init();                    /* PB1 可能被实验三配成 TIM3_CH4，这里抢回来 */
  s_r_state = R_IDLE;
  REACT_LED(0);
  rnd_seed(HAL_GetTick());
}

/* 开始一次新测试：随机 2~5 秒后点亮提示灯 */
static void exp2_start(void)
{
  s_r_wait    = 2000U + (rnd() % 3001U);
  s_r_wait_t0 = HAL_GetTick();
  s_r_state   = R_WAIT;
  REACT_LED(0);
  s_need_draw = 1U;
}

static void exp2_task(int8_t key)
{
  uint32_t now = HAL_GetTick();

  if (key == (int8_t)KE_KEY1)
  {
    REACT_LED(0);
    s_mode = MODE_MENU;
    s_need_draw = 1U;
    return;
  }

  switch (s_r_state)
  {
    case R_IDLE:
      if (key == (int8_t)KE_KEY0)
      {
        exp2_start();                    /* 随机等待 2~5 秒 */
      }
      break;

    case R_WAIT:
      if (key >= 0)
      {
        s_r_state = R_FALSE;             /* 提示灯没亮就按 = 抢跑 */
        beep(500U);                      /* 抢跑：长鸣，一听就知道错了 */
        s_need_draw = 1U;
      }
      else if ((uint32_t)(now - s_r_wait_t0) >= s_r_wait)
      {
        s_r_state = R_GO;
        s_r_t0    = now;
        REACT_LED(1);                    /* 提示灯亮 = 开始计时 */
        s_need_draw = 1U;
      }
      break;

    case R_GO:
      if (key == (int8_t)KE_KEY0)
      {
        s_r_last = (uint32_t)(now - s_r_t0);
        REACT_LED(0);                    /* 提示灯熄灭 = 成绩已锁定 */

        /* 记录最好成绩 */
        if ((s_r_best == 0U) || (s_r_last < s_r_best))
        {
          s_r_best = s_r_last;
        }
        s_r_have = 1U;

        s_r_state = R_RESULT;
        beep(250U);                      /* 成功按下：明显的长"滴"，和按键提示音区分 */
        s_need_draw = 1U;
      }
      break;

    case R_RESULT:
      if (key == (int8_t)KE_KEY0)
      {
        exp2_start();                    /* 按 K0 直接再测一次 */
      }
      break;

    case R_FALSE:
      if (key >= 0)                      /* 任意键返回待机 */
      {
        s_r_state = R_IDLE;
        s_need_draw = 1U;
      }
      break;

    default:
      break;
  }

  /* ★ 必须在这里刷新屏幕。
   *   原来这里漏了渲染调用（只置了 s_need_draw 但没人用它），
   *   导致进入实验二后画面永远停在初始那一帧。                        */
  if (s_need_draw != 0U)
  {
    exp2_render();
    s_need_draw = 0U;
  }
}

/* ==========================================================================
 * 实验三：三色呼吸灯
 * ========================================================================== */

/* 抛物线近似的半正弦，输入 0..999（一个完整周期），输出 0..1000 */
static uint16_t isin1000(uint16_t p)
{
  uint32_t x = (p < 500U) ? p : (uint32_t)(1000U - p);

  return (uint16_t)((4UL * x * (500UL - x)) / 500UL);
}

static void exp3_apply(void)
{
  uint16_t r;
  uint16_t g;
  uint16_t b;

  switch (s_rgb_mode)
  {
    case 0U:    /* 同步呼吸：三路同相位 */
      r = isin1000(s_rgb_phase);
      g = r;
      b = r;
      break;

    case 1U:    /* 三色流水：三路相差 120 度 */
      r = isin1000(s_rgb_phase);
      g = isin1000((uint16_t)((s_rgb_phase + 333U) % 1000U));
      b = isin1000((uint16_t)((s_rgb_phase + 667U) % 1000U));
      break;

    default:    /* 警灯闪烁：红蓝交替 */
      if (((s_rgb_phase / 250U) % 2U) == 0U)
      {
        r = 1000U; g = 0U; b = 0U;
      }
      else
      {
        r = 0U; g = 0U; b = 1000U;
      }
      break;
  }

  BSP_Rgb_Set(r, g, b);
}

static void exp3_render(void)
{
  Disp_Clear();
  Disp_Line(0U, "3 三色呼吸");
  Disp_Line(1U, s_rgb_name[s_rgb_mode]);
  Disp_Line(2U, s_rgb_run ? "运行中" : "已停止");
  Disp_Line(3U, "UP切换 K0停");
  Disp_Refresh();
}

static void exp3_enter(void)
{
  BSP_Rgb_Init();
  BSP_Rgb_Start();
  s_rgb_mode  = 0U;
  s_rgb_phase = 0U;
  s_rgb_run   = 1U;
  exp3_apply();
}

static void exp3_task(int8_t key)
{
  uint32_t now = HAL_GetTick();

  if (key == (int8_t)KE_KEY1)
  {
    BSP_Rgb_Stop();
    s_mode = MODE_MENU;
    s_need_draw = 1U;
    return;
  }

  if (key == (int8_t)KE_UP)
  {
    s_rgb_mode = (uint8_t)((s_rgb_mode + 1U) % RGB_MODE_NUM);
    s_rgb_phase = 0U;
    if (s_rgb_run != 0U) { exp3_apply(); }
    s_need_draw = 1U;
  }

  if (key == (int8_t)KE_KEY0)
  {
    s_rgb_run = (s_rgb_run != 0U) ? 0U : 1U;
    if (s_rgb_run != 0U) { BSP_Rgb_Start(); exp3_apply(); }
    else                 { BSP_Rgb_Stop(); }
    s_need_draw = 1U;
  }

  if ((s_rgb_run != 0U) && ((uint32_t)(now - s_rgb_last) >= 20U))
  {
    s_rgb_last = now;
    s_rgb_phase = (uint16_t)((s_rgb_phase + 10U) % 1000U);   /* 20ms 一步 -> 2 秒一周期 */
    exp3_apply();
  }

  /* ★ 同样必须在这里刷新屏幕（原来也漏了，切模式时画面不会变） */
  if (s_need_draw != 0U)
  {
    exp3_render();
    s_need_draw = 0U;
  }
}

/* ==========================================================================
 * 实验四：频率与占空比自测仪
 * ========================================================================== */
/* 整数占用的像素宽度（ASCII 8 像素/字符，含负号） */
static uint8_t digits_of(int32_t v)
{
  uint8_t  d = 1U;
  uint32_t a = (v < 0) ? ((uint32_t)(-(v + 1)) + 1U) : (uint32_t)v;

  while (a >= 10U)
  {
    a /= 10U;
    d++;
  }
  if (v < 0)
  {
    d++;
  }
  return d;
}

/* 保留 1 位小数的数的像素宽度（含负号），用于计算后续内容的 x 坐标 */
static uint8_t fixed1_width(int32_t v10)
{
  uint32_t a  = (v10 < 0) ? ((uint32_t)(-(v10 + 1)) + 1U) : (uint32_t)v10;
  uint32_t ip = a / 10U;
  uint8_t  n  = 1U;                      /* 整数部分至少 1 位 */

  while (ip >= 10U)
  {
    ip /= 10U;
    n++;
  }
  n += 2U;                               /* '.' + 1 位小数 */
  if (v10 < 0)
  {
    n++;
  }
  return (uint8_t)(n * 8U);
}

/* 一行显示： <单字标签> <频率>Hz <占空比(1位小数)>
 * 用单字标签是为了让 "设 10000Hz 100.0" 也能塞进 128 像素 */
static void exp4_show_pair(uint8_t line, const char *tag, int32_t freq, int32_t duty_x10)
{
  uint8_t y = (uint8_t)(line * DISP_LINE_H);
  uint8_t x = 24U;                       /* 汉字标签 16 + 空格 8 */

  Disp_String(0U, y, tag);
  Disp_Int(x, y, freq);
  x = (uint8_t)(x + digits_of(freq) * 8U);
  Disp_String(x, y, "Hz ");
  x = (uint8_t)(x + 24U);                /* "Hz " = 8+8+8 */
  Disp_Fixed1(x, y, duty_x10);
}

static void exp4_render(void)
{
  uint32_t meas_f = 0U;
  uint16_t meas_d = 0U;
  uint8_t  ok;

  Disp_Clear();
  Disp_Line(0U, "4 频率占空比");

  /* 设定值 */
  exp4_show_pair(1U, "设 ", (int32_t)s_meter_pts[s_meter_idx].freq,
                 (int32_t)s_meter_pts[s_meter_idx].duty * 10);

  ok = BSP_Cap_Get(&meas_f, &meas_d);
  s_meter_draw = 0U;

  if (ok == 0U)
  {
    Disp_Line(2U, "测 无信号");
    Disp_Line(3U, "请接跳线");
  }
  else
  {
    int32_t err_f;
    uint8_t x;

    /* 测量值 */
    exp4_show_pair(2U, "测 ", (int32_t)meas_f, (int32_t)meas_d);

    /* 相对误差，单位 0.1% */
    err_f = ((int32_t)meas_f - (int32_t)s_meter_pts[s_meter_idx].freq) * 1000
          / (int32_t)s_meter_pts[s_meter_idx].freq;

    Disp_String(0U, 3U * DISP_LINE_H, "误 ");
    Disp_Fixed1(24U, 3U * DISP_LINE_H, err_f);
    x = (uint8_t)(24U + fixed1_width(err_f));
    Disp_String(x, 3U * DISP_LINE_H, "%");
  }

  Disp_Refresh();
}

static void exp4_enter(void)
{
  /* TIM3 从 PWM 切到输入捕获；TIM4 输出测试信号 */
  BSP_SigOut_Init();
  BSP_Cap_Init();
  BSP_SigOut_Set(s_meter_pts[s_meter_idx].freq, s_meter_pts[s_meter_idx].duty);
  BSP_SigOut_Start();
  s_meter_draw = 1U;
  s_meter_last = HAL_GetTick();
}

static void exp4_leave(void)
{
  BSP_SigOut_Stop();
  BSP_Cap_DeInit();
}

static void exp4_task(int8_t key)
{
  if (key == (int8_t)KE_KEY1)
  {
    exp4_leave();
    s_mode = MODE_MENU;
    s_need_draw = 1U;
    return;
  }

  if (key == (int8_t)KE_UP)
  {
    s_meter_idx = (uint8_t)((s_meter_idx + 1U) % METER_NUM);
    BSP_SigOut_Set(s_meter_pts[s_meter_idx].freq, s_meter_pts[s_meter_idx].duty);
    s_meter_draw = 1U;
  }

  /* 每 200ms 刷新一次测量结果（用时间戳，避免同一毫秒窗口内重复刷新） */
  if ((s_meter_draw != 0U) ||
      ((uint32_t)(HAL_GetTick() - s_meter_last) >= 200U))
  {
    s_meter_last = HAL_GetTick();
    exp4_render();
  }
}

/* ==========================================================================
 * 实验五：N 次闪光 + 硬件急停
 * ========================================================================== */
static void exp5_render(void)
{
  Disp_Clear();
  Disp_Line(0U, "5 N次闪光急停");

  Disp_String(0U, 1U * DISP_LINE_H, "次数 N=");
  Disp_Int(56U, 1U * DISP_LINE_H, (int32_t)s_flash_n);

  Disp_Line(2U, "UP改 K0启动");

  switch (s_flash_state)
  {
    case 1U:  Disp_Line(3U, "运行中"); break;
    case 2U:  Disp_Line(3U, "已停止"); break;
    case 3U:  Disp_Line(3U, "已刹车 K2确认"); break;
    default:  Disp_Line(3U, "待机"); break;
  }

  Disp_Refresh();
}

static void exp5_enter(void)
{
  BSP_Flash_Init();
  s_flash_n     = 3U;
  s_flash_state = 0U;
}

static void exp5_task(int8_t key)
{
  if (key == (int8_t)KE_KEY1)
  {
    BSP_Flash_Stop();
    s_mode = MODE_MENU;
    s_need_draw = 1U;
    return;
  }

  /* 刹车是硬件异步动作，这里只负责发现并锁存状态 */
  if (BSP_Flash_BrakeLatched() != 0U)
  {
    if (s_flash_state != 3U)
    {
      s_flash_state = 3U;
      beep(200U);
      s_need_draw = 1U;
    }
  }

  if (key == (int8_t)KE_UP)
  {
    s_flash_n = (uint8_t)((s_flash_n % 9U) + 1U);   /* 1..9 循环 */
    s_need_draw = 1U;
  }

  if (key == (int8_t)KE_KEY0)
  {
    if (BSP_Flash_Start(s_flash_n) == 0U)
    {
      s_flash_state = 1U;
    }
    s_need_draw = 1U;
  }

  if (key == (int8_t)KE_KEY2)
  {
    /* 人工确认：清刹车标志并重新使能输出 */
    BSP_Flash_ClearBrake();
    s_flash_state = 0U;
    s_need_draw = 1U;
  }

  /* 发完 N 个脉冲后 OPM 会自动清 CEN */
  if ((s_flash_state == 1U) && (BSP_Flash_IsBusy() == 0U))
  {
    s_flash_state = 2U;
    s_need_draw = 1U;
  }

  if (s_need_draw != 0U)
  {
    exp5_render();
    s_need_draw = 0U;      /* ← 必须清掉，否则整屏刷新会在主循环里每圈都跑 */
  }
}

/* ==========================================================================
 * 菜单
 * ========================================================================== */
static void menu_render(void)
{
  uint8_t first = (s_menu_sel < 3U) ? 0U : (uint8_t)(s_menu_sel - 2U);

  Disp_Clear();
  Disp_Line(0U, EXP_TITLE);

  for (uint8_t i = 0U; i < 3U; i++)
  {
    uint8_t idx = (uint8_t)(first + i);
    if (idx >= MENU_NUM)
    {
      break;
    }
    Disp_String(0U, (uint8_t)((i + 1U) * DISP_LINE_H), (idx == s_menu_sel) ? ">" : " ");
    Disp_String(8U, (uint8_t)((i + 1U) * DISP_LINE_H), s_menu[idx]);
  }

  Disp_Refresh();
  s_need_draw = 0U;
}

static void app_enter_mode(AppMode m)
{
  s_mode = m;
  s_need_draw = 1U;

  switch (m)
  {
    case MODE_EXP1:
      s_e1_count = 0U;
      s_e1_last  = HAL_GetTick();
      exp1_render();
      break;

    case MODE_EXP2:
      exp2_enter();
      exp2_render();
      break;

    case MODE_EXP3:
      exp3_enter();
      exp3_render();
      break;

    case MODE_EXP4:
      exp4_enter();
      exp4_render();
      break;

    case MODE_EXP5:
      exp5_enter();
      exp5_render();
      break;

    case MODE_MENU:
    default:
      menu_render();
      break;
  }

  s_need_draw = 0U;
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

void App_Init(void)
{
  (void)PCF8574_Init();                 /* 板载蜂鸣器 */
  Disp_Init();                          /* OLED */
  keys_init();
  REACT_LED(0);
  rnd_seed(HAL_GetTick());

  app_enter_mode(MODE_MENU);
  beep(120U);                           /* 上电"滴"一声：IIC 链路自检 */
}

void App_Task(void)
{
  int8_t key = key_scan();

  if (key >= 0)
  {
    beep(40U);                          /* 按键提示音 */
  }

  switch (s_mode)
  {
    case MODE_MENU:
      if (s_need_draw != 0U)
      {
        menu_render();
      }
      if (key == (int8_t)KE_UP)
      {
        s_menu_sel = (uint8_t)((s_menu_sel + 1U) % MENU_NUM);
        s_need_draw = 1U;
      }
      if (key == (int8_t)KE_KEY0)
      {
        app_enter_mode((AppMode)(MODE_EXP1 + s_menu_sel));
      }
      break;

    case MODE_EXP1: exp1_task(key); break;
    case MODE_EXP2: exp2_task(key); break;
    case MODE_EXP3: exp3_task(key); break;
    case MODE_EXP4: exp4_task(key); break;
    case MODE_EXP5: exp5_task(key); break;

    default:
      s_mode = MODE_MENU;
      s_need_draw = 1U;
      break;
  }

  beep_task();
}

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 跑马灯 + 蜂鸣器实验  （正点原子阿波罗 V2 STM32F429）
  ******************************************************************************
  * 硬件（正点原子阿波罗 V2 STM32F429 + DNF429-EB 实验箱面包板）
  * ---------------------------------------------------------------------------
  *  8 个外接 LED（低电平点亮）：
  *      LED0 = PE10   LED1 = PE11   LED2 = PE12   LED3 = PE5
  *      LED4 = PE13   LED5 = PE7    LED6 = PE8    LED7 = PE9
  *  4 个按键：
  *      KEY0 = PH3    KEY1 = PH2    KEY2 = PC13   （低电平有效，上拉）
  *      KEY_UP = PA0                              （高电平有效，下拉）
  *  蜂鸣器（板载有源蜂鸣器，不在 GPIO 上！）：
  *      BEEP -> PCF8574（IIC IO 扩展芯片）的 P0
  *      IIC 总线：SCL = PH4，SDA = PH5，器件地址 0x20
  *      驱动：S8550(PNP) 高边开关 -> P0 拉低时鸣响（低电平有效）
  *      实现见 pcf8574.c（软件模拟 IIC）
  *
  * 操作方式：
  *      KEY0 / KEY1 / KEY2 短按 -> 切换特效 0 / 1 / 2
  *      KEY_UP 短按             -> 切换特效 3
  *      KEY_UP 长按(>=600ms)    -> 进入 / 退出"呼吸灯"模式（选做部分）
  *      每次有效按下蜂鸣器只响一次
  *
  * 呼吸灯（选做部分：PWM 占空比渐变实现亮度平滑变化）
  *      8 个 LED 无法全部用硬件 PWM：PE7 只有 TIM1_ETR(外部触发输入)，
  *      PE8/PE10/PE12 只能作 TIM1 的互补输出，因此改用 TIM7 中断做软件 PWM：
  *      20kHz 中断 + 100 级分辨率 -> 200Hz PWM，
  *      占空比按余弦曲线 0 -> 100 -> 0 渐变，8 个灯完全同步呼吸。
  *      实现见 softpwm.c / softpwm.h
  *
  * 系统时钟：HSE 25MHz（PH0/PH1）-> PLL -> 180MHz
  * 调试接口：SWD（PA13/PA14）
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include "pcf8574.h"
#include "softpwm.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* 外设引脚描述：端口 / 引脚 / 有效电平 */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  GPIO_PinState active_state;
} AppGpioPin;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LED_COUNT           8U      /* LED 数量 */
#define KEY_COUNT           4U      /* 按键数量 */
#define KEY_DEBOUNCE_MS     20U     /* 按键消抖时间(ms) */
#define EFFECT_INTERVAL_MS  150U    /* 灯效每步间隔(ms) */
#define BEEP_TIME_MS        80U     /* 蜂鸣器鸣响时长(ms) */

#define BEEP_BOOT_SELFTEST  1U      /* 上电"滴"一声：验证 IIC 链路与蜂鸣器 */

/* ---- 呼吸灯（选做部分：PWM 占空比渐变）---- */
#define BREATH_STEPS        64U     /* 一个完整呼吸周期的级数 */
#define BREATH_STEP_MS      40U     /* 每级保持时间(ms)，64*40ms -> 周期约 2.56s */
#define KEY_LONGPRESS_MS    600U    /* KEY_UP 长按判定门限(ms) */
#define KEY_UP_INDEX        3U      /* KEY_UP 在 key_pins[] 中的下标 */

/* KEY_UP 手势判定结果 */
#define KEY_GESTURE_NONE    0U
#define KEY_GESTURE_SHORT   1U      /* 短按：走原来的特效 3 */
#define KEY_GESTURE_LONG    2U      /* 长按：进入 / 退出呼吸灯 */

/* 第 4 个按键 KEY_UP 接在 PA0（WKUP 引脚），CubeMX 不生成其宏，在此定义 */
#ifndef KEY_UP_Pin
#define KEY_UP_Pin          GPIO_PIN_0
#define KEY_UP_GPIO_Port    GPIOA
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* 取反：由有效电平推出无效电平 */
#define INACTIVE_STATE(active) ((active) == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET)
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 8 个 LED 引脚表（顺序即跑马灯顺序），低电平点亮 */
static const AppGpioPin led_pins[LED_COUNT] =
{
  {GPIOE, GPIO_PIN_10, GPIO_PIN_RESET},   /* LED0 */
  {GPIOE, GPIO_PIN_11, GPIO_PIN_RESET},   /* LED1 */
  {GPIOE, GPIO_PIN_12, GPIO_PIN_RESET},   /* LED2 */
  {GPIOE, GPIO_PIN_5,  GPIO_PIN_RESET},   /* LED3 */
  {GPIOE, GPIO_PIN_13, GPIO_PIN_RESET},   /* LED4 */
  {GPIOE, GPIO_PIN_7,  GPIO_PIN_RESET},   /* LED5 */
  {GPIOE, GPIO_PIN_8,  GPIO_PIN_RESET},   /* LED6 */
  {GPIOE, GPIO_PIN_9,  GPIO_PIN_RESET}    /* LED7 */
};

/* 4 个按键：前 3 个低电平有效(上拉)，KEY_UP 高电平有效(下拉) */
static const AppGpioPin key_pins[KEY_COUNT] =
{
  {KEY0_GPIO_Port,   KEY0_Pin,   GPIO_PIN_RESET},   /* PH3  低电平有效 */
  {KEY1_GPIO_Port,   KEY1_Pin,   GPIO_PIN_RESET},   /* PH2  低电平有效 */
  {KEY2_GPIO_Port,   KEY2_Pin,   GPIO_PIN_RESET},   /* PC13 低电平有效 */
  {KEY_UP_GPIO_Port, KEY_UP_Pin, GPIO_PIN_SET}      /* PA0  高电平有效 */
};

/* 按键消抖用：原始值 / 已确认稳态 / 变化时刻 */
static GPIO_PinState key_last_raw[KEY_COUNT];
static GPIO_PinState key_stable[KEY_COUNT];
static uint32_t      key_changed_tick[KEY_COUNT];

/* 灯效状态机 */
static uint8_t  effect_mode      = 0U;  /* 当前特效 0..3 */
static uint8_t  effect_step      = 0U;  /* 当前步进 */
static uint32_t last_effect_tick = 0U;  /* 上次刷新时刻 */
static uint8_t  effect_refresh   = 1U;  /* 1 = 立即刷新 */

/* 蜂鸣器自动关断时刻 + PCF8574 是否就绪 */
static uint32_t beep_stop_tick = 0U;
static uint8_t  beep_ready     = 0U;

/* 呼吸灯模式（选做部分） */
static uint8_t  breath_mode  = 0U;  /* 1 = 当前处于呼吸灯模式 */
static uint8_t  breath_index = 0U;  /* 当前处于占空比表中的第几级 */
static uint32_t breath_tick  = 0U;  /* 上一级亮度的刷新时刻 */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void APP_GPIO_Init(void);
static void Keys_Init(void);
static uint8_t Key_Update_Debounce(void);
static int8_t Scan_Effect_Key(uint8_t changed_mask);
static uint8_t Scan_KeyUp_Gesture(uint8_t changed_mask);
static void Select_Effect(uint8_t mode);
static void Run_Effect_Task(void);
static void Write_Leds(uint8_t pattern);
static void All_Leds_Off(void);
static void Breath_Init(void);
static void Breath_Toggle(void);
static void Run_Breath_Task(void);
static void Buzzer_Init(void);
static void Buzzer_Write(uint8_t on);
static void Buzzer_Beep(uint32_t duration_ms);
static void Buzzer_Task(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
  * @brief  应用层 GPIO 初始化
  *         LED -> 推挽输出；按键 -> 输入（按有效电平选上下拉）
  *         注意：PA0(KEY_UP) CubeMX 未生成，这里手动初始化
  *         蜂鸣器不在 GPIO 上（走 PCF8574），见 Buzzer_Init()
  */
static void APP_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 使能所需端口时钟 */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* ---- 8 个 LED：推挽输出、上拉、高速 ---- */
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  for (uint8_t i = 0U; i < LED_COUNT; i++)
  {
    GPIO_InitStruct.Pin = led_pins[i].pin;
    HAL_GPIO_Init(led_pins[i].port, &GPIO_InitStruct);
  }
  All_Leds_Off();                       /* 上电全灭 */

  /* ---- 4 个按键：输入；低电平有效上拉、高电平有效下拉 ---- */
  GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  for (uint8_t i = 0U; i < KEY_COUNT; i++)
  {
    GPIO_InitStruct.Pin  = key_pins[i].pin;
    GPIO_InitStruct.Pull = (key_pins[i].active_state == GPIO_PIN_RESET) ?
                           GPIO_PULLUP : GPIO_PULLDOWN;
    HAL_GPIO_Init(key_pins[i].port, &GPIO_InitStruct);
  }
}

/**
  * @brief  记录按键初始状态（避免上电误触发）
  */
static void Keys_Init(void)
{
  uint32_t now = HAL_GetTick();

  for (uint8_t i = 0U; i < KEY_COUNT; i++)
  {
    key_last_raw[i]     = HAL_GPIO_ReadPin(key_pins[i].port, key_pins[i].pin);
    key_stable[i]       = key_last_raw[i];
    key_changed_tick[i] = now;
  }
}

/**
  * @brief  更新全部按键的消抖状态
  * @retval 位掩码：本次"稳定电平发生翻转"的按键（bit i 对应 key_pins[i]）
  * @note   时间戳消抖 KEY_DEBOUNCE_MS；只在电平真正翻转的那一次把对应位置 1，
  *         因此按住不放不会反复产生事件，蜂鸣器也就只响一次。
  */
static uint8_t Key_Update_Debounce(void)
{
  uint32_t now          = HAL_GetTick();
  uint8_t  changed_mask = 0U;

  for (uint8_t i = 0U; i < KEY_COUNT; i++)
  {
    GPIO_PinState raw = HAL_GPIO_ReadPin(key_pins[i].port, key_pins[i].pin);

    /* 原始电平变化 -> 重新计时 */
    if (raw != key_last_raw[i])
    {
      key_last_raw[i]     = raw;
      key_changed_tick[i] = now;
    }

    /* 稳定超过消抖时间，且与已确认状态不同 -> 确认翻转 */
    if (((uint32_t)(now - key_changed_tick[i]) >= KEY_DEBOUNCE_MS) &&
        (raw != key_stable[i]))
    {
      key_stable[i] = raw;
      changed_mask |= (uint8_t)(1U << i);
    }
  }

  return changed_mask;
}

/**
  * @brief  从按键翻转中取出"特效选择键按下"事件（KEY0 / KEY1 / KEY2）
  * @param  changed_mask Key_Update_Debounce() 的返回值
  * @retval 0..2 = 对应按键刚按下；-1 = 无事件
  * @note   KEY_UP 不在这里处理：它需要区分短按与长按，见 Scan_KeyUp_Gesture()。
  */
static int8_t Scan_Effect_Key(uint8_t changed_mask)
{
  for (uint8_t i = 0U; i < KEY_UP_INDEX; i++)
  {
    if (((changed_mask & (uint8_t)(1U << i)) != 0U) &&
        (key_stable[i] == key_pins[i].active_state))
    {
      return (int8_t)i;
    }
  }

  return -1;
}

/**
  * @brief  KEY_UP 手势识别：短按 / 长按
  * @param  changed_mask Key_Update_Debounce() 的返回值
  * @retval KEY_GESTURE_NONE / KEY_GESTURE_SHORT / KEY_GESTURE_LONG
  * @note   短按动作要等"松手"才判定，这样按住不放时不会先误切一次特效 3；
  *         长按在按住过程中就触发且只触发一次，松手后也不再补发短按。
  */
static uint8_t Scan_KeyUp_Gesture(uint8_t changed_mask)
{
  static uint8_t  pressed    = 0U;   /* 当前是否按住 KEY_UP */
  static uint8_t  long_fired = 0U;   /* 本次按住是否已判为长按 */
  static uint32_t press_tick = 0U;   /* 按下时刻 */

  /* 处理按下 / 松开边沿（已消抖） */
  if ((changed_mask & (uint8_t)(1U << KEY_UP_INDEX)) != 0U)
  {
    if (key_stable[KEY_UP_INDEX] == key_pins[KEY_UP_INDEX].active_state)
    {
      pressed    = 1U;
      long_fired = 0U;
      press_tick = HAL_GetTick();
    }
    else
    {
      pressed = 0U;

      if (long_fired == 0U)          /* 没到长按门限就松手 -> 判为短按 */
      {
        return KEY_GESTURE_SHORT;
      }
    }
  }

  /* 按住过程中到达长按门限 -> 立即触发一次 */
  if ((pressed != 0U) && (long_fired == 0U) &&
      ((uint32_t)(HAL_GetTick() - press_tick) >= KEY_LONGPRESS_MS))
  {
    long_fired = 1U;
    return KEY_GESTURE_LONG;
  }

  return KEY_GESTURE_NONE;
}

/**
  * @brief  切换灯效（按键按下时调用），并触发一次蜂鸣
  */
static void Select_Effect(uint8_t mode)
{
  /* 若正处于呼吸灯模式，先退出：把 LED 的控制权从软件 PWM 交还给普通 GPIO */
  if (breath_mode != 0U)
  {
    breath_mode = 0U;
    SoftPwm_Enable(0U);
  }

  effect_mode    = (uint8_t)(mode % KEY_COUNT);
  effect_step    = 0U;
  effect_refresh = 1U;
  Buzzer_Beep(BEEP_TIME_MS);            /* 每次有效按下只响一次 */
}

/**
  * @brief  灯效任务：每 EFFECT_INTERVAL_MS 推进一次
  *         4 种特效：单点移动 / 逐格点亮 / 奇偶交替 / 全亮全灭
  */
static void Run_Effect_Task(void)
{
  /* KEY0：单个 LED 依次移动 */
  static const uint8_t key0_patterns[] =
  {
    0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U
  };
  /* KEY1：LED 逐格点亮（累加），最后清空 */
  static const uint8_t key1_patterns[] =
  {
    0x01U, 0x03U, 0x07U, 0x0FU, 0x1FU, 0x3FU, 0x7FU, 0xFFU, 0x00U
  };
  /* KEY2：奇数位与偶数位交替闪 */
  static const uint8_t key2_patterns[] =
  {
    0x55U, 0xAAU
  };
  /* KEY_UP：全部一起闪 */
  static const uint8_t key3_patterns[] =
  {
    0xFFU, 0x00U
  };

  uint8_t  pattern       = 0U;
  uint8_t  pattern_count = 0U;
  uint32_t now           = HAL_GetTick();

  /* 未到刷新时间且非强制刷新 -> 直接返回（非阻塞） */
  if ((effect_refresh == 0U) &&
      ((uint32_t)(now - last_effect_tick) < EFFECT_INTERVAL_MS))
  {
    return;
  }

  effect_refresh   = 0U;
  last_effect_tick = now;

  switch (effect_mode)
  {
    case 0U:  /* KEY0 */
      pattern       = key0_patterns[effect_step];
      pattern_count = (uint8_t)(sizeof(key0_patterns) / sizeof(key0_patterns[0]));
      break;

    case 1U:  /* KEY1 */
      pattern       = key1_patterns[effect_step];
      pattern_count = (uint8_t)(sizeof(key1_patterns) / sizeof(key1_patterns[0]));
      break;

    case 2U:  /* KEY2 */
      pattern       = key2_patterns[effect_step];
      pattern_count = (uint8_t)(sizeof(key2_patterns) / sizeof(key2_patterns[0]));
      break;

    default:  /* KEY_UP */
      pattern       = key3_patterns[effect_step];
      pattern_count = (uint8_t)(sizeof(key3_patterns) / sizeof(key3_patterns[0]));
      break;
  }

  Write_Leds(pattern);
  effect_step = (uint8_t)((effect_step + 1U) % pattern_count);
}

/**
  * @brief  按位模式写 8 个 LED（bit0 -> LED0 ... bit7 -> LED7）
  */
static void Write_Leds(uint8_t pattern)
{
  for (uint8_t i = 0U; i < LED_COUNT; i++)
  {
    GPIO_PinState state = ((pattern & (uint8_t)(1U << i)) != 0U) ?
                          led_pins[i].active_state :
                          INACTIVE_STATE(led_pins[i].active_state);

    HAL_GPIO_WritePin(led_pins[i].port, led_pins[i].pin, state);
  }
}

/**
  * @brief  全部 LED 熄灭
  */
static void All_Leds_Off(void)
{
  Write_Leds(0x00U);
}

/* ---------------------- 呼吸灯（选做部分：软件 PWM） ---------------------- */

/**
  * @brief  呼吸灯初始化：把 8 个 LED 引脚登记给软件 PWM 模块
  * @note   引脚方向已在 APP_GPIO_Init() 中配成推挽输出，这里只做登记。
  *         LED 表与跑马灯共用 led_pins[]，因此不存在两处定义不一致的问题。
  */
static void Breath_Init(void)
{
  SoftPwmChannel channels[LED_COUNT];

  for (uint8_t i = 0U; i < LED_COUNT; i++)
  {
    channels[i].port         = led_pins[i].port;
    channels[i].pin          = led_pins[i].pin;
    channels[i].active_state = led_pins[i].active_state;
  }

  SoftPwm_Init(channels, LED_COUNT);
}

/**
  * @brief  进入 / 退出呼吸灯模式（KEY_UP 长按时调用）
  * @note   进入时从占空比 0 起步，熄灯状态下慢慢亮起，不会有突变感；
  *         退出时停止 PWM，驱动内部会把 8 个灯全部熄灭。
  */
static void Breath_Toggle(void)
{
  if (breath_mode == 0U)
  {
    breath_mode  = 1U;
    breath_index = 0U;
    breath_tick  = HAL_GetTick();
    SoftPwm_SetDuty(0U);
    SoftPwm_Enable(1U);
  }
  else
  {
    breath_mode    = 0U;
    SoftPwm_Enable(0U);
    effect_refresh = 1U;           /* 回到灯效后立即重画一帧，不等 150ms */
  }

  Buzzer_Beep(BEEP_TIME_MS);       /* 每次有效按下只响一次 */
}

/**
  * @brief  呼吸灯任务：按余弦曲线把占空比 0 -> 100 -> 0 缓慢推进
  * @note   表值 = round(50 * (1 - cos(2*PI*i/64)))，即占空比 0..100。
  *         余弦曲线两端变化慢、中间变化快，比线性三角波更接近自然呼吸的节奏，
  *         不会在亮度高低两端出现"顿一下"的感觉。
  */
static void Run_Breath_Task(void)
{
  /* 一个完整呼吸周期的占空比表（单位：1/100，即 0..SOFTPWM_STEPS） */
  static const uint8_t breath_duty[BREATH_STEPS] =
  {
     0U,  0U,  1U,  2U,  4U,  6U,  8U, 11U,
    15U, 18U, 22U, 26U, 31U, 35U, 40U, 45U,
    50U, 55U, 60U, 65U, 69U, 74U, 78U, 82U,
    85U, 89U, 92U, 94U, 96U, 98U, 99U, 100U,
   100U, 99U, 98U, 96U, 94U, 92U, 89U, 85U,
    82U, 78U, 74U, 69U, 65U, 60U, 55U, 50U,
    45U, 40U, 35U, 31U, 26U, 22U, 18U, 15U,
    11U,  8U,  6U,  4U,  2U,  1U,  0U,  0U
  };

  uint32_t now = HAL_GetTick();

  /* 非阻塞：每 BREATH_STEP_MS 才推进一级亮度 */
  if ((uint32_t)(now - breath_tick) < BREATH_STEP_MS)
  {
    return;
  }
  breath_tick = now;

  SoftPwm_SetDuty(breath_duty[breath_index]);
  breath_index = (uint8_t)((breath_index + 1U) % BREATH_STEPS);
}

/**
  * @brief  蜂鸣器初始化：初始化 PCF8574（软件 IIC @ PH4/PH5）
  * @note   板载有源蜂鸣器挂在 PCF8574 的 P0，经 S8550(PNP) 高边开关驱动，
  *         低电平鸣响。若 PCF8574 无应答（IIC 有问题），beep_ready 保持 0，
  *         后续按键不会去访问总线。
  */
static void Buzzer_Init(void)
{
  beep_ready = (PCF8574_Init() == 0U) ? 1U : 0U;

  if (beep_ready != 0U)
  {
    Buzzer_Write(0U);                 /* 确保上电不响 */
  }
}

/**
  * @brief  写蜂鸣器（on != 0 -> 响）
  */
static void Buzzer_Write(uint8_t on)
{
  if (beep_ready == 0U)
  {
    return;
  }

  (void)PCF8574_WriteBit(PCF8574_BEEP_BIT,
                         (on != 0U) ? PCF8574_BEEP_ON : PCF8574_BEEP_OFF);
}

/**
  * @brief  启动一次蜂鸣（非阻塞，到时由 Buzzer_Task 关断）
  */
static void Buzzer_Beep(uint32_t duration_ms)
{
  if (beep_ready == 0U)
  {
    return;
  }

  Buzzer_Write(1U);
  beep_stop_tick = HAL_GetTick() + duration_ms;
}

/**
  * @brief  蜂鸣器关断任务
  */
static void Buzzer_Task(void)
{
  if ((beep_stop_tick != 0U) &&
      ((int32_t)(HAL_GetTick() - beep_stop_tick) >= 0))
  {
    Buzzer_Write(0U);
    beep_stop_tick = 0U;
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  APP_GPIO_Init();      /* 应用层 GPIO（含 KEY_UP） */
  Breath_Init();        /* 呼吸灯：登记 8 个 LED + 配置 TIM7 软件 PWM 时基 */
  Buzzer_Init();        /* 蜂鸣器：PCF8574 软件 IIC @ PH4/PH5 */
  Keys_Init();          /* 记录按键初态，防上电误触发 */
  Select_Effect(0U);    /* 默认进入特效 0 */

#if BEEP_BOOT_SELFTEST
  /* 上电"滴"一声：能听到就说明 IIC 链路 + 蜂鸣器都正常 */
  Buzzer_Beep(120U);
  HAL_Delay(200U);
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint8_t changed_mask = Key_Update_Debounce();   /* 按键消抖，得到翻转掩码 */
    int8_t  effect_key   = Scan_Effect_Key(changed_mask);
    uint8_t gesture;

    /* KEY0 / KEY1 / KEY2 短按 -> 切换对应特效 */
    if (effect_key >= 0)
    {
      Select_Effect((uint8_t)effect_key);
    }

    /* KEY_UP：短按选特效 3，长按进入/退出呼吸灯 */
    gesture = Scan_KeyUp_Gesture(changed_mask);
    if (gesture == KEY_GESTURE_LONG)
    {
      Breath_Toggle();
    }
    else if (gesture == KEY_GESTURE_SHORT)
    {
      Select_Effect(KEY_UP_INDEX);
    }

    /* 两种模式互斥：呼吸灯模式下 LED 由软件 PWM 接管，跑马灯不再刷灯 */
    if (breath_mode != 0U)
    {
      Run_Breath_Task();
    }
    else
    {
      Run_Effect_Task();              /* 灯效推进（非阻塞） */
    }

    Buzzer_Task();                    /* 蜂鸣器到时关断 */
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate Over-Drive mode to reach 180 MHz
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/**
  ******************************************************************************
  * @file    softpwm.c
  * @brief   软件 PWM 驱动实现（原理与取舍见 softpwm.h 的文件头注释）
  ******************************************************************************
  * 实现要点
  * ---------------------------------------------------------------------------
  * 1. 中断里直接写 BSRR 寄存器，不做"读-改-写"。
  *    BSRR 的低 16 位写 1 = 置位该引脚，高 16 位写 1 = 复位该引脚，
  *    写 0 的位不受影响，所以一条写指令就能原子地只改自己那一位。
  *    每个通道在初始化时预先算好"点亮值"和"熄灭值"两个掩码，中断里直接用。
  *
  * 2. 只在电平真的发生变化时才写端口。
  *    8 个灯占空比相同，因此一个 PWM 周期内只会翻转 2 次（全亮 / 全灭），
  *    其余 20kHz 的中断只是计数器加一，开销极小（CPU 占用 < 1%）。
  *
  * 3. 与其他外设不冲突：
  *    本工程蜂鸣器走 PH4/PH5 的软件 IIC。IIC 是静态总线（没有最低时钟频率
  *    要求），被中断打断只会让 SCL/SDA 的保持时间变长，不会传输出错，
  *    所以 PWM 与蜂鸣器互不干扰。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "softpwm.h"

/* Private types -------------------------------------------------------------*/
/**
  * @brief 通道运行参数：预计算的 BSRR 掩码，中断里可直接使用
  */
typedef struct
{
  GPIO_TypeDef *port;
  uint32_t      bsrr_on;    /* 输出"点亮"时写入 BSRR 的值 */
  uint32_t      bsrr_off;   /* 输出"熄灭"时写入 BSRR 的值 */
} SoftPwmRuntime;

/* Private variables ---------------------------------------------------------*/
static SoftPwmRuntime s_channel[SOFTPWM_MAX_CH];   /* 通道运行参数表 */
static uint8_t        s_count   = 0U;              /* 已登记通道数 */
static uint8_t        s_enabled = 0U;              /* 1 = PWM 输出中 */
static uint8_t        s_tick    = 0U;              /* PWM 计数器 0..SOFTPWM_STEPS-1 */
static uint8_t        s_duty    = 0U;              /* 当前占空比 0..SOFTPWM_STEPS */
static uint8_t        s_level   = 0U;              /* 当前输出电平 0=灭 1=亮 */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  初始化软件 PWM
  */
void SoftPwm_Init(const SoftPwmChannel *channels, uint8_t count)
{
  uint8_t i;

  if (channels == NULL)
  {
    return;
  }

  if (count > SOFTPWM_MAX_CH)
  {
    count = SOFTPWM_MAX_CH;
  }
  s_count = count;

  /* ---- 1. 登记通道，并预计算 BSRR 掩码 ---- */
  for (i = 0U; i < count; i++)
  {
    uint32_t pin = (uint32_t)channels[i].pin;

    s_channel[i].port = channels[i].port;

    if (channels[i].active_state == GPIO_PIN_SET)
    {
      s_channel[i].bsrr_on  = pin;            /* 置位 -> 高电平 -> 点亮 */
      s_channel[i].bsrr_off = pin << 16U;     /* 复位 -> 低电平 -> 熄灭 */
    }
    else
    {
      s_channel[i].bsrr_on  = pin << 16U;     /* 复位 -> 低电平 -> 点亮 */
      s_channel[i].bsrr_off = pin;            /* 置位 -> 高电平 -> 熄灭 */
    }
  }

  /* ---- 2. 上电先全部熄灭，避免残留亮灯 ---- */
  for (i = 0U; i < s_count; i++)
  {
    s_channel[i].port->BSRR = s_channel[i].bsrr_off;
  }

  s_enabled = 0U;
  s_tick    = 0U;
  s_duty    = 0U;
  s_level   = 0U;

  /* ---- 3. 配置 TIM7 作为软件 PWM 的时基 ---- */
  __HAL_RCC_TIM7_CLK_ENABLE();

  TIM7->CR1  = 0U;                       /* 先关掉，配置期间不产生中断 */
  TIM7->PSC  = SOFTPWM_TIM_PSC;
  TIM7->ARR  = SOFTPWM_TIM_ARR;
  TIM7->EGR  = TIM_EGR_UG;               /* 产生一次更新事件，立即装载 PSC */
  TIM7->SR   = 0U;                       /* 清掉更新标志 */
  TIM7->DIER = TIM_DIER_UIE;             /* 允许更新中断 */
  TIM7->CR1  = TIM_CR1_ARPE;             /* 自动重装，但暂不启动(CEN=0) */

  /* 优先级 1：低于 SysTick(0)，保证 HAL_GetTick 的毫秒节拍不被拖慢 */
  HAL_NVIC_SetPriority(TIM7_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);
}

/**
  * @brief  启动 / 停止 PWM 输出
  */
void SoftPwm_Enable(uint8_t enable)
{
  uint8_t i;

  if (enable != 0U)
  {
    /* 从最暗处起步，避免启动瞬间突然全亮 */
    s_tick  = 0U;
    s_level = 0U;
    for (i = 0U; i < s_count; i++)
    {
      s_channel[i].port->BSRR = s_channel[i].bsrr_off;
    }

    TIM7->SR = 0U;                       /* 清掉停机期间可能落下的标志 */
    s_enabled = 1U;
    TIM7->CR1 |= TIM_CR1_CEN;            /* 启动计数 */
  }
  else
  {
    TIM7->CR1 &= ~TIM_CR1_CEN;           /* 停计数，中断不再产生 */
    s_enabled = 0U;
    s_level   = 0U;

    /* 停止后引脚不再被 PWM 驱动，统一置为"熄灭"，交还给普通 GPIO 输出 */
    for (i = 0U; i < s_count; i++)
    {
      s_channel[i].port->BSRR = s_channel[i].bsrr_off;
    }
  }
}

/**
  * @brief  查询 PWM 是否启动
  */
uint8_t SoftPwm_IsEnabled(void)
{
  return s_enabled;
}

/**
  * @brief  设置占空比（所有通道统一）
  */
void SoftPwm_SetDuty(uint8_t duty)
{
  if (duty > SOFTPWM_STEPS)
  {
    duty = SOFTPWM_STEPS;
  }

  s_duty = duty;      /* 单字节写入是原子的，中断里读到的必然是新值或旧值 */
}

/**
  * @brief  查询当前占空比
  */
uint8_t SoftPwm_GetDuty(void)
{
  return s_duty;
}

/**
  * @brief  软件 PWM 滴答处理（在 TIM7 更新中断里调用）
  */
void SoftPwm_ISR(void)
{
  uint8_t level;
  uint8_t i;

  if (s_enabled == 0U)
  {
    return;
  }

  /* 计数器在 0..SOFTPWM_STEPS-1 之间循环 */
  s_tick++;
  if (s_tick >= SOFTPWM_STEPS)
  {
    s_tick = 0U;
  }

  /* 小于占空比 -> 点亮，否则熄灭 */
  level = (s_tick < s_duty) ? 1U : 0U;

  /* 只在电平变化时写端口，一个周期只翻转 2 次 */
  if (level != s_level)
  {
    s_level = level;

    for (i = 0U; i < s_count; i++)
    {
      s_channel[i].port->BSRR = (level != 0U) ? s_channel[i].bsrr_on
                                              : s_channel[i].bsrr_off;
    }
  }
}

/**
  * @brief  TIM7 更新中断服务函数
  * @note   放在本模块内，让软件 PWM 驱动自身闭环，不必改 stm32f4xx_it.c。
  *         若日后用 CubeMX 配置了 TIM7，CubeMX 会在 stm32f4xx_it.c 里再生成
  *         一个同名函数，届时需删掉其中一个，否则链接报重复定义。
  */
void TIM7_IRQHandler(void)
{
  if ((TIM7->SR & TIM_SR_UIF) != 0U)
  {
    TIM7->SR = (uint16_t)(~TIM_SR_UIF);   /* 写 0 清更新标志 */
    SoftPwm_ISR();
  }
}

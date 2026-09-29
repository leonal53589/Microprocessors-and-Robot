/**
  ******************************************************************************
  * @file    bsp_tim.c
  * @brief   定时器驱动层
  *
  *   1) TIM6  基本定时器  -> 1ms 时间基准（接管 HAL 时基，中断里只计数+置标志）
  *   2) TIM3  通用定时器  -> CH2/CH3/CH4 三路 PWM（三色呼吸灯，1kHz）
  *   3) TIM4  通用定时器  -> CH3 PWM 输出（自测仪信号源）
  *   4) TIM3  通用定时器  -> CH2 单通道双边沿输入捕获（自测仪测量端）
  *   5) TIM1  高级定时器  -> CH1 PWM + RCR + OPM（N 次闪光），BKIN 硬件急停
  *
  *  定时器时钟：APB1 定时器 = 90MHz，APB2 定时器 = 180MHz
  ******************************************************************************
  */

#include "bsp_tim.h"
#include "pinmap.h"

/* ==========================================================================
 * 1. TIM6 —— 1ms 时间基准（接管 HAL 时基）
 * ========================================================================== */

volatile uint8_t g_tick_flag_100ms = 0U;
volatile uint8_t g_tick_flag_500ms = 0U;

static uint16_t s_tick_div = 0U;

/**
  * @brief  覆盖 HAL 的 __weak HAL_InitTick：用 TIM6 而不是 SysTick 做 1ms 时基
  * @note   HAL_Init() 和 HAL_RCC_ClockConfig() 都会调用本函数，所以这里按
  *         "当前实际的 APB1 定时器时钟" 计算分频，切换主频后自动跟着变。
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  RCC_ClkInitTypeDef clkconfig;
  uint32_t           pFLatency;
  uint32_t           uwTimclock;
  uint32_t           uwAPB1Prescaler;

  HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

  uwAPB1Prescaler = clkconfig.APB1CLKDivider;
  uwTimclock      = HAL_RCC_GetPCLK1Freq();
  if (uwAPB1Prescaler != RCC_HCLK_DIV1)
  {
    /* APB 预分频不为 1 时，定时器时钟 = PCLKx x2 */
    uwTimclock *= 2U;
  }

  __HAL_RCC_TIM6_CLK_ENABLE();

  /* 计数频率 1MHz -> ARR = 999 时每 1000 次溢出 = 1ms */
  TIM6->PSC  = (uint32_t)((uwTimclock / 1000000U) - 1U);
  TIM6->ARR  = (uint32_t)((1000000U / (1000U / TICK_MS)) - 1U);
  TIM6->EGR  = TIM_EGR_UG;
  TIM6->SR   = 0U;
  TIM6->DIER = TIM_DIER_UIE;

  HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0U);
  HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);

  TIM6->CR1 |= TIM_CR1_CEN;

  uwTickFreq = HAL_TICK_FREQ_DEFAULT;
  return HAL_OK;
}

/**
  * @brief  TIM6 更新中断：中断里只做"计数 + 置标志"
  */
void TIM6_DAC_IRQHandler(void)
{
  if ((TIM6->SR & TIM_SR_UIF) != 0U)
  {
    TIM6->SR = (uint32_t)~TIM_SR_UIF;

    HAL_IncTick();                       /* 毫秒计数 */

    s_tick_div++;
    if (s_tick_div >= 100U)
    {
      s_tick_div = 0U;
      g_tick_flag_100ms = 1U;
      if ((HAL_GetTick() % 500U) < 100U)
      {
        g_tick_flag_500ms = 1U;
      }
    }
  }
}

/* ==========================================================================
 * 2. TIM3 —— 三色 PWM（通用定时器，三个通道共用 PSC/ARR）
 *      TIM3 时钟 90MHz：PSC = 89 -> 1MHz，ARR = 999 -> 1kHz
 *      CH2 = PB5(蓝)  CH3 = PB0(绿, 板载DS1)  CH4 = PB1(红, 板载DS0)
 *      所有 LED 低电平点亮：亮度 = 1 - 占空比
 * ========================================================================== */

static uint16_t rgb_clamp(uint16_t v)
{
  return (v > RGB_PWM_FULL) ? RGB_PWM_FULL : v;
}

void BSP_Rgb_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM3;

  gpio.Pin = RGB_B_PIN; HAL_GPIO_Init(RGB_B_PORT, &gpio);
  gpio.Pin = RGB_G_PIN; HAL_GPIO_Init(RGB_G_PORT, &gpio);
  gpio.Pin = RGB_R_PIN; HAL_GPIO_Init(RGB_R_PORT, &gpio);

  /* 若之前跑过输入捕获，这里要彻底关掉它的中断 */
  HAL_NVIC_DisableIRQ(TIM3_IRQn);
  TIM3->DIER = 0U;

  TIM3->CR1  = 0U;
  /* 目标 1kHz，且 ARR+1 = RGB_PWM_FULL = 1000：
   *   计数频率 = f_pwm x (ARR+1) = 1000 x 1000 = 1MHz
   *   PSC = 90MHz / 1MHz - 1 = 89，ARR = 999
   *   （注意别写成 90MHz/(1000x1)，那会得到 1Hz 而不是 1kHz） */
  TIM3->PSC  = (uint32_t)((90000000UL /
                           ((uint32_t)RGB_PWM_FREQ_HZ * (uint32_t)RGB_PWM_FULL)) - 1UL);
  TIM3->ARR  = RGB_PWM_FULL - 1U;                    /* 1000 计数 -> 1ms -> 1kHz */

  /* CH2/CH3/CH4 都是 PWM 模式 1 + 预装载使能 */
  TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_CC2S | TIM_CCMR1_OC2M))
              | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2PE;
  TIM3->CCMR2 = (TIM3->CCMR2 & ~(TIM_CCMR2_OC3M | TIM_CCMR2_OC4M))
              | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE
              | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;

  TIM3->CCER = TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;  /* 高电平有效 */

  TIM3->CCR2 = RGB_PWM_FULL;      /* 先全灭 */
  TIM3->CCR3 = RGB_PWM_FULL;
  TIM3->CCR4 = RGB_PWM_FULL;
  TIM3->CCR1 = 0U;

  TIM3->EGR = TIM_EGR_UG;
  TIM3->SR  = 0U;
}

void BSP_Rgb_Set(uint16_t r, uint16_t g, uint16_t b)
{
  r = rgb_clamp(r);
  g = rgb_clamp(g);
  b = rgb_clamp(b);

  /* 低电平点亮 -> 亮度越大，CCR 越小 */
  TIM3->CCR4 = (uint32_t)(RGB_PWM_FULL - r);   /* CH4 -> PB1 红 */
  TIM3->CCR3 = (uint32_t)(RGB_PWM_FULL - g);   /* CH3 -> PB0 绿 */
  TIM3->CCR2 = (uint32_t)(RGB_PWM_FULL - b);   /* CH2 -> PB5 蓝 */
}

void BSP_Rgb_Start(void)
{
  TIM3->CR1 |= TIM_CR1_CEN;
}

void BSP_Rgb_Stop(void)
{
  BSP_Rgb_Set(0U, 0U, 0U);          /* 三个通道输出全高 -> LED 全灭 */
  TIM3->CR1 &= ~TIM_CR1_CEN;
}

/* ==========================================================================
 * 3. TIM4 —— PWM 输出（自测仪信号源）
 *      TIM4 时钟 90MHz。为了让占空比分辨率尽量高，取 ARR+1 接近 10000：
 *        100Hz  -> PSC = 89,  ARR = 9999   (1MHz / 100Hz)
 *        1kHz   -> PSC = 8,   ARR = 9999   (10MHz / 1kHz)
 *        10kHz  -> PSC = 0,   ARR = 8999   (90MHz / 10kHz)
 * ========================================================================== */

void BSP_SigOut_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_TIM4_CLK_ENABLE();

  gpio.Pin       = SIG_OUT_PIN;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(SIG_OUT_PORT, &gpio);

  TIM4->CR1   = 0U;
  TIM4->CCMR2 = (TIM4->CCMR2 & ~TIM_CCMR2_OC3M)
              | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE;
  TIM4->CCER  = TIM_CCER_CC3E;
  TIM4->EGR   = TIM_EGR_UG;
  TIM4->SR    = 0U;

  BSP_SigOut_Set(1000U, 50U);
}

void BSP_SigOut_Set(uint32_t freq_hz, uint8_t duty_pct)
{
  uint32_t psc, tick, arr;

  if (freq_hz == 0U || freq_hz > 100000U) { return; }
  if (duty_pct > 100U) { duty_pct = 100U; }

  /* 让 ARR+1 尽量接近 10000，以取得高占空比分辨率 */
  psc = 90000000UL / (10000UL * freq_hz);
  if (psc > 0U) { psc -= 1U; }
  if (psc > 0xFFFFU) { psc = 0xFFFFU; }

  tick = 90000000UL / (psc + 1UL);
  arr  = (tick / freq_hz);
  if (arr == 0U) { arr = 1U; }
  arr -= 1U;
  if (arr > 0xFFFFU) { arr = 0xFFFFU; }

  TIM4->CR1 &= ~TIM_CR1_CEN;
  TIM4->PSC  = psc;
  TIM4->ARR  = arr;
  TIM4->CCR3 = (uint32_t)(((arr + 1UL) * (uint32_t)duty_pct) / 100UL);
  TIM4->EGR  = TIM_EGR_UG;          /* 把 PSC/ARR 预装载值搬进影子寄存器 */
  TIM4->SR   = 0U;
}

void BSP_SigOut_Start(void)
{
  TIM4->CR1 |= TIM_CR1_CEN;
}

void BSP_SigOut_Stop(void)
{
  TIM4->CR1 &= ~TIM_CR1_CEN;
}

/* ==========================================================================
 * 4. TIM3_CH2 —— 单通道双边沿输入捕获（自测仪测量端）
 *      TIM3 时钟 90MHz，PSC = 89 -> 1MHz（1us 分辨率）
 *      量程：周期 <= 65535us -> 频率 >= 15.3Hz
 *      上升沿记周期起点，下降沿记高电平时间 -> 同时得到频率和占空比
 * ========================================================================== */

static volatile uint16_t s_cap_rise    = 0U;   /* 上升沿时刻（计数器值） */
static volatile uint16_t s_cap_period  = 0U;   /* 周期计数值 */
static volatile uint16_t s_cap_high    = 0U;   /* 高电平计数值 */
static volatile uint8_t  s_cap_wait_rise = 1U; /* 1 = 正在等上升沿 */
static volatile uint8_t  s_cap_have_rise = 0U; /* 已经有过一个上升沿 */
static volatile uint8_t  s_cap_period_ok = 0U;
static volatile uint8_t  s_cap_high_ok   = 0U;
static volatile uint32_t s_cap_last_ms   = 0U;

void BSP_Cap_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* 先把 TIM3 从 PWM 状态里彻底拿出来 */
  TIM3->CR1  = 0U;
  TIM3->CCER = 0U;
  TIM3->DIER = 0U;
  TIM3->SR   = 0U;

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  /* 注意：捕获脚配置为【复用功能输入】，不是推挽输出，
   *       所以可以安全地用杜邦线和 PWM 输出脚直连。
   *       下拉是为了在拔掉跳线后电平稳定为低，便于判定"无信号"。 */
  gpio.Pin       = SIG_CAP_PIN;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_PULLDOWN;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(SIG_CAP_PORT, &gpio);

  TIM3->PSC   = (uint32_t)((90000000UL / 1000000UL) - 1UL);  /* -> 1MHz */
  TIM3->ARR   = 0xFFFFU;
  TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_CC2S | TIM_CCMR1_IC2F | TIM_CCMR1_IC2PSC))
              | TIM_CCMR1_CC2S_0;                  /* IC2 映射到 TI2 */
  TIM3->CCER  = TIM_CCER_CC2E;                     /* 上升沿捕获 */
  TIM3->DIER  = TIM_DIER_CC2IE;
  TIM3->EGR   = TIM_EGR_UG;
  TIM3->SR    = 0U;

  s_cap_rise      = 0U;
  s_cap_period    = 0U;
  s_cap_high      = 0U;
  s_cap_wait_rise = 1U;
  s_cap_have_rise = 0U;
  s_cap_period_ok = 0U;
  s_cap_high_ok   = 0U;
  s_cap_last_ms   = HAL_GetTick();

  HAL_NVIC_SetPriority(TIM3_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(TIM3_IRQn);

  TIM3->CR1 = TIM_CR1_CEN;
}

void BSP_Cap_DeInit(void)
{
  TIM3->CR1  = 0U;
  TIM3->CCER = 0U;
  TIM3->DIER = 0U;
  TIM3->SR   = 0U;
  HAL_NVIC_DisableIRQ(TIM3_IRQn);
}

/**
  * @brief  TIM3 捕获中断：中断里只记录时间戳，不做任何显示/串口输出
  */
void TIM3_IRQHandler(void)
{
  if ((TIM3->SR & TIM_SR_CC2IF) != 0U)
  {
    uint16_t ccr = (uint16_t)TIM3->CCR2;

    TIM3->SR = (uint32_t)~TIM_SR_CC2IF;

    if (s_cap_wait_rise != 0U)
    {
      /* 上升沿 */
      if (s_cap_have_rise != 0U)
      {
        s_cap_period    = (uint16_t)(ccr - s_cap_rise);   /* 16 位减法自然处理回绕 */
        s_cap_period_ok = 1U;
      }
      s_cap_rise      = ccr;
      s_cap_have_rise = 1U;
      s_cap_wait_rise = 0U;
      TIM3->CCER |= TIM_CCER_CC2P;                        /* 改为下降沿 */
    }
    else
    {
      /* 下降沿 */
      s_cap_high    = (uint16_t)(ccr - s_cap_rise);
      s_cap_high_ok = 1U;
      s_cap_wait_rise = 1U;
      TIM3->CCER &= ~TIM_CCER_CC2P;                       /* 改回上升沿 */
    }

    s_cap_last_ms = HAL_GetTick();
  }
}

/**
  * @brief  读取捕获结果
  * @retval 1 = 有有效信号；0 = 无信号（超时）或数据还没凑齐
  */
uint8_t BSP_Cap_Get(uint32_t *freq_hz, uint16_t *duty_permille)
{
  uint16_t period;
  uint16_t high;

  if ((uint32_t)(HAL_GetTick() - s_cap_last_ms) > CAP_TIMEOUT_MS)
  {
    s_cap_period_ok = 0U;              /* 超时：作废旧数据，避免显示上一组 */
    s_cap_high_ok   = 0U;
    return 0U;
  }

  if ((s_cap_period_ok == 0U) || (s_cap_high_ok == 0U))
  {
    return 0U;
  }

  period = s_cap_period;
  high   = s_cap_high;

  if (period == 0U)
  {
    return 0U;
  }

  if (freq_hz != NULL)
  {
    *freq_hz = 1000000UL / (uint32_t)period;              /* 1us 计数 -> Hz */
  }
  if (duty_permille != NULL)
  {
    *duty_permille = (uint16_t)(((uint32_t)high * 1000UL) / (uint32_t)period);
  }

  return 1U;
}

/* ==========================================================================
 * 5. TIM1 —— 高级定时器：N 次闪光 + BKIN 硬件急停
 *      TIM1 时钟 180MHz：PSC = 179 -> 1MHz，ARR = 999 -> 1kHz
 *      CH1  = PE9  外接 LED（低电平点亮：空闲态 OIS1 = 1 -> 引脚高 -> 灭）
 *      BKIN = PE15 外接按键到 GND（低电平触发刹车）
 *
 *      关键位：
 *        RCR  : 重复计数器。边沿对齐下溢出周期 = PWM 周期，
 *               要 N 个脉冲就写 RCR = N-1（第 N 次溢出才产生更新事件）
 *        OPM  : 单脉冲模式。更新事件时硬件自动清 CEN -> 发完 N 个就停
 *        MOE  : 主输出使能。高级定时器不置 1 就没有任何输出
 *        BKIN : 刹车输入。触发后硬件异步清 MOE，输出立刻进入空闲态；
 *               AOE = 0 时不会自动恢复，必须软件重新置 MOE
 * ========================================================================== */

#define FLASH_TIM_CLK_HZ    180000000UL
#define FLASH_TIM_FREQ_HZ   1000UL
#define FLASH_TIM_ARR       999U
#define FLASH_TIM_CCR       500U        /* 50% 占空比 */

static volatile uint32_t s_flash_brake_cnt     = 0U;
static volatile uint8_t  s_flash_brake_latched = 0U;

void BSP_Flash_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_TIM1_CLK_ENABLE();

  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF1_TIM1;

  /* CH1 输出 */
  gpio.Pin  = FLASH_LED_PIN;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(FLASH_LED_PORT, &gpio);

  /* BKIN 输入（内部上拉：不按键为高 = 刹车无效；按下接地 = 触发刹车） */
  gpio.Pin  = FLASH_BKIN_PIN;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(FLASH_BKIN_PORT, &gpio);

  TIM1->CR1   = 0U;
  /* OIS1 = 1：MOE = 0 时 OC1 输出空闲高电平 -> 低电平点亮的 LED 熄灭 */
  TIM1->CR2   = TIM_CR2_OIS1;
  TIM1->PSC   = (uint32_t)((FLASH_TIM_CLK_HZ / (FLASH_TIM_FREQ_HZ * (FLASH_TIM_ARR + 1UL))) - 1UL);
  TIM1->ARR   = FLASH_TIM_ARR;
  TIM1->RCR   = 0U;
  TIM1->CCR1  = FLASH_TIM_CCR;
  TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
  TIM1->CCER  = TIM_CCER_CC1E;                    /* 不用互补通道 */
  /* BKE=1 使能刹车，BKP=0 低电平有效，AOE=0 不自动恢复，OSSR/OSSI 空闲态，MOE=1 */
  TIM1->BDTR  = TIM_BDTR_BKE | TIM_BDTR_OSSR | TIM_BDTR_OSSI | TIM_BDTR_MOE;
  TIM1->DIER  = TIM_DIER_BIE;                     /* 刹车中断使能（BIE 在 DIER，不在 BDTR） */
  TIM1->EGR   = TIM_EGR_UG;
  TIM1->SR    = 0U;

  s_flash_brake_cnt     = 0U;
  s_flash_brake_latched = 0U;

  HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);
}

/**
  * @brief  刹车中断：硬件已经异步关断了输出，这里只记录状态
  */
void TIM1_BRK_TIM9_IRQHandler(void)
{
  if ((TIM1->SR & TIM_SR_BIF) != 0U)
  {
    TIM1->SR = (uint32_t)~TIM_SR_BIF;     /* 写 0 清 BIF */

    TIM1->CR1 &= ~TIM_CR1_CEN;            /* 顺便停掉计数器 */
    s_flash_brake_cnt++;
    s_flash_brake_latched = 1U;
  }
}

/**
  * @brief  启动 N 次闪光
  * @param  n 1..9
  * @retval 0 = 已启动；1 = 刹车未确认，拒绝启动
  */
uint8_t BSP_Flash_Start(uint8_t n)
{
  if (s_flash_brake_latched != 0U)
  {
    return 1U;                            /* 必须先确认恢复 */
  }

  if (n < 1U) { n = 1U; }
  if (n > 9U) { n = 9U; }

  TIM1->CR1 &= ~TIM_CR1_CEN;
  TIM1->SR   = 0U;

  TIM1->RCR  = (uint32_t)(n - 1U);        /* 边沿对齐：RCR = N-1 -> N 个 PWM 周期 */
  TIM1->EGR  = TIM_EGR_UG;                /* 产生更新事件，把 RCR 装进影子寄存器 */
  TIM1->SR   = 0U;

  TIM1->CR1 |= TIM_CR1_OPM;               /* 单脉冲模式：更新事件后硬件自动清 CEN */
  TIM1->BDTR |= TIM_BDTR_MOE;             /* 确保主输出使能 */
  TIM1->CR1 |= TIM_CR1_CEN;

  return 0U;
}

void BSP_Flash_Stop(void)
{
  TIM1->CR1  &= ~TIM_CR1_CEN;
  TIM1->BDTR &= ~TIM_BDTR_MOE;            /* MOE=0 -> 输出进入空闲态 -> LED 灭 */
}

uint8_t BSP_Flash_IsBusy(void)
{
  return ((TIM1->CR1 & TIM_CR1_CEN) != 0U) ? 1U : 0U;
}

uint8_t BSP_Flash_BrakeLatched(void)
{
  return s_flash_brake_latched;
}

uint16_t BSP_Flash_BrakeCount(void)
{
  return (uint16_t)s_flash_brake_cnt;
}

/**
  * @brief  人工确认：清刹车标志并重新使能主输出
  */
void BSP_Flash_ClearBrake(void)
{
  TIM1->CR1 &= ~TIM_CR1_CEN;
  TIM1->SR   = (uint32_t)~TIM_SR_BIF;     /* 清 BIF */
  TIM1->BDTR |= TIM_BDTR_MOE;             /* 重新使能主输出 */
  s_flash_brake_latched = 0U;
}

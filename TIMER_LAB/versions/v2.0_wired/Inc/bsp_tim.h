/**
  ******************************************************************************
  * @file    bsp_tim.h
  * @brief   定时器驱动层（基本 / 通用 / 高级）
  *
  *  定时器时钟（关键！不要拿 CPU 主频当所有定时器输入）：
  *      SYSCLK = 180MHz, AHB = 180MHz
  *      APB1 = 45MHz  ->  APB1 定时器时钟 = 90MHz   (TIM2~7, TIM12~14)
  *      APB2 = 90MHz  ->  APB2 定时器时钟 = 180MHz  (TIM1, TIM8~11)
  *      更新频率 f = f_TIM / ((PSC+1) * (ARR+1))
  ******************************************************************************
  */

#ifndef __BSP_TIM_H
#define __BSP_TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ======================= 1. 基本定时器：1ms 时间基准 ======================= */
/* 用 TIM6 接管 HAL 的时基，这样 HAL_GetTick() 就是 TIM6 的 1ms 计数。
 * TIM6 时钟 90MHz：PSC = 8999, ARR = 9  ->  90e6/9000/10 = 1000Hz      */
#define TICK_MS             (1U)

/* 中断里只做：计数 + 置标志 */
extern volatile uint8_t g_tick_flag_100ms;
extern volatile uint8_t g_tick_flag_500ms;

/* ======================= 2. 通用定时器 TIM3：三色 PWM ===================== */
/* TIM3 时钟 90MHz，目标 1kHz：PSC = 89 -> 1MHz，ARR = 999 */
#define RGB_PWM_FREQ_HZ     1000U
#define RGB_PWM_FULL        1000U       /* 亮度满量程：CCR 0..1000 */

void     BSP_Rgb_Init(void);
void     BSP_Rgb_Set(uint16_t r, uint16_t g, uint16_t b);   /* 0..RGB_PWM_FULL */
void     BSP_Rgb_Start(void);
void     BSP_Rgb_Stop(void);

/* ======================= 3. 通用定时器 TIM4：PWM 输出（自测仪信号源） ===== */
void     BSP_SigOut_Init(void);
void     BSP_SigOut_Set(uint32_t freq_hz, uint8_t duty_pct);
void     BSP_SigOut_Start(void);
void     BSP_SigOut_Stop(void);

/* ======================= 4. 通用定时器 TIM3_CH2：输入捕获（自测仪测量端）= */
/*  单通道双边沿捕获：上升沿记周期起点，下降沿记高电平时间
 *  TIM3 时钟 90MHz，PSC = 89 -> 1MHz（1us 分辨率）
 *  量程：周期 <= 65535us -> 频率 >= 15.3Hz                              */
#define CAP_TIMEOUT_MS      150U        /* 超过该时间无捕获 -> 无信号 */

void     BSP_Cap_Init(void);
void     BSP_Cap_DeInit(void);
uint8_t  BSP_Cap_Get(uint32_t *freq_hz, uint16_t *duty_permille);  /* 1 = 有信号 */

/* ======================= 5. 高级定时器 TIM1：N 次闪光 + BKIN 急停 ========= */
/*  TIM1 时钟 180MHz，PWM 1kHz：PSC = 179 -> 1MHz，ARR = 999            */
void     BSP_Flash_Init(void);
uint8_t  BSP_Flash_Start(uint8_t n);        /* n = 1..9，返回 0 = 已启动 */
void     BSP_Flash_Stop(void);
uint8_t  BSP_Flash_IsBusy(void);            /* 1 = 还在发脉冲 */
uint8_t  BSP_Flash_BrakeLatched(void);      /* 1 = 急停已锁存 */
void     BSP_Flash_ClearBrake(void);        /* 清刹车标志 + 重新使能 MOE */
uint16_t BSP_Flash_BrakeCount(void);        /* 累计刹车次数（用于观察） */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TIM_H */

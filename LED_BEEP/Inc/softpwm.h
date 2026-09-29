/**
  ******************************************************************************
  * @file    softpwm.h
  * @brief   软件 PWM 驱动：在任意 GPIO 上用定时器中断产生 PWM 波
  *          用于实现"PWM 占空比渐变"的呼吸灯（选做部分）
  ******************************************************************************
  * 一、原理
  * ---------------------------------------------------------------------------
  *  用一个基本定时器 TIM7 产生固定频率的"滴答"中断（20kHz），中断里维护一个
  *  0 .. SOFTPWM_STEPS-1 循环递增的计数器 s_tick：
  *
  *      s_tick <  当前占空比 -> 输出"点亮"电平
  *      s_tick >= 当前占空比 -> 输出"熄灭"电平
  *
  *  于是输出波形的频率 = 20000Hz / 100级 = 200Hz，亮度分辨率 = 1/100 = 1%。
  *  200Hz 远高于人眼闪烁融合频率（约 60~90Hz），因此肉眼看不出闪烁，
  *  只看到亮度随占空比连续变化 —— 这就是 PWM 调光。
  *
  * 二、为什么用软件 PWM，而不是用定时器的硬件 PWM 通道
  * ---------------------------------------------------------------------------
  *  本实验 8 个 LED 接在 PE5 / PE7 / PE8 / PE9 / PE10 / PE11 / PE12 / PE13，
  *  查 STM32F429 数据手册的复用功能(AF)分配表可知：
  *      PE9  -> TIM1_CH1   ┐
  *      PE11 -> TIM1_CH2   ├ 有独立的硬件 PWM 输出通道
  *      PE13 -> TIM1_CH3   │
  *      PE5  -> TIM9_CH1   ┘
  *      PE8  -> TIM1_CH1N  ┐ 只能作 TIM1 的互补输出
  *      PE10 -> TIM1_CH2N  ├ （必须开互补输出，且与 CHx 反相）
  *      PE12 -> TIM1_CH3N  ┘
  *      PE7  -> TIM1_ETR     只有"外部触发输入"，压根没有输出通道
  *
  *  也就是说：纯硬件 PWM 最多只能覆盖 7 个灯，PE7(LED5) 永远做不到；
  *  而且互补输出天生与主通道反相、还受死区发生器影响，8 个灯无法做到同步。
  *  软件 PWM 对引脚没有任何外设要求，8 个灯可以完全同步呼吸，
  *  并且一根线都不用改 —— 所以选做部分采用本方案。
  *
  * 三、分辨率与时间参数
  * ---------------------------------------------------------------------------
  *  APB1 = 45MHz，APB1 分频系数 = 4（≠1）-> TIM7 计数时钟 = 90MHz
  *      PSC = 8   -> 90MHz / (8+1)  = 10MHz
  *      ARR = 499 -> 10MHz / (499+1) = 20kHz  （滴答中断频率）
  *      STEPS = 100 -> PWM 频率 = 20kHz / 100 = 200Hz
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SOFTPWM_H
#define __SOFTPWM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Exported constants --------------------------------------------------------*/
/** 滴答中断频率(Hz)：20kHz */
#define SOFTPWM_TICK_HZ     20000U

/** 一个 PWM 周期的级数，也等于亮度分辨率（100 级 -> 1% 一档） */
#define SOFTPWM_STEPS       100U

/** 最大支持的通道数（本工程用 8 个 LED） */
#define SOFTPWM_MAX_CH      8U

/** TIM7 分频与重装值：90MHz/(8+1)/(499+1) = 20kHz */
#define SOFTPWM_TIM_PSC     8U
#define SOFTPWM_TIM_ARR     499U

/* Exported types ------------------------------------------------------------*/
/**
  * @brief 软件 PWM 通道描述：端口 / 引脚 / 点亮时的电平
  */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  GPIO_PinState active_state;   /* 输出"点亮"时该引脚的电平 */
} SoftPwmChannel;

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化软件 PWM：登记通道并配置 TIM7 时基中断
  * @param  channels 通道表（数组首地址）
  * @param  count    通道个数（> SOFTPWM_MAX_CH 时按最大值截断）
  * @note   调用前各引脚的 GPIO 必须已配置为推挽输出。
  *         初始化后 PWM 处于"停止"状态，需再调用 SoftPwm_Enable(1) 才输出。
  */
void SoftPwm_Init(const SoftPwmChannel *channels, uint8_t count);

/**
  * @brief  启动 / 停止 PWM 输出
  * @param  enable 1 = 启动，0 = 停止
  * @note   停止时会把所有通道强制置为"熄灭"电平，避免残留亮灯。
  */
void SoftPwm_Enable(uint8_t enable);

/**
  * @brief  查询 PWM 当前是否处于启动状态
  * @retval 1 = 启动，0 = 停止
  */
uint8_t SoftPwm_IsEnabled(void);

/**
  * @brief  设置占空比（所有通道统一）
  * @param  duty 0 .. SOFTPWM_STEPS
  *              0 = 全灭，SOFTPWM_STEPS = 全亮，中间值按比例调光
  * @note   只改占空比、不重配定时器，可在中断里安全调用（写一个字节，原子）。
  */
void SoftPwm_SetDuty(uint8_t duty);

/**
  * @brief  查询当前占空比
  * @retval 0 .. SOFTPWM_STEPS
  */
uint8_t SoftPwm_GetDuty(void);

/**
  * @brief  软件 PWM 的"滴答"处理，必须在 TIM7 更新中断里调用
  */
void SoftPwm_ISR(void);

#ifdef __cplusplus
}
#endif

#endif /* __SOFTPWM_H */

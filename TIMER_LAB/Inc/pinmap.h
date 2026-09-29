/**
  ******************************************************************************
  * @file    pinmap.h
  * @brief   TIMER_LAB 全工程引脚分配（唯一事实来源）
  *
  *  板卡：正点原子阿波罗 V2 STM32F429（STM32F429IGTx, LQFP176）
  *        配套 DNF429-EB 嵌入式学习实验箱
  *  依据：《阿波罗V2 硬件参考手册》MCU 引脚分配表
  *        《STM32F429 阿波罗开发指南》第 19/20/21/23 章
  *
  *  ⚠️ 所有外接 LED 统一为【低电平点亮】：正极经 1K 限流电阻接 3.3V，负极接引脚
  ******************************************************************************
  */

#ifndef __PINMAP_H
#define __PINMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ==========================================================================
 * 0. 板载资源（无需接线）
 * ========================================================================== */

/* 4 个板载按键 */
#define KEY0_PORT       GPIOH
#define KEY0_PIN        GPIO_PIN_3      /* 低电平有效 */
#define KEY1_PORT       GPIOH
#define KEY1_PIN        GPIO_PIN_2      /* 低电平有效 */
#define KEY2_PORT       GPIOC
#define KEY2_PIN        GPIO_PIN_13     /* 低电平有效 */
#define KEYUP_PORT      GPIOA
#define KEYUP_PIN       GPIO_PIN_0      /* 高电平有效（WK_UP） */

/* 板载有源蜂鸣器：PCF8574 的 P0（低电平响），见 pcf8574.c */

/* ==========================================================================
 * 1. OLED（SSD1306 128x64，直接对插 P6）
 *
 *  官方 8080 并口映射（开发指南 23.2 节）：
 *      OLED_CS  = DCMI_VSYNC = PB7      OLED_RS  = DCMI_SCL  = PB4
 *      OLED_WR  = DCMI_HREF  = PH8      OLED_RD  = DCMI_SDA  = PB3
 *      OLED_RST = DCMI_RESET = PA15
 *      D0..D7   = PC6 PC7 PC8 PC9 PC11 PD3 PB8 PB9
 *
 *  4 线 SPI 模式（同一连接器，模块 BS1/BS2 切换）：
 *      SCLK=D0=PC6   SDIN=D1=PC7   DC(=RS)=PB4   CS=PB7   RST=PA15
 *
 *  IIC 模式（模块 BS1/BS2 切换）：
 *      SCL=D0=PC6    SDA=D1=PC7    RST=PA15      地址 0x78
 * ========================================================================== */
#define OLED_IF_8080    0
#define OLED_IF_SPI     1
#define OLED_IF_IIC     2

#ifndef OLED_IF_MODE
#define OLED_IF_MODE    OLED_IF_8080
#endif

#define OLED_CS_PORT    GPIOB
#define OLED_CS_PIN     GPIO_PIN_7
#define OLED_RS_PORT    GPIOB           /* 也称 DC */
#define OLED_RS_PIN     GPIO_PIN_4
#define OLED_WR_PORT    GPIOH           /* 仅 8080 模式用 */
#define OLED_WR_PIN     GPIO_PIN_8
#define OLED_RD_PORT    GPIOB           /* 仅 8080 模式用 */
#define OLED_RD_PIN     GPIO_PIN_3
#define OLED_RST_PORT   GPIOA
#define OLED_RST_PIN    GPIO_PIN_15

/* 8080 数据总线 D0~D7；SPI/IIC 模式下只有 D0/D1 有用 */
#define OLED_D0_PORT    GPIOC
#define OLED_D0_PIN     GPIO_PIN_6
#define OLED_D1_PORT    GPIOC
#define OLED_D1_PIN     GPIO_PIN_7
#define OLED_D2_PORT    GPIOC
#define OLED_D2_PIN     GPIO_PIN_8
#define OLED_D3_PORT    GPIOC
#define OLED_D3_PIN     GPIO_PIN_9
#define OLED_D4_PORT    GPIOC
#define OLED_D4_PIN     GPIO_PIN_11
#define OLED_D5_PORT    GPIOD
#define OLED_D5_PIN     GPIO_PIN_3
#define OLED_D6_PORT    GPIOB
#define OLED_D6_PIN     GPIO_PIN_8
#define OLED_D7_PORT    GPIOB
#define OLED_D7_PIN     GPIO_PIN_9

/* ==========================================================================
 * 2. 实验二：随机反应速度测试器
 *     1ms 基准 = TIM6（基本定时器，无引脚）
 *     提示 LED = 板载 DS0（PB1，红色）—— 低电平点亮，不用接任何线就能看到
 *              同时并联驱动 PE10（外接 LED 可选，想接就并一个）
 *
 *  注意：PB1 在实验三里是 TIM3_CH4（红色 PWM），两者互斥；
 *        所以进入实验二时要把它重新配成普通推挽输出（见 app.c 的 react_led_init）
 * ========================================================================== */
#define REACT_LED_PORT      GPIOB
#define REACT_LED_PIN       GPIO_PIN_1      /* 板载 DS0 红色 LED */
#define REACT_LED_EXT_PORT  GPIOE
#define REACT_LED_EXT_PIN   GPIO_PIN_10     /* 外接 LED（可选） */

/* ==========================================================================
 * 3. 实验三：三色呼吸灯（TIM3 通用定时器，三个 PWM 通道，1kHz）
 *     CH2 = PB5（外接蓝色 LED）
 *     CH3 = PB0（板载 DS1 绿色 LED，也可外接）
 *     CH4 = PB1（板载 DS0 红色 LED，也可外接）
 * ========================================================================== */
#define RGB_B_PORT      GPIOB
#define RGB_B_PIN       GPIO_PIN_5
#define RGB_G_PORT      GPIOB
#define RGB_G_PIN       GPIO_PIN_0
#define RGB_R_PORT      GPIOB
#define RGB_R_PIN       GPIO_PIN_1

/* ==========================================================================
 * 4. 实验四：频率与占空比自测仪
 *     PWM 输出 = TIM4_CH3 = PD14
 *     捕获输入 = TIM3_CH2 = PB5   （与实验三的蓝灯共用，模式互斥）
 *     跳线：PD14 <--杜邦线--> PB5
 * ========================================================================== */
#define SIG_OUT_PORT    GPIOD
#define SIG_OUT_PIN     GPIO_PIN_14
#define SIG_CAP_PORT    GPIOB
#define SIG_CAP_PIN     GPIO_PIN_5

/* ==========================================================================
 * 5. 实验五：N 次闪光 + 硬件急停（TIM1 高级定时器）
 *     CH1  = PE9 （外接 LED）
 *     BKIN = PB12（外接按键到 GND，低电平触发刹车）
 * ========================================================================== */
#define FLASH_LED_PORT  GPIOE
#define FLASH_LED_PIN   GPIO_PIN_9
#define FLASH_BKIN_PORT GPIOB
#define FLASH_BKIN_PIN  GPIO_PIN_12

#ifdef __cplusplus
}
#endif

#endif /* __PINMAP_H */

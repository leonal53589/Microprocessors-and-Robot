/**
  ******************************************************************************
  * @file    pinmap.h
  * @brief   SMART_HOME 全工程引脚分配（唯一事实来源）
  *
  *  板卡：正点原子阿波罗 V2 STM32F429（STM32F429IGTx, LQFP176）
  *        配套 DNF429-EB 嵌入式学习实验箱
  *        外接：7 寸 800x480 RGB 屏（RGBLCD 接口）+ ATK-OV5640（P6 摄像头接口）
  *
  *  依据：《阿波罗V2 STM32F429开发板IO引脚分配表》（guide 目录，逐脚核对）
  *        《阿波罗V2 硬件参考手册》《STM32F429 阿波罗开发指南》
  *        第 13/14/19/25/26/31/32/45/50 章
  *
  *  ★ 与实验一（LED_BEEP）、实验二（TIMER_LAB）最大的不同：
  *    本实验要把 FMC（SDRAM 帧缓冲）、LTDC（RGB 屏）、DCMI（摄像头）三套总线
  *    同时开起来，因此下面这些引脚【不能】再像前两个实验那样随手借用：
  *        全部 FMC 数据/地址/控制脚、全部 LTDC 数据/时序脚、全部 DCMI 脚、
  *        PA5（ADC）、PB12（单总线）、PB0/PB1（板载 LED）、PH2/PH3/PC13/PA0（按键）
  *    凡是本文件里出现的脚，其它模块都不许再改它的 MODER/AFR。
  *
  *  可独立使用的引脚（未接外设、未引出到总线）在本工程里只剩下按键、LED 和
  *  PB6/PB10 之类的少量脚，这也是本实验"资源核对"的难点所在。
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
 *    IO 分配表：PA0=WK_UP、PH2=KEY1、PH3=KEY0、PC13=KEY2
 * ========================================================================== */

/* 4 个板载按键（与实验二完全一致，方便沿用按键习惯） */
#define KEY0_PORT       GPIOH
#define KEY0_PIN        GPIO_PIN_3      /* 低电平有效，靠左第一个按键 K0  */
#define KEY1_PORT       GPIOH
#define KEY1_PIN        GPIO_PIN_2      /* 低电平有效 K1                   */
#define KEY2_PORT       GPIOC
#define KEY2_PIN        GPIO_PIN_13     /* 低电平有效 K2                   */
#define KEYUP_PORT      GPIOA
#define KEYUP_PIN       GPIO_PIN_0      /* 高电平有效（WK_UP）             */

/* 板载 LED（IO 分配表：PB0=LED1 绿 DS1，PB1=LED0 红 DS0）
 * 硬件参考手册 LED 原理图：VCC3.3 --[R65/R62 510R]-- LED -- MCU 引脚，
 * MCU 灌电流，所以【输出低电平点亮】。 */
#define LED_GREEN_PORT  GPIOB
#define LED_GREEN_PIN   GPIO_PIN_0      /* DS1 绿色，正常状态常亮 */
#define LED_RED_PORT    GPIOB
#define LED_RED_PIN     GPIO_PIN_1      /* DS0 红色，越限 2Hz 闪烁 / 故障常亮 */
#define LED_ON_LEVEL    GPIO_PIN_RESET  /* 低电平点亮 */
#define LED_OFF_LEVEL   GPIO_PIN_SET

/* ==========================================================================
 * 1. 7 寸 800x480 RGB 屏：LTDC 引脚
 *    IO 分配表"RGBLCD接口"列 + 数据手册 AF14(LTDC)
 *
 *    注意低 3 位（R0~R2、G0~G1、B0~B2）板上没有引出：面板只接了高 5/6/5 位，
 *    正好对应 LTDC 的 RGB565 像素格式（R[7:3] G[7:2] B[7:3]），所以本工程用
 *    RGB565 是最自然、也最省 SDRAM 带宽的选择（750KiB/帧而不是 1.5MiB）。
 * ========================================================================== */
#define LCD_AF                  GPIO_AF14_LTDC

#define LCD_R3_PORT     GPIOH
#define LCD_R3_PIN      GPIO_PIN_9
#define LCD_R4_PORT     GPIOH
#define LCD_R4_PIN      GPIO_PIN_10
#define LCD_R5_PORT     GPIOH
#define LCD_R5_PIN      GPIO_PIN_11
#define LCD_R6_PORT     GPIOH
#define LCD_R6_PIN      GPIO_PIN_12
#define LCD_R7_PORT     GPIOG
#define LCD_R7_PIN      GPIO_PIN_6

#define LCD_G2_PORT     GPIOH
#define LCD_G2_PIN      GPIO_PIN_13
#define LCD_G3_PORT     GPIOH
#define LCD_G3_PIN      GPIO_PIN_14
#define LCD_G4_PORT     GPIOH
#define LCD_G4_PIN      GPIO_PIN_15
#define LCD_G5_PORT     GPIOI
#define LCD_G5_PIN      GPIO_PIN_0
#define LCD_G6_PORT     GPIOI
#define LCD_G6_PIN      GPIO_PIN_1
#define LCD_G7_PORT     GPIOI
#define LCD_G7_PIN      GPIO_PIN_2

#define LCD_B3_PORT     GPIOG
#define LCD_B3_PIN      GPIO_PIN_11
#define LCD_B4_PORT     GPIOI
#define LCD_B4_PIN      GPIO_PIN_4
#define LCD_B5_PORT     GPIOI
#define LCD_B5_PIN      GPIO_PIN_5
#define LCD_B6_PORT     GPIOI
#define LCD_B6_PIN      GPIO_PIN_6
#define LCD_B7_PORT     GPIOI
#define LCD_B7_PIN      GPIO_PIN_7

#define LCD_CLK_PORT    GPIOG
#define LCD_CLK_PIN     GPIO_PIN_7       /* LTDC_CLK  */
#define LCD_DE_PORT     GPIOF
#define LCD_DE_PIN      GPIO_PIN_10      /* LTDC_DE   */
#define LCD_VSYNC_PORT  GPIOI
#define LCD_VSYNC_PIN   GPIO_PIN_9       /* LTDC_VSYNC*/
#define LCD_HSYNC_PORT  GPIOI
#define LCD_HSYNC_PIN   GPIO_PIN_10      /* LTDC_HSYNC*/

/* 背光控制：PB5（IO 分配表：PB5=LCD_BL，TFTLCD/RGBLCD 接口背光脚）
 * 高电平点亮背光。注意实验二把 PB5 当 TIM3_CH2 用，本工程 PB5 只做背光。 */
#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_PIN_5
#define LCD_BL_ON       GPIO_PIN_SET
#define LCD_BL_OFF      GPIO_PIN_RESET

/* 800x480 面板时序（水平/垂直同步宽度、前后沿）与 LTDC 时钟
 * 面板给出典型像素时钟 30MHz；如换屏只需改这一组宏。
 *   行总长 = 800 + 48 + 88 + 40 = 976
 *   场总长 = 480 +  3 + 32 + 13 = 528
 *   帧率   = 30MHz / (976*528) ≈ 58.2Hz
 */
#define LCD_WIDTH               800U
#define LCD_HEIGHT              480U
#define LCD_HSW                 48U
#define LCD_HBP                 88U
#define LCD_HFP                 40U
#define LCD_VSW                 3U
#define LCD_VBP                 32U
#define LCD_VFP                 13U
/* PLLSAI: HSE/PLLM = 1MHz 输入, VCO = 1MHz * 240 = 240MHz,
 *         /PLLSAIR(4) = 60MHz, 再 /PLLSAIDivR(2) = 30MHz 作为 LTDC 时钟 */
#define LCD_PLLSAI_N            240U
#define LCD_PLLSAI_R            4U
#define LCD_PLLSAI_Q            8U
#define LCD_PLLSAIDIVR          RCC_PLLSAIDIVR_2

/* ==========================================================================
 * 2. 外部 SDRAM：FMC 引脚（W9825G6KH，32MB，16 位，4 Bank x 13 行 x 9 列）
 *    IO 分配表里凡标注 "FMC_xxx(LCD/SDRAM/NAND 共用)" 的都在这里，AF12
 *
 *    帧缓冲必须放在 SDRAM：800x480 RGB565 单帧 = 750KiB，
 *    而 F429 片内 SRAM 一共只有 256KiB（含 64KiB CCM），放不下。
 * ========================================================================== */
#define SDRAM_AF                GPIO_AF12_FMC

/* 数据总线 D0~D15 */
#define FMC_D0_PORT     GPIOD
#define FMC_D0_PIN      GPIO_PIN_14
#define FMC_D1_PORT     GPIOD
#define FMC_D1_PIN      GPIO_PIN_15
#define FMC_D2_PORT     GPIOD
#define FMC_D2_PIN      GPIO_PIN_0
#define FMC_D3_PORT     GPIOD
#define FMC_D3_PIN      GPIO_PIN_1
#define FMC_D4_PORT     GPIOE
#define FMC_D4_PIN      GPIO_PIN_7
#define FMC_D5_PORT     GPIOE
#define FMC_D5_PIN      GPIO_PIN_8
#define FMC_D6_PORT     GPIOE
#define FMC_D6_PIN      GPIO_PIN_9
#define FMC_D7_PORT     GPIOE
#define FMC_D7_PIN      GPIO_PIN_10
#define FMC_D8_PORT     GPIOE
#define FMC_D8_PIN      GPIO_PIN_11
#define FMC_D9_PORT     GPIOE
#define FMC_D9_PIN      GPIO_PIN_12
#define FMC_D10_PORT    GPIOE
#define FMC_D10_PIN     GPIO_PIN_13
#define FMC_D11_PORT    GPIOE
#define FMC_D11_PIN     GPIO_PIN_14
#define FMC_D12_PORT    GPIOE
#define FMC_D12_PIN     GPIO_PIN_15
#define FMC_D13_PORT    GPIOD
#define FMC_D13_PIN     GPIO_PIN_8
#define FMC_D14_PORT    GPIOD
#define FMC_D14_PIN     GPIO_PIN_9
#define FMC_D15_PORT    GPIOD
#define FMC_D15_PIN     GPIO_PIN_10

/* 地址总线 A0~A12 */
#define FMC_A0_PORT     GPIOF
#define FMC_A0_PIN      GPIO_PIN_0
#define FMC_A1_PORT     GPIOF
#define FMC_A1_PIN      GPIO_PIN_1
#define FMC_A2_PORT     GPIOF
#define FMC_A2_PIN      GPIO_PIN_2
#define FMC_A3_PORT     GPIOF
#define FMC_A3_PIN      GPIO_PIN_3
#define FMC_A4_PORT     GPIOF
#define FMC_A4_PIN      GPIO_PIN_4
#define FMC_A5_PORT     GPIOF
#define FMC_A5_PIN      GPIO_PIN_5
#define FMC_A6_PORT     GPIOF
#define FMC_A6_PIN      GPIO_PIN_12
#define FMC_A7_PORT     GPIOF
#define FMC_A7_PIN      GPIO_PIN_13
#define FMC_A8_PORT     GPIOF
#define FMC_A8_PIN      GPIO_PIN_14
#define FMC_A9_PORT     GPIOF
#define FMC_A9_PIN      GPIO_PIN_15
#define FMC_A10_PORT    GPIOG
#define FMC_A10_PIN     GPIO_PIN_0
#define FMC_A11_PORT    GPIOG
#define FMC_A11_PIN     GPIO_PIN_1
#define FMC_A12_PORT    GPIOG
#define FMC_A12_PIN     GPIO_PIN_2

/* Bank 地址线与字节掩码 */
#define FMC_BA0_PORT    GPIOG
#define FMC_BA0_PIN     GPIO_PIN_4
#define FMC_BA1_PORT    GPIOG
#define FMC_BA1_PIN     GPIO_PIN_5
#define FMC_NBL0_PORT   GPIOE
#define FMC_NBL0_PIN    GPIO_PIN_0
#define FMC_NBL1_PORT   GPIOE
#define FMC_NBL1_PIN    GPIO_PIN_1

/* SDRAM 控制线 */
#define FMC_SDCLK_PORT  GPIOG
#define FMC_SDCLK_PIN   GPIO_PIN_8
#define FMC_SDNCAS_PORT GPIOG
#define FMC_SDNCAS_PIN  GPIO_PIN_15
#define FMC_SDNRAS_PORT GPIOF
#define FMC_SDNRAS_PIN  GPIO_PIN_11
#define FMC_SDNWE_PORT  GPIOC
#define FMC_SDNWE_PIN   GPIO_PIN_0
#define FMC_SDNE0_PORT  GPIOC
#define FMC_SDNE0_PIN   GPIO_PIN_2
#define FMC_SDCKE0_PORT GPIOC
#define FMC_SDCKE0_PIN  GPIO_PIN_3

/* ==========================================================================
 * 3. 外部 SDRAM 地址空间分配（32MB，本工程只用前 2MB）
 *    Bank1 起始地址 = 0xC0000000（FMC_SDRAM_BANK1 + 16 位数据宽度）
 * ========================================================================== */
#define SDRAM_BANK_ADDR         0xC0000000U
#define SDRAM_SIZE_BYTES        (32U * 1024U * 1024U)

/* LCD 单层帧缓冲：800*480*2 = 750KiB，放在 SDRAM 起始处 */
#define LCD_FB_ADDR             (SDRAM_BANK_ADDR + 0x00000000U)
#define LCD_FB_BYTES            (LCD_WIDTH * LCD_HEIGHT * 2U)

/* 摄像头帧缓冲：QVGA RGB565 双缓冲，接在 LCD 帧缓冲后面（4KiB 对齐） */
#define CAM_FB_ADDR             (SDRAM_BANK_ADDR + 0x00100000U)
#define CAM_FB_BYTES            (320U * 240U * 2U)
#define CAM_FB_ADDR_B           (CAM_FB_ADDR + CAM_FB_BYTES)

/* ==========================================================================
 * 4. DHT11 温湿度：单总线 U10
 *    IO 分配表：PB12 = IIC_INT / 1WIRE_DQ
 *      → 同时接 PCF8574 的 INT 脚和单总线接口(U10) 的 DQ 脚，板上有 4.7K 上拉
 *    ★ 共用提醒：PCF8574 的 INT 是开漏输出。本工程不使用 PCF8574，
 *      它的 INT 由上拉电阻保持高电平，不会干扰 DHT11 的时序；
 *      但若将来要用 PCF8574 的中断，必须先把 PB12 的从机释放掉。
 * ========================================================================== */
#define DHT11_PORT      GPIOB
#define DHT11_PIN       GPIO_PIN_12

/* ==========================================================================
 * 5. 电子电压表：ADC1_IN5
 *    IO 分配表：PA5 = STM_ADC（"拔了 P10 的跳线帽，则该 IO 完全独立"）
 *      板上 RV1 电位器 -> P10 跳线 -> PA5，量程 0~3.3V
 * ========================================================================== */
#define ADC_VOLT_PORT       GPIOA
#define ADC_VOLT_PIN        GPIO_PIN_5
#define ADC_VOLT_CHANNEL    ADC_CHANNEL_5
#define ADC_VREF_MV         3300U       /* VDDA = 3.3V */
#define ADC_FULL_SCALE      4095U       /* 12 位右对齐 */

/* ==========================================================================
 * 6. 摄像头 OV5640：P6（OLED/CAMERA 接口）
 *    DCMI 引脚用 AF13，SCCB 用软件模拟（板上 PB3/PB4 已有 4.7K 上拉）
 *
 *    ★ 与实验一/二的资源冲突说明：
 *      实验一/二用 P6 插 SSD1306 OLED（8080 并口），占用的正是 DCMI 的
 *      PB7/PB4/PH8/PB3/PA15 和数据线；本实验改用 7 寸 RGB 屏之后 P6 空出来
 *      专门给摄像头，两者不再冲突。
 *    ★ PA8(DCMI_XCLK) 不驱动：ATK-OV5640 模块自带 24MHz 有源晶振；
 *      而且 IO 分配表明确写着 PA8 同时接 LF0038 红外接收头并有 4.7K 上拉，
 *      "不建议做普通 IO 使用"，所以本工程不碰它。
 * ========================================================================== */
#define DCMI_AF                 GPIO_AF13_DCMI

#define DCMI_D0_PORT    GPIOC
#define DCMI_D0_PIN     GPIO_PIN_6
#define DCMI_D1_PORT    GPIOC
#define DCMI_D1_PIN     GPIO_PIN_7
#define DCMI_D2_PORT    GPIOC
#define DCMI_D2_PIN     GPIO_PIN_8
#define DCMI_D3_PORT    GPIOC
#define DCMI_D3_PIN     GPIO_PIN_9
#define DCMI_D4_PORT    GPIOC
#define DCMI_D4_PIN     GPIO_PIN_11
#define DCMI_D5_PORT    GPIOD
#define DCMI_D5_PIN     GPIO_PIN_3
#define DCMI_D6_PORT    GPIOB
#define DCMI_D6_PIN     GPIO_PIN_8
#define DCMI_D7_PORT    GPIOB
#define DCMI_D7_PIN     GPIO_PIN_9
#define DCMI_PCLK_PORT  GPIOA
#define DCMI_PCLK_PIN   GPIO_PIN_6
#define DCMI_HREF_PORT  GPIOH
#define DCMI_HREF_PIN   GPIO_PIN_8
#define DCMI_VSYNC_PORT GPIOB
#define DCMI_VSYNC_PIN  GPIO_PIN_7

/* SCCB（摄像头寄存器接口，软件模拟 IIC） */
#define OV5640_SCL_PORT GPIOB
#define OV5640_SCL_PIN  GPIO_PIN_4
#define OV5640_SDA_PORT GPIOB
#define OV5640_SDA_PIN  GPIO_PIN_3
/* 复位脚：OLED/CAMERA 接口的 RESET（IO 分配表：PA15 = JTDI / DCMI_RESET）
 * 板上该脚有 10K 上拉；本实验只用 SWD 调试，所以 PA15 可以安全占用。 */
#define OV5640_RST_PORT GPIOA
#define OV5640_RST_PIN  GPIO_PIN_15

#ifdef __cplusplus
}
#endif

#endif /* __PINMAP_H */

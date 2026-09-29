/**
  ******************************************************************************
  * @file    pinmap.h
  * @brief   FACE_LAB 全工程引脚分配（唯一事实来源）—— 实验四 人脸录入与识别
  *
  *  板卡：正点原子阿波罗 V2 STM32F429（STM32F429IGTx, LQFP176）
  *        配套 DNF429-EB 嵌入式学习实验箱
  *        外接：7 寸 800x480 RGB 屏（RGBLCD 接口）+ ATK-OV5640（P6 摄像头接口）
  *        板载：W25Q256 SPI NOR Flash（32MB，SPI5）用于持久保存人脸库
  *
  *  依据：《阿波罗V2 STM32F429开发板IO引脚分配表》（guide 目录，逐脚核对）
  *        《STM32F429 阿波罗开发指南》第 13/14/25/26/31/39/50 章
  *
  *  ★ 本实验比实验三多了"板载 SPI NOR Flash"这一路，其余（FMC/LTDC/DCMI）
  *    与实验三完全一致：三套总线同时工作，引脚不可随意借用。
  ******************************************************************************
  */

#ifndef __PINMAP_H
#define __PINMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ==========================================================================
 * 0. 板载资源：4 个按键 + 双色 LED
 *    IO 分配表：PA0=WK_UP、PH2=KEY1、PH3=KEY0、PC13=KEY2、PB0=LED1(绿)、PB1=LED0(红)
 *    硬件手册 LED 原理图：VCC3.3 --[510R]-- LED -- MCU 引脚，所以低电平点亮
 * ========================================================================== */
#define KEY0_PORT       GPIOH
#define KEY0_PIN        GPIO_PIN_3      /* 低电平有效 K0  */
#define KEY1_PORT       GPIOH
#define KEY1_PIN        GPIO_PIN_2      /* 低电平有效 K1  */
#define KEY2_PORT       GPIOC
#define KEY2_PIN        GPIO_PIN_13     /* 低电平有效 K2  */
#define KEYUP_PORT      GPIOA
#define KEYUP_PIN       GPIO_PIN_0      /* 高电平有效 WK_UP */

#define LED_GREEN_PORT  GPIOB
#define LED_GREEN_PIN   GPIO_PIN_0      /* DS1 绿色：识别"通过"时点亮 */
#define LED_RED_PORT    GPIOB
#define LED_RED_PIN     GPIO_PIN_1      /* DS0 红色：识别"不通过"时点亮 */
#define LED_ON_LEVEL    GPIO_PIN_RESET
#define LED_OFF_LEVEL   GPIO_PIN_SET

/* ==========================================================================
 * 1. 7 寸 800x480 RGB 屏：LTDC 引脚（全部 AF14）
 *    面板只引出了 R3~R7 / G2~G7 / B3~B7，正好对应 LTDC 的 RGB565
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
#define LCD_CLK_PIN     GPIO_PIN_7
#define LCD_DE_PORT     GPIOF
#define LCD_DE_PIN      GPIO_PIN_10
#define LCD_VSYNC_PORT  GPIOI
#define LCD_VSYNC_PIN   GPIO_PIN_9
#define LCD_HSYNC_PORT  GPIOI
#define LCD_HSYNC_PIN   GPIO_PIN_10

/* 背光：PB5（IO 分配表：PB5=LCD_BL），高电平点亮 */
#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_PIN_5
#define LCD_BL_ON       GPIO_PIN_SET
#define LCD_BL_OFF      GPIO_PIN_RESET

/* 800x480 面板时序 + PLLSAI（30MHz 像素时钟 -> 976x528 -> 58.2Hz） */
#define LCD_WIDTH               800U
#define LCD_HEIGHT              480U
#define LCD_HSW                 48U
#define LCD_HBP                 88U
#define LCD_HFP                 40U
#define LCD_VSW                 3U
#define LCD_VBP                 32U
#define LCD_VFP                 13U
#define LCD_PLLSAI_N            240U
#define LCD_PLLSAI_R            4U
#define LCD_PLLSAI_Q            8U
#define LCD_PLLSAIDIVR          RCC_PLLSAIDIVR_2

/* ==========================================================================
 * 2. 外部 SDRAM：FMC 引脚（W9825G6KH 32MB，16 位，4 Bank x 13 行 x 9 列）
 *    全部 AF12
 * ========================================================================== */
#define SDRAM_AF                GPIO_AF12_FMC

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

#define FMC_BA0_PORT    GPIOG
#define FMC_BA0_PIN     GPIO_PIN_4
#define FMC_BA1_PORT    GPIOG
#define FMC_BA1_PIN     GPIO_PIN_5
#define FMC_NBL0_PORT   GPIOE
#define FMC_NBL0_PIN    GPIO_PIN_0
#define FMC_NBL1_PORT   GPIOE
#define FMC_NBL1_PIN    GPIO_PIN_1

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
 * 3. 外部 SDRAM 地址分配（32MB，本工程只用前 3MB）
 *
 *    为什么必须放 SDRAM：
 *      800x480 RGB565 单帧 = 750KiB，一帧就超过 F429 片内 SRAM（256KiB），
 *      再加上 48x48 人脸图库、LBP/颜色直方图特征和 PCA 模型（240KiB 量级），
 *      只能全部放外部 SDRAM。四块区域刻意分开，互不重叠。
 * ========================================================================== */
#define SDRAM_BANK_ADDR         0xC0000000U
#define SDRAM_SIZE_BYTES        (32U * 1024U * 1024U)

/* LCD 单层帧缓冲：800*480*2 = 750KiB */
#define LCD_FB_ADDR             (SDRAM_BANK_ADDR + 0x00000000U)
#define LCD_FB_BYTES            (LCD_WIDTH * LCD_HEIGHT * 2U)

/* 摄像头缓冲：QVGA RGB565 双缓冲（每块 150KiB） */
#define CAM_FB_ADDR             (SDRAM_BANK_ADDR + 0x00100000U)
#define CAM_FB_BYTES            (320U * 240U * 2U)
#define CAM_FB_ADDR_B           (CAM_FB_ADDR + CAM_FB_BYTES)

/* 人脸图库（48x48 的 RGB565 + 灰度副本） */
#define FACE_DB_ADDR            (SDRAM_BANK_ADDR + 0x00160000U)
#define FACE_DB_RESERVED        (256U * 1024U)

/* 特征区（LBP 分区直方图 + 颜色直方图） */
#define FACE_FEAT_ADDR          (SDRAM_BANK_ADDR + 0x001A0000U)
#define FACE_FEAT_RESERVED      (512U * 1024U)

/* 算法工作区（PCA 模型：均值脸 + 特征脸 + 投影） */
#define ALGO_WS_ADDR            (SDRAM_BANK_ADDR + 0x00220000U)
#define ALGO_WS_RESERVED        (512U * 1024U)

/* SDRAM 自检区：放到 16MiB 处，远离以上所有分配 */
#define SDRAM_TEST_ADDR         (SDRAM_BANK_ADDR + 0x01000000U)

/* ==========================================================================
 * 4. 摄像头 OV5640：P6（OLED/CAMERA 接口）
 *    DCMI 引脚 AF13；SCCB 用软件模拟（PB3/PB4 板上已有 4.7K 上拉）
 *    PA8(DCMI_XCLK) 不驱动：ATK-OV5640 自带 24MHz 有源晶振，且该脚并联
 *    LF0038 红外接收头，IO 分配表明确"不建议做普通 IO 使用"
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

#define OV5640_SCL_PORT GPIOB
#define OV5640_SCL_PIN  GPIO_PIN_4
#define OV5640_SDA_PORT GPIOB
#define OV5640_SDA_PIN  GPIO_PIN_3
#define OV5640_RST_PORT GPIOA
#define OV5640_RST_PIN  GPIO_PIN_15

/* ==========================================================================
 * 5. 板载 SPI NOR Flash：W25Q256（32MB，256Mbit）
 *    IO 分配表：PF6 = F_CS（W25Q256 片选）、
 *               PF7 = SPI5_SCK、PF8 = SPI5_MISO、PF9 = SPI5_MOSI
 *    表中提示"PF6 接 W25Q256 的片选信号，不建议做普通 IO 使用"，
 *    即这四根线板上已连好 W25Q256，不需要外接线。
 *
 *    W25Q256 容量 32MB > 16MB，地址需 25 位，必须用【4 字节地址模式】：
 *    上电发 0xB7 进入 4 字节地址模式，之后用
 *    0x13(读) / 0x12(页编程) / 0x21(4KB 扇区擦除) / 0xDC(64KB 块擦除)。
 * ========================================================================== */
#define W25Q_SPI                SPI5
#define W25Q_SPI_AF             GPIO_AF5_SPI5
#define W25Q_CS_PORT            GPIOF
#define W25Q_CS_PIN             GPIO_PIN_6
#define W25Q_SCK_PORT           GPIOF
#define W25Q_SCK_PIN            GPIO_PIN_7
#define W25Q_MISO_PORT          GPIOF
#define W25Q_MISO_PIN           GPIO_PIN_8
#define W25Q_MOSI_PORT          GPIOF
#define W25Q_MOSI_PIN           GPIO_PIN_9

/* 人脸库在 Flash 里的独立数据区：从 16MiB 偏移开始，占 1MiB。
 * 板载 Flash 前半部分可能存有字库等资源，这里刻意避开，
 * 满足实验要求里"预留独立的数据区域，不得覆盖已有程序或资源数据"。 */
#define FACE_FLASH_BASE         0x01000000U
#define FACE_FLASH_RESERVED     (1U * 1024U * 1024U)

#ifdef __cplusplus
}
#endif

#endif /* __PINMAP_H */

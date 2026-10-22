/**
  ******************************************************************************
  * @file    pinmap.h
  * @brief   FACE_LAB 鍏ㄥ伐绋嬪紩鑴氬垎閰嶏紙鍞竴浜嬪疄鏉ユ簮锛夆€斺€?瀹為獙鍥?浜鸿劯褰曞叆涓庤瘑鍒?  *
  *  鏉垮崱锛氭鐐瑰師瀛愰樋娉㈢綏 V2 STM32F429锛圫TM32F429IGTx, LQFP176锛?  *        閰嶅 DNF429-EB 宓屽叆寮忓涔犲疄楠岀
  *        澶栨帴锛? 瀵?800x480 RGB 灞忥紙RGBLCD 鎺ュ彛锛? ATK-OV5640锛圥6 鎽勫儚澶存帴鍙ｏ級
  *        鏉胯浇锛歐25Q256 SPI NOR Flash锛?2MB锛孲PI5锛夌敤浜庢寔涔呬繚瀛樹汉鑴稿簱
  *
  *  渚濇嵁锛氥€婇樋娉㈢綏V2 STM32F429寮€鍙戞澘IO寮曡剼鍒嗛厤琛ㄣ€嬶紙guide 鐩綍锛岄€愯剼鏍稿锛?  *        銆奡TM32F429 闃挎尝缃楀紑鍙戞寚鍗椼€嬬 13/14/25/26/31/39/50 绔?  *
  *  鈽?鏈疄楠屾瘮瀹為獙涓夊浜?鏉胯浇 SPI NOR Flash"杩欎竴璺紝鍏朵綑锛團MC/LTDC/DCMI锛?  *    涓庡疄楠屼笁瀹屽叏涓€鑷达細涓夊鎬荤嚎鍚屾椂宸ヤ綔锛屽紩鑴氫笉鍙殢鎰忓€熺敤銆?  ******************************************************************************
  */

#ifndef __PINMAP_H
#define __PINMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ==========================================================================
 * 0. 鏉胯浇璧勬簮锛? 涓寜閿?+ 鍙岃壊 LED
 *    IO 鍒嗛厤琛細PA0=WK_UP銆丳H2=KEY1銆丳H3=KEY0銆丳C13=KEY2銆丳B0=LED1(缁?銆丳B1=LED0(绾?
 *    纭欢鎵嬪唽 LED 鍘熺悊鍥撅細VCC3.3 --[510R]-- LED -- MCU 寮曡剼锛屾墍浠ヤ綆鐢靛钩鐐逛寒
 * ========================================================================== */
#define KEY0_PORT       GPIOH
#define KEY0_PIN        GPIO_PIN_3      /* 浣庣數骞虫湁鏁?K0  */
#define KEY1_PORT       GPIOH
#define KEY1_PIN        GPIO_PIN_2      /* 浣庣數骞虫湁鏁?K1  */
#define KEY2_PORT       GPIOC
#define KEY2_PIN        GPIO_PIN_13     /* 浣庣數骞虫湁鏁?K2  */
#define KEYUP_PORT      GPIOA
#define KEYUP_PIN       GPIO_PIN_0      /* 楂樼數骞虫湁鏁?WK_UP */

#define LED_GREEN_PORT  GPIOB
#define LED_GREEN_PIN   GPIO_PIN_0      /* DS1 缁胯壊锛氳瘑鍒?閫氳繃"鏃剁偣浜?*/
#define LED_RED_PORT    GPIOB
#define LED_RED_PIN     GPIO_PIN_1      /* DS0 绾㈣壊锛氳瘑鍒?涓嶉€氳繃"鏃剁偣浜?*/
#define LED_ON_LEVEL    GPIO_PIN_RESET
#define LED_OFF_LEVEL   GPIO_PIN_SET

/* ==========================================================================
 * 1. 7 瀵?800x480 RGB 灞忥細LTDC 寮曡剼锛堝叏閮?AF14锛? *    闈㈡澘鍙紩鍑轰簡 R3~R7 / G2~G7 / B3~B7锛屾濂藉搴?LTDC 鐨?RGB565
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

/* 鑳屽厜锛歅B5锛圛O 鍒嗛厤琛細PB5=LCD_BL锛夛紝楂樼數骞崇偣浜?*/
#define LCD_BL_PORT     GPIOB
#define LCD_BL_PIN      GPIO_PIN_5
#define LCD_BL_ON       GPIO_PIN_SET
#define LCD_BL_OFF      GPIO_PIN_RESET

/* 灞忥細闈㈡澘鍘熺敓 1024x600锛堟鐐瑰師瀛?0x7016锛岀敱 R7/G7/B7 鐨?ID 寮曡剼瀹炴祴寰楀嚭锛夛紝
 * 鐣岄潰浠嶆寜 800x480 缁樺埗銆佺敤鍥惧眰绐楀彛灞呬腑鏄剧ず銆? * 瀹樻柟鍙傛暟锛堛€婂紑鍙戞寚鍗椼€嬬 26 绔?ltdcid == 0X7016 鍒嗘敮锛夛細
 *   hsw=20 hbp=140 hfp=160 vsw=3 vbp=20 vfp=12 -> 鎬?1344x635
 *   鍍忕礌鏃堕挓 ltdc_clk_set(360, 2, RCC_PLLSAIDIVR_4) = 45MHz -> 绾?52.8Hz
 * 锛堝師鍏堣鐢ㄤ簡 4.3 瀵?800x480 鐨?48/88/40/3/32/13 + 30MHz锛屽湪 7 瀵稿睆涓婁細榛戝睆锛?*/
#define LCD_WIDTH               800U
#define LCD_HEIGHT              480U
#define LCD_PANEL_W             1024U
#define LCD_PANEL_H             600U
#define LCD_HSW                 20U
#define LCD_HBP                 140U
#define LCD_HFP                 160U
#define LCD_VSW                 3U
#define LCD_VBP                 20U
#define LCD_VFP                 12U
/* 灞呬腑绐楀彛锛氬乏涓婅 = ((1024-800)/2, (600-480)/2) = (112, 60) */
#define LCD_WIN_X0              112U
#define LCD_WIN_Y0              60U
#define LCD_WIN_X1              (LCD_WIN_X0 + LCD_WIDTH - 1U)
#define LCD_WIN_Y1              (LCD_WIN_Y0 + LCD_HEIGHT - 1U)
#define LCD_PLLSAI_N            360U
#define LCD_PLLSAI_R            2U
#define LCD_PLLSAI_Q            8U
#define LCD_PLLSAIDIVR          RCC_PLLSAIDIVR_4

/* ==========================================================================
 * 2. 澶栭儴 SDRAM锛欶MC 寮曡剼锛圵9825G6KH 32MB锛?6 浣嶏紝4 Bank x 13 琛?x 9 鍒楋級
 *    鍏ㄩ儴 AF12
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
 * 3. 澶栭儴 SDRAM 鍦板潃鍒嗛厤锛?2MB锛屾湰宸ョ▼鍙敤鍓?3MB锛? *
 *    涓轰粈涔堝繀椤绘斁 SDRAM锛? *      800x480 RGB565 鍗曞抚 = 750KiB锛屼竴甯у氨瓒呰繃 F429 鐗囧唴 SRAM锛?56KiB锛夛紝
 *      鍐嶅姞涓?48x48 浜鸿劯鍥惧簱銆丩BP/棰滆壊鐩存柟鍥剧壒寰佸拰 PCA 妯″瀷锛?40KiB 閲忕骇锛夛紝
 *      鍙兘鍏ㄩ儴鏀惧閮?SDRAM銆傚洓鍧楀尯鍩熷埢鎰忓垎寮€锛屼簰涓嶉噸鍙犮€? * ========================================================================== */
#define SDRAM_BANK_ADDR         0xC0000000U
#define SDRAM_SIZE_BYTES        (32U * 1024U * 1024U)

/* LCD 鍗曞眰甯х紦鍐诧細800*480*2 = 750KiB */
#define LCD_FB_ADDR             (SDRAM_BANK_ADDR + 0x00000000U)
#define LCD_FB_BYTES            (LCD_WIDTH * LCD_HEIGHT * 2U)

/* 鎽勫儚澶寸紦鍐诧細QVGA RGB565 鍙岀紦鍐诧紙姣忓潡 150KiB锛?*/
#define CAM_FB_ADDR             (SDRAM_BANK_ADDR + 0x00100000U)
#define CAM_FB_BYTES            (320U * 240U * 2U)
#define CAM_FB_ADDR_B           (CAM_FB_ADDR + CAM_FB_BYTES)

/* 浜鸿劯鍥惧簱锛?8x48 鐨?RGB565 + 鐏板害鍓湰锛?*/
#define FACE_DB_ADDR            (SDRAM_BANK_ADDR + 0x00160000U)
#define FACE_DB_RESERVED        (256U * 1024U)

/* 鐗瑰緛鍖猴紙LBP 鍒嗗尯鐩存柟鍥?+ 棰滆壊鐩存柟鍥撅級 */
#define FACE_FEAT_ADDR          (SDRAM_BANK_ADDR + 0x001A0000U)
#define FACE_FEAT_RESERVED      (512U * 1024U)

/* 绠楁硶宸ヤ綔鍖猴紙PCA 妯″瀷锛氬潎鍊艰劯 + 鐗瑰緛鑴?+ 鎶曞奖锛?*/
#define ALGO_WS_ADDR            (SDRAM_BANK_ADDR + 0x00220000U)
#define ALGO_WS_RESERVED        (512U * 1024U)

/* SDRAM 鑷鍖猴細鏀惧埌 16MiB 澶勶紝杩滅浠ヤ笂鎵€鏈夊垎閰?*/
#define SDRAM_TEST_ADDR         (SDRAM_BANK_ADDR + 0x01000000U)

/* ==========================================================================
 * 4. 鎽勫儚澶?OV5640锛歅6锛圤LED/CAMERA 鎺ュ彛锛? *    DCMI 寮曡剼 AF13锛汼CCB 鐢ㄨ蒋浠舵ā鎷燂紙PB3/PB4 鏉夸笂宸叉湁 4.7K 涓婃媺锛? *    PA8(DCMI_XCLK) 涓嶉┍鍔細ATK-OV5640 鑷甫 24MHz 鏈夋簮鏅舵尟锛屼笖璇ヨ剼骞惰仈
 *    LF0038 绾㈠鎺ユ敹澶达紝IO 鍒嗛厤琛ㄦ槑纭?涓嶅缓璁仛鏅€?IO 浣跨敤"
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
 * 5. 鏉胯浇 SPI NOR Flash锛歐25Q256锛?2MB锛?56Mbit锛? *    IO 鍒嗛厤琛細PF6 = F_CS锛圵25Q256 鐗囬€夛級銆? *               PF7 = SPI5_SCK銆丳F8 = SPI5_MISO銆丳F9 = SPI5_MOSI
 *    琛ㄤ腑鎻愮ず"PF6 鎺?W25Q256 鐨勭墖閫変俊鍙凤紝涓嶅缓璁仛鏅€?IO 浣跨敤"锛? *    鍗宠繖鍥涙牴绾挎澘涓婂凡杩炲ソ W25Q256锛屼笉闇€瑕佸鎺ョ嚎銆? *
 *    W25Q256 瀹归噺 32MB > 16MB锛屽湴鍧€闇€ 25 浣嶏紝蹇呴』鐢ㄣ€? 瀛楄妭鍦板潃妯″紡銆戯細
 *    涓婄數鍙?0xB7 杩涘叆 4 瀛楄妭鍦板潃妯″紡锛屼箣鍚庣敤
 *    0x13(璇? / 0x12(椤电紪绋? / 0x21(4KB 鎵囧尯鎿﹂櫎) / 0xDC(64KB 鍧楁摝闄?銆? * ========================================================================== */
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

/* 浜鸿劯搴撳湪 Flash 閲岀殑鐙珛鏁版嵁鍖猴細浠?16MiB 鍋忕Щ寮€濮嬶紝鍗?1MiB銆? * 鏉胯浇 Flash 鍓嶅崐閮ㄥ垎鍙兘瀛樻湁瀛楀簱绛夎祫婧愶紝杩欓噷鍒绘剰閬垮紑锛? * 婊¤冻瀹為獙瑕佹眰閲?棰勭暀鐙珛鐨勬暟鎹尯鍩燂紝涓嶅緱瑕嗙洊宸叉湁绋嬪簭鎴栬祫婧愭暟鎹?銆?*/
#define FACE_FLASH_BASE         0x01000000U
#define FACE_FLASH_RESERVED     (1U * 1024U * 1024U)

#ifdef __cplusplus
}
#endif


/* ---- 摄像头 XCLK：PA8 = MCO1，输出 HSE(25MHz) 供传感器工作 ---- */
#define OV5640_XCLK_PORT GPIOA
#define OV5640_XCLK_PIN  GPIO_PIN_8
#define OV5640_XCLK_AF   0U          /* AF0 = MCO1 */

/* ---- 摄像头 PWDN：P6 的 DCMI_PWDN 接在 PCF8574 的 P2 上
 *      PCF8574 挂在 PH4=SCL / PH5=SDA（板上 A0~A2 接地 -> 写地址 0x40）---- */
#define PCF8574_SCL_PORT  GPIOH
#define PCF8574_SCL_PIN   GPIO_PIN_4
#define PCF8574_SDA_PORT  GPIOH
#define PCF8574_SDA_PIN   GPIO_PIN_5
#define PCF8574_ADDR_W    0x40U
#define PCF8574_ADDR_R    0x41U
#define PCF8574_PWDN_BIT  0x04U
#endif /* __PINMAP_H */

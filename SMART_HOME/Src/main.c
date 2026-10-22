/**
  ******************************************************************************
  * @file    main.c
  * @brief   SMART_HOME —— STM32F429 智能家居中控终端（实验三）
  *
  *  板卡：正点原子阿波罗 V2 STM32F429（STM32F429IGTx）+ DNF429-EB 实验箱
  *  外设：7 寸 RGB 屏（面板 1024x600，界面 800x480 居中；LTDC + SDRAM 帧缓冲）
  *        DHT11 温湿度（PB12 单总线）           —— 开发指南第 45 章
  *        电子电压表（RV1 -> PA5/ADC1_IN5）     —— 第 32 章
  *        OV5640 摄像头（P6，DCMI + DMA）       —— 第 50 章
  *        板载双色 LED（PB0 绿 / PB1 红）与 4 个按键
  *
  *  时钟：HSE 25MHz -> PLL(25/360/2) -> SYSCLK 180MHz
  *        HCLK 180MHz / APB1 45MHz / APB2 90MHz / SDRAM 90MHz
  *        LTDC 像素时钟 = PLLSAI(1MHz x 360 / 2) / 4 = 45MHz（1024x600 面板）
  *
  *  主循环里只有 App_Task()，所有采集/显示/报警都是按时间戳调度的，
  *  没有任何 while 等待，保证按键和摄像头预览不会被"卡住"。
  ******************************************************************************
  */

#include "main.h"
#include "gpio.h"
#include "app.h"
#include "config.h"

#if (CFG_LCD_SELFTEST != 0U)
#include "pinmap.h"
#include "bsp_lcd.h"
#include "bsp_sdram.h"
#include "bsp_delay.h"

/**
  * @brief  LCD 纯显示自检（CFG_LCD_SELFTEST=1 时使用）
  *
  *  只初始化 时基 / SDRAM / LCD，然后画一屏"亮底 + 竖色带 + 大号文字 +
  *  底部活动蓝条"，最后死循环。完全不初始化 DHT11 / ADC / 摄像头 / 按键 /
  *  报警，用于把 "屏 + 排线 + 背光 + LTDC + 帧缓冲" 这条链路单独验证出来。
  *
  *  画面含义：
  *    - 整屏白底      -> 背光/屏只要工作，必然是一片亮白，不可能"全黑"
  *    - 8 条竖色带    -> 校验 RGB 数据线有没有接错/缺位
  *    - 深色面板+大字 -> 校验字库与对比度
  *    - 底部蓝条来回跑 -> 证明程序在跑（不是静态残影）
  */
static void lcd_selftest(void)
{
  static const uint16_t bars[8] =
  {
    LCD_WHITE, LCD_YELLOW, LCD_TEAL, LCD_GREEN, LCD_ORANGE, LCD_RED, LCD_BLUE, LCD_BLACK
  };
  uint16_t i;
  uint16_t x;
  uint32_t n = 0U;

  BSP_DelayInit();
  (void)BSP_SDRAM_Init();

  if (BSP_LCD_Init() != 0U)
  {
    Error_Handler();                 /* LCD 初始化失败：停在这里便于调试 */
  }

  BSP_LCD_Clear(LCD_WHITE);          /* 整屏白底 */

  for (i = 0U; i < 8U; i++)          /* 上半屏 8 条竖色带，各 100 像素宽 */
  {
    BSP_LCD_FillRect((uint16_t)(i * 100U), 0U, 100U, 200U, bars[i]);
  }

  BSP_LCD_FillRect(0U, 210U, LCD_WIDTH, 170U, LCD_BLACK);
  BSP_LCD_DrawStringCentered(220U, EXP_TITLE, LCD_WHITE, LCD_BLACK, LCD_FONT_LARGE);
  BSP_LCD_DrawStringCentered(270U, "LCD SELFTEST fb 800x480 / panel 1024x600",
                             LCD_YELLOW, LCD_BLACK, LCD_FONT_SMALL);
  BSP_LCD_DrawString(40U, 312U, "姓名 " STUDENT_NAME, LCD_WHITE, LCD_BLACK, LCD_FONT_SMALL);
  BSP_LCD_DrawString(300U, 312U, "学号 " STUDENT_ID, LCD_WHITE, LCD_BLACK, LCD_FONT_SMALL);
  BSP_LCD_DrawString(600U, 312U, "班级 " STUDENT_CLASS, LCD_WHITE, LCD_BLACK, LCD_FONT_SMALL);

  for (;;)                           /* 底部活动蓝条 */
  {
    x = (uint16_t)((n * 7U) % (uint16_t)(LCD_WIDTH - 60U));
    BSP_LCD_FillRect(0U, 400U, LCD_WIDTH, 80U, LCD_LIGHTGRAY);
    BSP_LCD_FillRect(x, 408U, 60U, 64U, LCD_BLUE);
    n++;
    HAL_Delay(25U);
  }
}
#endif /* CFG_LCD_SELFTEST */

void SystemClock_Config(void);

int main(void)
{
  /* 复位外设、初始化 Flash 接口与 1ms 时基（SysTick） */
  HAL_Init();

  /* 系统时钟 180MHz */
  SystemClock_Config();

#if (CFG_LCD_SELFTEST != 0U)
  lcd_selftest();                    /* 纯显示自检：内部死循环，不返回 */
#endif

  /* 基础 GPIO：4 个按键 */
  MX_GPIO_Init();

  /* 应用层：SDRAM -> LCD -> 传感器 -> 摄像头 -> 状态机 */
  App_Init();

  while (1)
  {
    App_Task();
  }
}

/**
  * @brief System Clock Configuration
  *        HSE 25MHz, PLLM=25, PLLN=360, PLLP=2 -> SYSCLK 180MHz
  *        注意 LTDC 的 PLLSAI 在 bsp_lcd.c 里单独配置（45MHz 像素时钟）
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** 主稳压器输出电压等级 1（配合 Over-Drive 才能到 180MHz） */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** 打开 HSE 并配置 PLL */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 25;
  RCC_OscInitStruct.PLL.PLLN       = 360;
  RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ       = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** 使能 Over-Drive 模式 */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** 配置 HCLK / PCLK1 / PCLK2 */
  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;    /* HCLK  = 180MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;      /* APB1  = 45MHz  */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;      /* APB2  = 90MHz  */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  出错处理：关中断死循环（便于调试器抓现场）
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */

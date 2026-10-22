/**
  ******************************************************************************
  * @file    main.c
  * @brief   FACE_LAB —— STM32F429 人脸录入与识别（实验四）
  *
  *  板卡：正点原子阿波罗 V2 STM32F429（STM32F429IGTx）+ DNF429-EB 实验箱
  *  外接：7 寸 1024x600 RGB 屏（800x480 界面居中）（RGBLCD 接口，LTDC + SDRAM 帧缓冲）
  *        ATK-OV5640 摄像头（P6，DCMI + DMA 实时预览与抓拍）—— 开发指南第 50 章
  *  板载：W25Q256 SPI NOR Flash（SPI5：PF6~PF9，人脸库持久化）
  *        W9825G6KH SDRAM（帧缓冲 / 人脸图库 / 特征 / PCA 模型都在这里）
  *        双色 LED（PB0 绿=识别通过 / PB1 红=不通过）+ 4 个按键
  *
  *  时钟：HSE 25MHz -> PLL(25/360/2) -> SYSCLK 180MHz
  *        HCLK 180MHz / APB1 45MHz / APB2 90MHz / SDRAM 90MHz
  *        LTDC 像素时钟 = PLLSAI(1MHz x 360 / 2) / 4 = 45MHz
  *
  *  主循环里只有 App_Task()：摄像头逐帧采集、按键、界面刷新都按时间戳调度，
  *  没有任何 while 等待，所以预览不卡、按键始终跟手。
  ******************************************************************************
  */

#include "main.h"
#include "gpio.h"
#include "app.h"

void SystemClock_Config(void);

int main(void)
{
  /* 复位外设、初始化 Flash 接口与 1ms 时基（SysTick） */
  HAL_Init();

  /* 系统时钟 180MHz */
  SystemClock_Config();

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

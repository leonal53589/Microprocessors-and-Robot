/**
  ******************************************************************************
  * @file    main.c
  * @brief   TIMER_LAB —— STM32F429 定时器综合实验
  *
  *  板卡：正点原子阿波罗 V2 STM32F429（STM32F429IGTx）+ DNF429-EB 实验箱
  *  时钟：HSE 25MHz -> PLL -> SYSCLK 180MHz
  *        HCLK 180MHz / APB1 45MHz（定时器 90MHz）/ APB2 90MHz（定时器 180MHz）
  *
  *  五个实验用 OLED 菜单切换，按键分配：
  *      UP  (PA0)  = 切换 / 翻页
  *      K0  (PH3)  = 确认 / 启动 / 反应按键
  *      K1  (PH2)  = 返回主菜单
  *      K2  (PC13) = 实验五"刹车确认恢复"
  ******************************************************************************
  */

#include "main.h"
#include "gpio.h"
#include "app.h"

void SystemClock_Config(void);

int main(void)
{
  /* 复位外设、初始化 Flash 接口与 HAL 时基
   * 注意：本工程的 1ms 时基由 TIM6 提供（见 bsp_tim.c 里重写的 HAL_InitTick） */
  HAL_Init();

  /* 配置系统时钟 180MHz */
  SystemClock_Config();

  /* 基础 GPIO：按键 + 提示 LED */
  MX_GPIO_Init();

  /* 应用层：OLED / 蜂鸣器 / 定时器 / 菜单 */
  App_Init();

  while (1)
  {
    App_Task();
  }
}

/**
  * @brief System Clock Configuration
  *        HSE 25MHz, PLLM=25, PLLN=360, PLLP=2 -> SYSCLK 180MHz
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
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;      /* 45MHz -> 定时器 90MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;      /* 90MHz -> 定时器 180MHz */

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

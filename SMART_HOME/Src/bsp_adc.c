/**
  ******************************************************************************
  * @file    bsp_adc.c
  * @brief   ADC1_IN5 单通道采样（板载 RV1 电位器电压表）
  *
  *  通道确认（《阿波罗V2 STM32F429开发板IO引脚分配表》）：
  *      PA5 = STM_ADC，"ADC 输入引脚，同时做 TPAD 检测脚；
  *            拔了 P10 的跳线帽，则该 IO 完全独立"
  *      也就是说 RV1 -> P10 跳线 -> PA5 -> ADC1_IN5 是板上既定的通路，
  *      不需要另外接线，只要保证 P10 的跳线帽插好。
  *
  *  采样参数：
  *      ADC 时钟 = PCLK2/4 = 90MHz/4 = 22.5MHz（F4 的 ADC 时钟上限 36MHz）
  *      采样时间 = 480 个 ADC 周期（电位器源阻抗较高，取最长采样保证建立）
  *      分辨率 12 位、右对齐、软件触发、单次转换（不用 DMA，够用且简单）
  *
  *  ⚠️ 本 HAL 版本里 F4 的 ADC 既没有提供 HAL_ADCEx_Calibration_Start()，
  *     也没有可以由用户触发的自校准（CAL）位（对照 stm32f429xx.h 的 ADC_CR2
  *     位定义），所以初始化里【不做校准】：打开 ADON 后留 20us 稳定时间
  *     （tSTAB）即可，读数噪声靠 BSP_ADC_ReadAverage() 的多次平均压制。
  ******************************************************************************
  */

#include "bsp_adc.h"
#include "pinmap.h"
#include "bsp_delay.h"

static ADC_HandleTypeDef s_hadc;
static uint8_t          s_ready;

static void adc_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* 模拟输入：不能开上下拉，否则会改变 RV1 的分压 */
  gpio.Pin  = ADC_VOLT_PIN;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ADC_VOLT_PORT, &gpio);
}

uint8_t BSP_ADC_Init(void)
{
  ADC_ChannelConfTypeDef cfg = {0};

  adc_gpio_init();
  __HAL_RCC_ADC1_CLK_ENABLE();

  s_hadc.Instance                   = ADC1;
  s_hadc.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
  s_hadc.Init.Resolution            = ADC_RESOLUTION_12B;
  s_hadc.Init.ScanConvMode          = DISABLE;
  s_hadc.Init.ContinuousConvMode    = DISABLE;
  s_hadc.Init.DiscontinuousConvMode = DISABLE;
  s_hadc.Init.NbrOfDiscConversion   = 0U;
  s_hadc.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  s_hadc.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  s_hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  s_hadc.Init.NbrOfConversion       = 1U;
  s_hadc.Init.DMAContinuousRequests = DISABLE;
  s_hadc.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;

  if (HAL_ADC_Init(&s_hadc) != HAL_OK)
  {
    return 1U;
  }

  cfg.Channel      = ADC_VOLT_CHANNEL;
  cfg.Rank         = 1U;
  cfg.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  cfg.Offset       = 0U;

  if (HAL_ADC_ConfigChannel(&s_hadc, &cfg) != HAL_OK)
  {
    return 2U;
  }

  /* ---- 上电稳定 ----
   * 注意：STM32F4 的 ADC 没有"用户触发自校准"的 CAL 位（ADC_CR2 里只有
   * ADON / CONT / DMA / SWSTART…，对照 stm32f429xx.h 的位定义），
   * 所以这里不做校准，只要打开 ADC 并留出稳定时间 tSTAB 即可；
   * 读数噪声靠 BSP_ADC_ReadAverage() 的多次平均来压制。 */
  ADC1->CR2 |= ADC_CR2_ADON;
  BSP_DelayUs(20U);

  s_ready = 1U;

  return 0U;
}

uint8_t BSP_ADC_IsReady(void)
{
  return s_ready;
}

uint16_t BSP_ADC_ReadRaw(void)
{
  uint16_t value;

  if (HAL_ADC_Start(&s_hadc) != HAL_OK)
  {
    return 0xFFFFU;
  }
  if (HAL_ADC_PollForConversion(&s_hadc, 10U) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&s_hadc);
    return 0xFFFFU;
  }

  value = (uint16_t)HAL_ADC_GetValue(&s_hadc);
  (void)HAL_ADC_Stop(&s_hadc);

  return value;
}

uint16_t BSP_ADC_ReadAverage(uint8_t count)
{
  uint32_t sum = 0U;
  uint8_t  used = 0U;

  if (count == 0U)
  {
    return 0xFFFFU;
  }

  for (uint8_t i = 0U; i < count; i++)
  {
    uint16_t v = BSP_ADC_ReadRaw();

    if (v != 0xFFFFU)
    {
      sum += v;
      used++;
    }
  }

  if (used == 0U)
  {
    return 0xFFFFU;
  }

  return (uint16_t)(sum / used);
}

uint16_t BSP_ADC_ToMilliVolt(uint16_t raw)
{
  /* V = N x VDDA / 4095，先乘后除避免整数截断丢精度 */
  return (uint16_t)(((uint32_t)raw * ADC_VREF_MV) / ADC_FULL_SCALE);
}

float BSP_ADC_ToVolt(uint16_t raw)
{
  return (float)BSP_ADC_ToMilliVolt(raw) / 1000.0f;
}

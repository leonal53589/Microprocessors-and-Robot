/**
  ******************************************************************************
  * @file    bsp_dcmi.c
  * @brief   DCMI + DMA2_Stream1_Channel1 摄像头采集
  *
  *  引脚（IO 分配表"OLED/CAMERA 接口"列，全部 AF13）：
  *      D0~D7 = PC6 PC7 PC8 PC9 PC11 PD3 PB8 PB9
  *      PCLK  = PA6    HREF = PH8    VSYNC = PB7
  *      （SCCB 的 PB3/PB4 和复位 PA15 由 ov5640.c 管理，本文件不碰）
  *
  *  为什么 DCMI 也要开中断：
  *      本 HAL 版本的 DCMI 在 DMA 搬完全部数据后，会打开"帧结束中断"，
  *      由 DCMI_IRQHandler -> HAL_DCMI_FrameEventCallback() 通知应用层，
  *      所以 NVIC 里必须同时打开 DMA2_Stream1 和 DCMI 两个中断。
  *
  *  DMA 参数的选择：
  *      DCMI_DR 是 32 位寄存器，配合 8 位数据宽度，
  *      DMA 一次搬 4 个字节（外设/存储器都按 Word 对齐），
  *      所以 Length 的单位是"字"，QVGA RGB565 = 320*240*2/4 = 38400。
  ******************************************************************************
  */

#include "bsp_dcmi.h"
#include "pinmap.h"

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
} BspPin;

static const BspPin s_dcmi_pins[] =
{
  { DCMI_D0_PORT,    DCMI_D0_PIN    }, { DCMI_D1_PORT,    DCMI_D1_PIN    },
  { DCMI_D2_PORT,    DCMI_D2_PIN    }, { DCMI_D3_PORT,    DCMI_D3_PIN    },
  { DCMI_D4_PORT,    DCMI_D4_PIN    }, { DCMI_D5_PORT,    DCMI_D5_PIN    },
  { DCMI_D6_PORT,    DCMI_D6_PIN    }, { DCMI_D7_PORT,    DCMI_D7_PIN    },
  { DCMI_PCLK_PORT,  DCMI_PCLK_PIN  }, { DCMI_HREF_PORT,  DCMI_HREF_PIN  },
  { DCMI_VSYNC_PORT, DCMI_VSYNC_PIN }
};

static DCMI_HandleTypeDef s_hdcmi;
static DMA_HandleTypeDef  s_hdma_dcmi;

static volatile uint8_t  s_frame_ready;
static volatile uint8_t  s_error;
static volatile uint32_t s_frame_count;

static void dcmi_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;   /* 摄像头像素时钟最高 48MHz */
  gpio.Alternate = DCMI_AF;

  for (uint32_t i = 0U; i < (sizeof(s_dcmi_pins) / sizeof(s_dcmi_pins[0])); i++)
  {
    gpio.Pin = s_dcmi_pins[i].pin;
    HAL_GPIO_Init(s_dcmi_pins[i].port, &gpio);
  }
}

static void dcmi_dma_init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  s_hdma_dcmi.Instance                 = DMA2_Stream1;
  s_hdma_dcmi.Init.Channel             = DMA_CHANNEL_1;
  s_hdma_dcmi.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  s_hdma_dcmi.Init.PeriphInc           = DMA_PINC_DISABLE;
  s_hdma_dcmi.Init.MemInc              = DMA_MINC_ENABLE;
  s_hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  s_hdma_dcmi.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  s_hdma_dcmi.Init.Mode                = DMA_NORMAL;
  s_hdma_dcmi.Init.Priority            = DMA_PRIORITY_HIGH;
  s_hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  s_hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  s_hdma_dcmi.Init.MemBurst            = DMA_MBURST_SINGLE;
  s_hdma_dcmi.Init.PeriphBurst         = DMA_PBURST_SINGLE;

  if (HAL_DMA_Init(&s_hdma_dcmi) != HAL_OK)
  {
    Error_Handler();
  }

  /* 中断优先级放在 SysTick(0) 之后，保证 1ms 时基不被摄像头打断 */
  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
  HAL_NVIC_SetPriority(DCMI_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

uint8_t BSP_DCMI_Init(void)
{
  dcmi_gpio_init();
  dcmi_dma_init();

  __HAL_RCC_DCMI_CLK_ENABLE();

  s_hdcmi.Instance = DCMI;
  s_hdcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
  s_hdcmi.Init.PCKPolarity      = DCMI_PCKPOLARITY_RISING;
  /* OV5640 的 VSYNC/HREF 都是高有效（对照器件时序图） */
  s_hdcmi.Init.VSPolarity       = DCMI_VSPOLARITY_HIGH;
  s_hdcmi.Init.HSPolarity       = DCMI_HSPOLARITY_HIGH;
  s_hdcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
  s_hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  s_hdcmi.DMA_Handle            = &s_hdma_dcmi;

  if (HAL_DCMI_Init(&s_hdcmi) != HAL_OK)
  {
    return 1U;
  }

  s_frame_ready = 0U;
  s_error       = 0U;
  s_frame_count = 0U;

  return 0U;
}

uint8_t BSP_DCMI_StartFrame(uint16_t *buf, uint32_t length_words)
{
  if (buf == NULL)
  {
    return 1U;
  }

  /* 上一帧结束后 DCMI 的 CAPTURE 位还是开的，先彻底停掉并清溢出标志，
   * 否则新一帧可能从中间开始搬，图像会错位/撕裂 */
  (void)HAL_DCMI_Stop(&s_hdcmi);
  __HAL_DCMI_CLEAR_FLAG(&s_hdcmi, DCMI_FLAG_OVRRI);

  s_error = 0U;

  if (HAL_DCMI_Start_DMA(&s_hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)buf, length_words) != HAL_OK)
  {
    return 2U;
  }

  return 0U;
}

void BSP_DCMI_Stop(void)
{
  (void)HAL_DCMI_Stop(&s_hdcmi);
  s_frame_ready = 0U;
}

uint8_t BSP_DCMI_TakeFrame(void)
{
  uint8_t ready = s_frame_ready;

  s_frame_ready = 0U;
  return ready;
}

uint8_t BSP_DCMI_HasError(void)
{
  return s_error;
}

void BSP_DCMI_ClearError(void)
{
  s_error = 0U;
}

uint32_t BSP_DCMI_FrameCount(void)
{
  return s_frame_count;
}

void BSP_DCMI_SwapBytes(uint16_t *buf, uint32_t pixels)
{
  if (buf == NULL)
  {
    return;
  }

  for (uint32_t i = 0U; i < pixels; i++)
  {
    uint16_t v = buf[i];
    buf[i] = (uint16_t)((v >> 8) | (v << 8));
  }
}

void BSP_DCMI_IRQHandler(void)
{
  HAL_DCMI_IRQHandler(&s_hdcmi);
}

void BSP_DCMI_DmaIRQHandler(void)
{
  HAL_DMA_IRQHandler(&s_hdma_dcmi);
}

/* -------------------------------------------------------------------------- */
/* HAL 回调（都在中断上下文里，只置标志，不做耗时操作）                        */
/* -------------------------------------------------------------------------- */

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  (void)hdcmi;
  s_frame_ready = 1U;
  s_frame_count++;
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
  (void)hdcmi;
  s_error = 1U;
}

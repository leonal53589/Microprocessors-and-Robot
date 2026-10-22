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
volatile BSP_DCMI_Diagnostics g_dcmi_diag;

static void dcmi_record_fault(uint8_t code)
{
  g_dcmi_diag.last_fault = code;
  g_dcmi_diag.faults++;
  if (s_error != 0U) { return; }  /* preserve the first fault in this frame */
  g_dcmi_diag.active_error = 1U;
  g_dcmi_diag.last_fault_tick = HAL_GetTick();
  g_dcmi_diag.dcmi_error = s_hdcmi.ErrorCode;
  g_dcmi_diag.dma_error = s_hdma_dcmi.ErrorCode;
  g_dcmi_diag.dcmi_risr = DCMI->RISR;
  g_dcmi_diag.dcmi_misr = DCMI->MISR;
  g_dcmi_diag.dma_ndtr = DMA2_Stream1->NDTR;
  g_dcmi_diag.dcmi_state = (uint32_t)s_hdcmi.State;
  g_dcmi_diag.dma_state = (uint32_t)s_hdma_dcmi.State;
  s_error = 1U;
}

static uint8_t dcmi_fault_from_hal(void)
{
  uint32_t de = s_hdcmi.ErrorCode;
  uint32_t me = s_hdma_dcmi.ErrorCode;
  if ((de & HAL_DCMI_ERROR_OVR) != 0U) return BSP_DCMI_FAULT_OVERRUN;
  if ((de & HAL_DCMI_ERROR_SYNC) != 0U) return BSP_DCMI_FAULT_SYNC;
  if ((me & HAL_DMA_ERROR_TE) != 0U) return BSP_DCMI_FAULT_DMA_TRANSFER;
  if ((me & HAL_DMA_ERROR_DME) != 0U) return BSP_DCMI_FAULT_DMA_DIRECT;
  if ((me & HAL_DMA_ERROR_FE) != 0U) return BSP_DCMI_FAULT_DMA_FIFO;
  return BSP_DCMI_FAULT_DMA_OTHER;
}


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

static uint8_t dcmi_dma_init(void)
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
  s_hdma_dcmi.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  s_hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  s_hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  s_hdma_dcmi.Init.MemBurst            = DMA_MBURST_SINGLE;
  s_hdma_dcmi.Init.PeriphBurst         = DMA_PBURST_SINGLE;

  if (HAL_DMA_Init(&s_hdma_dcmi) != HAL_OK)
  {
    return 1U;
  }

  /* 中断优先级放在 SysTick(0) 之后，保证 1ms 时基不被摄像头打断 */
  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
  HAL_NVIC_SetPriority(DCMI_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
  return 0U;
}

uint8_t BSP_DCMI_Init(void)
{
  dcmi_gpio_init();
  if (dcmi_dma_init() != 0U) { return 1U; }

  __HAL_RCC_DCMI_CLK_ENABLE();

  s_hdcmi.Instance = DCMI;
  s_hdcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
  s_hdcmi.Init.PCKPolarity      = DCMI_PCKPOLARITY_RISING;
  /* OV5640 0x4740=0x21: VSYNC/HREF 高电平时像素有效。
   * DCMI 的同步有效电平表示消隐（忽略像素），因此两者均选 LOW。 */
  s_hdcmi.Init.VSPolarity       = DCMI_VSPOLARITY_LOW;
  s_hdcmi.Init.HSPolarity       = DCMI_HSPOLARITY_LOW;
  s_hdcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
  s_hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  s_hdcmi.Init.JPEGMode         = DCMI_JPEG_DISABLE;
  /* HAL DMA 回调通过 Parent 找回 DCMI；必须建立双向关联。 */
  __HAL_LINKDMA(&s_hdcmi, DMA_Handle, s_hdma_dcmi);

  if (HAL_DCMI_Init(&s_hdcmi) != HAL_OK)
  {
    return 1U;
  }

  s_frame_ready = 0U;
  s_error       = 0U;
  g_dcmi_diag = (BSP_DCMI_Diagnostics){0};

  return 0U;
}

uint8_t BSP_DCMI_StartFrame(uint16_t *buf, uint32_t length_words)
{
  if ((buf == NULL) || (((uint32_t)buf & 3U) != 0U) ||
      (length_words == 0U) || (length_words > 0xFFFFU))
  {
    dcmi_record_fault(BSP_DCMI_FAULT_ARGUMENT);
    return 1U;
  }

  /* 上一帧结束后 DCMI 的 CAPTURE 位还是开的，先彻底停掉并清溢出标志，
   * 否则新一帧可能从中间开始搬，图像会错位/撕裂 */
  if (HAL_DCMI_Stop(&s_hdcmi) != HAL_OK)
  {
    dcmi_record_fault(BSP_DCMI_FAULT_STOP);
    return 2U;
  }
  s_hdcmi.ErrorCode = HAL_DCMI_ERROR_NONE;  /* discard previous frame errors */
  __HAL_DCMI_DISABLE_IT(&s_hdcmi, DCMI_IT_FRAME | DCMI_IT_LINE | DCMI_IT_VSYNC);
  __HAL_DCMI_CLEAR_FLAG(&s_hdcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
                       DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI | DCMI_FLAG_LINERI);
  /* HAL 帧结束回调会关闭错误中断；每次抓取都要重新开启。 */
  __HAL_DCMI_ENABLE_IT(&s_hdcmi, DCMI_IT_ERR | DCMI_IT_OVR);

  s_frame_ready = 0U;
  s_error = 0U;
  g_dcmi_diag.active_error = 0U;

  if (HAL_DCMI_Start_DMA(&s_hdcmi, DCMI_MODE_SNAPSHOT, (uint32_t)buf, length_words) != HAL_OK)
  {
    dcmi_record_fault(BSP_DCMI_FAULT_START);
    return 2U;
  }

  /* 本工程的旧 HAL 没有向上传递 HAL_DMA_Start_IT 的失败结果。 */
  if ((s_hdma_dcmi.ErrorCode != HAL_DMA_ERROR_NONE) ||
      (s_hdma_dcmi.State != HAL_DMA_STATE_BUSY) ||
      (DMA2_Stream1->NDTR == 0U))
  {
    dcmi_record_fault(BSP_DCMI_FAULT_DMA_START);
    (void)HAL_DCMI_Stop(&s_hdcmi);
    return 2U;
  }
  g_dcmi_diag.starts++;
  g_dcmi_diag.last_start_tick = HAL_GetTick();
  return 0U;
}

void BSP_DCMI_Stop(void)
{
  (void)HAL_DCMI_Stop(&s_hdcmi);
  s_frame_ready = 0U;
}

uint8_t BSP_DCMI_TakeFrame(void)
{
  uint8_t ready;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  /* This HAL reports a frame only through the DCMI FRAME IRQ. On rare frames
   * DMA has transferred every word and DCMI has latched FRAME, but the IRQ
   * remains masked. Accept only that fully completed buffer. */
  if ((s_frame_ready == 0U) && (s_error == 0U) &&
      ((DCMI->RISR & DCMI_FLAG_FRAMERI) != 0U) &&
      ((DCMI->RISR & (DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI)) == 0U) &&
      (DMA2_Stream1->NDTR == 0U) &&
      ((DMA2_Stream1->CR & DMA_SxCR_EN) == 0U) &&
      (s_hdcmi.State == HAL_DCMI_STATE_READY) &&
      (s_hdcmi.ErrorCode == HAL_DCMI_ERROR_NONE) &&
      (s_hdma_dcmi.ErrorCode == HAL_DMA_ERROR_NONE))
  {
    __HAL_DCMI_DISABLE_IT(&s_hdcmi, DCMI_IT_FRAME);
    __HAL_DCMI_CLEAR_FLAG(&s_hdcmi, DCMI_FLAG_FRAMERI);
    s_frame_ready = 1U;
    g_dcmi_diag.frames++;
    g_dcmi_diag.polled_frames++;
    g_dcmi_diag.last_frame_ms = HAL_GetTick() - g_dcmi_diag.last_start_tick;
    if (g_dcmi_diag.last_frame_ms > g_dcmi_diag.max_frame_ms)
    {
      g_dcmi_diag.max_frame_ms = g_dcmi_diag.last_frame_ms;
    }
  }

  ready = s_frame_ready;
  s_frame_ready = 0U;
  __set_PRIMASK(primask);
  return ready;
}

uint8_t BSP_DCMI_HasError(void)
{
  return s_error;
}

void BSP_DCMI_ClearError(void)
{
  s_error = 0U;
  g_dcmi_diag.active_error = 0U;
}

void BSP_DCMI_RecordTimeout(void)
{
  g_dcmi_diag.timeouts++;
  dcmi_record_fault(BSP_DCMI_FAULT_FRAME_TIMEOUT);
  s_error = 0U;  /* retry until no valid frame for CFG_CAM_STALE_MS */
  g_dcmi_diag.active_error = 0U;
}

uint8_t BSP_DCMI_LastFaultCode(void)
{
  return g_dcmi_diag.last_fault;
}

uint32_t BSP_DCMI_FrameCount(void)
{
  return g_dcmi_diag.frames;
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
  g_dcmi_diag.frames++;
  g_dcmi_diag.last_frame_ms = HAL_GetTick() - g_dcmi_diag.last_start_tick;
  if (g_dcmi_diag.last_frame_ms > g_dcmi_diag.max_frame_ms)
  {
    g_dcmi_diag.max_frame_ms = g_dcmi_diag.last_frame_ms;
  }
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
  __HAL_DCMI_DISABLE_IT(hdcmi, DCMI_IT_ERR | DCMI_IT_OVR);
  dcmi_record_fault(dcmi_fault_from_hal());
}

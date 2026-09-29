/**
  ******************************************************************************
  * @file    bsp_sdram.c
  * @brief   FMC + W9825G6KH SDRAM 初始化与自检
  *
  *  器件：W9825G6KH-6（32MB = 4 Bank x 8192 行 x 512 列 x 16 位）
  *        -6 表示 6ns / 166MHz，本工程用 90MHz（HCLK/2），CAS 延迟取 3 更稳
  *
  *  初始化顺序（《STM32F4xx 参考手册》FMC 章节 + 器件手册 JEDEC 上电流程）：
  *      1) 配置 FMC 控制器寄存器（时序 + 器件参数）
  *      2) 使能 SDRAM 时钟（CLK_ENABLE 命令），等 >= 100us
  *      3) 对所有 Bank 预充电（PALL）
  *      4) 自动刷新 8 次（AUTOREFRESH x8）
  *      5) 写模式寄存器（LOAD_MODE）：突发长度 1、顺序、CAS=3、单次写
  *      6) 设置刷新计数器（64ms / 8192 行 -> 7.8125us @90MHz）
  *
  *  ⚠️ 后面的 LTDC 帧缓冲、DCMI 图像缓冲都落在这片 SDRAM 上，
  *     所以 LCD 花屏 / 摄像头丢帧时，第一个要怀疑的就是这里。
  ******************************************************************************
  */

#include "bsp_sdram.h"
#include "bsp_delay.h"
#include "pinmap.h"

/* ---- 引脚表：IO 分配表里所有标注 FMC 的脚，AF12，速度必须开到最高 ---- */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
} BspPin;

static const BspPin s_fmc_pins[] =
{
  /* 数据总线 D0~D15 */
  { FMC_D0_PORT,  FMC_D0_PIN  }, { FMC_D1_PORT,  FMC_D1_PIN  },
  { FMC_D2_PORT,  FMC_D2_PIN  }, { FMC_D3_PORT,  FMC_D3_PIN  },
  { FMC_D4_PORT,  FMC_D4_PIN  }, { FMC_D5_PORT,  FMC_D5_PIN  },
  { FMC_D6_PORT,  FMC_D6_PIN  }, { FMC_D7_PORT,  FMC_D7_PIN  },
  { FMC_D8_PORT,  FMC_D8_PIN  }, { FMC_D9_PORT,  FMC_D9_PIN  },
  { FMC_D10_PORT, FMC_D10_PIN }, { FMC_D11_PORT, FMC_D11_PIN },
  { FMC_D12_PORT, FMC_D12_PIN }, { FMC_D13_PORT, FMC_D13_PIN },
  { FMC_D14_PORT, FMC_D14_PIN }, { FMC_D15_PORT, FMC_D15_PIN },
  /* 地址总线 A0~A12 */
  { FMC_A0_PORT,  FMC_A0_PIN  }, { FMC_A1_PORT,  FMC_A1_PIN  },
  { FMC_A2_PORT,  FMC_A2_PIN  }, { FMC_A3_PORT,  FMC_A3_PIN  },
  { FMC_A4_PORT,  FMC_A4_PIN  }, { FMC_A5_PORT,  FMC_A5_PIN  },
  { FMC_A6_PORT,  FMC_A6_PIN  }, { FMC_A7_PORT,  FMC_A7_PIN  },
  { FMC_A8_PORT,  FMC_A8_PIN  }, { FMC_A9_PORT,  FMC_A9_PIN  },
  { FMC_A10_PORT, FMC_A10_PIN }, { FMC_A11_PORT, FMC_A11_PIN },
  { FMC_A12_PORT, FMC_A12_PIN },
  /* Bank 地址线与字节掩码 */
  { FMC_BA0_PORT, FMC_BA0_PIN }, { FMC_BA1_PORT, FMC_BA1_PIN },
  { FMC_NBL0_PORT, FMC_NBL0_PIN }, { FMC_NBL1_PORT, FMC_NBL1_PIN },
  /* 控制线 */
  { FMC_SDCLK_PORT,  FMC_SDCLK_PIN  },
  { FMC_SDNCAS_PORT, FMC_SDNCAS_PIN },
  { FMC_SDNRAS_PORT, FMC_SDNRAS_PIN },
  { FMC_SDNWE_PORT,  FMC_SDNWE_PIN  },
  { FMC_SDNE0_PORT,  FMC_SDNE0_PIN  },
  { FMC_SDCKE0_PORT, FMC_SDCKE0_PIN }
};

/* SDRAM 模式寄存器定义
 * 注意：本 HAL 版本没有提供 SDRAM_MODEREG_* 宏（它们属于 ST 的 BSP 层），
 *       所以这里按 JEDEC 器件手册自己定义。
 * 突发长度写 0b000（ST 的 SDRAM_MODEREG_BURST_LENGTH_1 也是 0）：
 *      实际突发由 FMC 控制器产生，SDRAM 自身的突发长度用不到。 */
#define SDRAM_MODEREG_BURST_LENGTH_1        (0x0000U)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL (0x0000U)
#define SDRAM_MODEREG_CAS_LATENCY_3         (0x0030U)   /* bit[6:4] = 011 */
#define SDRAM_MODEREG_OPERATING_STANDARD    (0x0000U)
#define SDRAM_MODEREG_WRITEBURST_SINGLE     (0x0200U)   /* bit9 = 1 单次写 */

/* 刷新计数器：COUNT = (64ms / 8192 行) x 90MHz - 20 = 683 */
#define SDRAM_REFRESH_COUNT     ((uint32_t)683U)

static SDRAM_HandleTypeDef s_hsdram;

static void fmc_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;   /* 90MHz 的 SDRAM 总线必须最高档 */
  gpio.Alternate = SDRAM_AF;

  for (uint32_t i = 0U; i < (sizeof(s_fmc_pins) / sizeof(s_fmc_pins[0])); i++)
  {
    gpio.Pin = s_fmc_pins[i].pin;
    HAL_GPIO_Init(s_fmc_pins[i].port, &gpio);
  }
}

static void sdram_send_command(uint32_t mode, uint32_t target,
                               uint32_t auto_refresh, uint32_t mode_reg)
{
  FMC_SDRAM_CommandTypeDef cmd = {0};

  cmd.CommandMode            = mode;
  cmd.CommandTarget          = target;
  cmd.AutoRefreshNumber      = auto_refresh;
  cmd.ModeRegisterDefinition = mode_reg;

  if (HAL_SDRAM_SendCommand(&s_hsdram, &cmd, 0xFFFFU) != HAL_OK)
  {
    Error_Handler();
  }
}

uint8_t BSP_SDRAM_Init(void)
{
  FMC_SDRAM_TimingTypeDef timing = {0};

  fmc_gpio_init();
  __HAL_RCC_FMC_CLK_ENABLE();

  /* ---- 器件参数：4 Bank x 13 行 x 9 列 x 16 位 ---- */
  s_hsdram.Instance = FMC_SDRAM_DEVICE;
  s_hsdram.Init.SDBank             = FMC_SDRAM_BANK1;
  s_hsdram.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;
  s_hsdram.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;
  s_hsdram.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_16;
  s_hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  s_hsdram.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_3;
  s_hsdram.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  s_hsdram.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;    /* SDCLK = HCLK/2 = 90MHz */
  s_hsdram.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
  s_hsdram.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_0;

  /* ---- 时序参数：按 90MHz（11.1ns/周期）算，单位是 SDRAM 时钟周期 ---- */
  timing.LoadToActiveDelay    = 2U;   /* tMRD = 2 clk  */
  timing.ExitSelfRefreshDelay = 7U;   /* tXSR = 70ns -> 7 clk */
  timing.SelfRefreshTime      = 4U;   /* tRAS = 42ns -> 4 clk */
  timing.RowCycleDelay        = 7U;   /* tRC  = 60ns -> 6 clk，取 7 留裕量 */
  timing.WriteRecoveryTime    = 3U;   /* tWR  = 2 clk + 裕量 */
  timing.RPDelay              = 2U;   /* tRP  = 18ns -> 2 clk */
  timing.RCDDelay             = 2U;   /* tRCD = 18ns -> 2 clk */

  if (HAL_SDRAM_Init(&s_hsdram, &timing) != HAL_OK)
  {
    return 1U;
  }

  /* ---- 上电初始化序列 ---- */
  sdram_send_command(FMC_SDRAM_CMD_CLK_ENABLE, FMC_SDRAM_CMD_TARGET_BANK1, 1U, 0U);
  BSP_DelayUs(100U);                                   /* 时钟稳定 >= 100us */

  sdram_send_command(FMC_SDRAM_CMD_PALL, FMC_SDRAM_CMD_TARGET_BANK1, 1U, 0U);

  sdram_send_command(FMC_SDRAM_CMD_AUTOREFRESH_MODE, FMC_SDRAM_CMD_TARGET_BANK1, 8U, 0U);

  sdram_send_command(FMC_SDRAM_CMD_LOAD_MODE, FMC_SDRAM_CMD_TARGET_BANK1, 1U,
                     (uint32_t)(SDRAM_MODEREG_BURST_LENGTH_1 |
                                SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
                                SDRAM_MODEREG_CAS_LATENCY_3 |
                                SDRAM_MODEREG_OPERATING_STANDARD |
                                SDRAM_MODEREG_WRITEBURST_SINGLE));

  /* ---- 刷新率 ---- */
  if (HAL_SDRAM_ProgramRefreshRate(&s_hsdram, SDRAM_REFRESH_COUNT) != HAL_OK)
  {
    return 2U;
  }

  return 0U;
}

uint32_t BSP_SDRAM_RefreshCount(void)
{
  return SDRAM_REFRESH_COUNT;
}

uint8_t BSP_SDRAM_Test(void)
{
  volatile uint16_t *base = (volatile uint16_t *)(SDRAM_BANK_ADDR + 0x00200000U);
  const uint32_t words = 64U * 1024U;          /* 只测 128KiB，够说明问题 */
  const uint16_t patterns[4] = { 0x0000U, 0xFFFFU, 0x5555U, 0xAAAAU };

  /* 图案 1：逐个地址写递增数据再读回比较 */
  for (uint32_t i = 0U; i < words; i++)
  {
    base[i] = (uint16_t)(i & 0xFFFFU);
  }
  for (uint32_t i = 0U; i < words; i++)
  {
    if (base[i] != (uint16_t)(i & 0xFFFFU))
    {
      return 1U;                               /* 数据线/地址线有问题 */
    }
  }

  /* 图案 2：全 0 / 全 1 / 交替，检查相邻位是否短路 */
  for (uint32_t p = 0U; p < 4U; p++)
  {
    for (uint32_t i = 0U; i < words; i++)
    {
      base[i] = patterns[p];
    }
    for (uint32_t i = 0U; i < words; i++)
    {
      if (base[i] != patterns[p])
      {
        return (uint8_t)(2U + p);
      }
    }
  }

  return 0U;
}

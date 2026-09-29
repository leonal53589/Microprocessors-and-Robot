/**
  ******************************************************************************
  * @file    ov5640.c
  * @brief   正点原子阿波罗 V2 + ATK-OV5640 摄像头驱动（SCCB 软件模拟 + 传感器寄存器配置）
  *
  *  本文件只做两件事：
  *    1) 用 PB4(SCL) / PB3(SDA) 软件模拟 SCCB 总线（板上有 4.7K 外部上拉，
  *       阿波罗板没有给摄像头留硬件 IIC，只能软件模拟）
  *    2) 硬件复位（PA15 低有效）-> 读产品 ID -> 按寄存器表把 OV5640 配成
  *       RGB565 + QVGA 320x240 输出
  *
  *  本文件【不】做什么：
  *    - 不配置 XCLK：ATK-OV5640 模块自带 24MHz 有源晶振，PA8 保持不动
  *    - 不配置任何 DCMI 引脚 / DCMI 寄存器：那些全部在 bsp_dcmi.c 里
  *    - 不使用 PWDN：P6 “OLED/CAMERA” 座子上根本没有引 PWDN
  *    - PB3/PB4 与 JTAG 的 JTDO/NJTRST 复用，这里当普通 GPIO 用，
  *      所以调试只能用 SWD（阿波罗板默认就是 SWD，不影响）
  *
  *  寄存器表取值来源（每一块在表里单独标注了 [A]/[B]/[C]）：
  *    [A] OmniVision OV5640 DVP 初始化序列（OmniVision《OV5640 DVP Application
  *        Note》，收录于 ArduCAM 的 ov5640_regs.h / OV5640YUV_Sensor_Dvp_Init）
  *    [B] OpenMV / esp32-camera 的 OV5640 默认寄存器组与寄存器位定义
  *        (sensors/private_include/ov5640_settings.h、ov5640_regs.h)
  *    [C] Linux mainline drivers/media/i2c/ov5640.c 的 QVGA/VGA 窗口与时序设置，
  *        以及 ArduCAM ov5640_regs.h 里 OV5640_RGB_QVGA / OV5640_QSXGA2QVGA 的
  *        RGB565 + QVGA 差值项
  ******************************************************************************
  */

#include "ov5640.h"
#include "pinmap.h"
#include "bsp_delay.h"

/* ==========================================================================
 *                              常量与宏
 * ========================================================================== */

/* SCCB 从机地址：7 位地址 0x3C，8 位形式写 0x78 / 读 0x79 */
#define OV5640_SCCB_ADDR_W    0x78U
#define OV5640_SCCB_ADDR_R    0x79U

#define OV5640_SCCB_RETRY     3U      /* 一次事务失败后的重试次数 */
#define OV5640_SCCB_DELAY_US  2U      /* SCL 边沿间延时 2us -> 约 200kHz，SCCB 上限 400kHz */

/* 产品 ID 寄存器（0x300A 高字节 + 0x300B 低字节 = 0x5640） */
#define OV5640_ID_REG_H       0x300AU
#define OV5640_ID_REG_L       0x300BU

/* 寄存器表里的“伪寄存器”：遇到它就延时 val 毫秒（val 是 uint8_t，最大 255ms）。
   0xFFFF 不是 OV5640 的真实寄存器，用来在纯寄存器表里表达手册要求的时序等待。 */
#define OV5640_REG_DELAY      0xFFFFU

/* RGB565 / QVGA 时序相关的几个“魔数”寄存器，单独提出来便于调参 */
#define OV5640_REG_PLL_CTRL1  0x3035U /* bit[7:4] 系统时钟分频，决定 PCLK */
#define OV5640_REG_OUT_W_H    0x3808U /* DVPHO[11:8] */
#define OV5640_REG_OUT_W_L    0x3809U /* DVPHO[7:0]  */
#define OV5640_REG_OUT_H_H    0x380aU /* DVPVO[10:8] */
#define OV5640_REG_OUT_H_L    0x380bU /* DVPVO[7:0]  */

/* ==========================================================================
 *                        SCCB 底层（软件模拟 IIC）
 *
 *  时序上 SCCB 与 IIC 几乎一致，最大区别是：
 *    - 只支持 1 个主设备，且【没有重复起始条件】，读寄存器时
 *      必须 “写地址 -> STOP -> 再 START -> 读数据”
 *    - 从机在第 9 个时钟只回 ACK/NAK（OV 器件不会拉低 SDA 表示忙）
 * ========================================================================== */

static uint32_t s_sda_pos = 0U;   /* SDA 引脚序号(0..15)，用于直接改 MODER/PUPDR */

/** 总线半周期延时：250kHz 左右，OV5640 的 SCCB 上限是 400kHz */
static void sccb_delay(void)
{
  BSP_DelayUs(OV5640_SCCB_DELAY_US);
}

/** SCL 电平（开漏输出，拉高靠板上 4.7K 上拉） */
static void sccb_scl(uint8_t level)
{
  HAL_GPIO_WritePin(OV5640_SCL_PORT, OV5640_SCL_PIN,
                    (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/** SDA 切成【开漏输出】：MODER = 01。直接写 MODER 比 HAL_GPIO_Init 快得多 */
static void sccb_sda_dir_out(void)
{
  OV5640_SDA_PORT->MODER = (OV5640_SDA_PORT->MODER & ~(3UL << (s_sda_pos * 2U)))
                         | (1UL << (s_sda_pos * 2U));
}

/** SDA 切成【输入】：MODER = 00，顺便打开内部上拉兜底（主力还是板上 4.7K） */
static void sccb_sda_dir_in(void)
{
  OV5640_SDA_PORT->MODER = OV5640_SDA_PORT->MODER & ~(3UL << (s_sda_pos * 2U));
  OV5640_SDA_PORT->PUPDR = (OV5640_SDA_PORT->PUPDR & ~(3UL << (s_sda_pos * 2U)))
                         | (1UL << (s_sda_pos * 2U));
}

/** SDA 输出 0/1（开漏：写 1 = 释放总线，靠上拉拉高） */
static void sccb_sda_out(uint8_t level)
{
  sccb_sda_dir_out();
  HAL_GPIO_WritePin(OV5640_SDA_PORT, OV5640_SDA_PIN,
                    (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/** 释放 SDA 并读回实际电平，返回 0 或 1 */
static uint8_t sccb_sda_in(void)
{
  sccb_sda_dir_in();
  sccb_delay();
  return (HAL_GPIO_ReadPin(OV5640_SDA_PORT, OV5640_SDA_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

/** 起始条件：SCL 为高时 SDA 由高变低 */
static void sccb_start(void)
{
  sccb_sda_out(1U);
  sccb_scl(1U);
  sccb_delay();
  sccb_sda_out(0U);
  sccb_delay();
  sccb_scl(0U);
  sccb_delay();
}

/** 停止条件：SCL 为高时 SDA 由低变高 */
static void sccb_stop(void)
{
  sccb_sda_out(0U);
  sccb_delay();
  sccb_scl(1U);
  sccb_delay();
  sccb_sda_out(1U);
  sccb_delay();
}

/** 发一个字节，返回 0 = 收到 ACK，1 = NAK/无响应 */
static uint8_t sccb_write_byte(uint8_t byte)
{
  uint8_t i;
  uint8_t nak;

  for (i = 0U; i < 8U; i++)
  {
    sccb_sda_out(((byte & 0x80U) != 0U) ? 1U : 0U);
    byte = (uint8_t)(byte << 1U);
    sccb_delay();
    sccb_scl(1U);
    sccb_delay();
    sccb_scl(0U);
    sccb_delay();
  }

  /* 第 9 个时钟：主机释放 SDA，从机拉低表示 ACK */
  (void)sccb_sda_in();
  sccb_delay();
  sccb_scl(1U);
  sccb_delay();

  nak = (sccb_sda_in() == 0U) ? 0U : 1U;

  sccb_scl(0U);
  sccb_delay();

  return nak;
}

/** 读一个字节；ack = 0 回 ACK，ack != 0 回 NAK（最后一个字节必须 NAK） */
static uint8_t sccb_read_byte(uint8_t ack)
{
  uint8_t i;
  uint8_t byte = 0U;

  sccb_sda_dir_in();          /* 主机释放 SDA，交给从机驱动 */
  sccb_delay();

  for (i = 0U; i < 8U; i++)
  {
    sccb_scl(1U);
    sccb_delay();
    byte = (uint8_t)(byte << 1U);
    if (sccb_sda_in() != 0U)
    {
      byte |= 0x01U;
    }
    sccb_scl(0U);
    sccb_delay();
  }

  /* 第 9 个时钟：主机应答 */
  sccb_sda_out((ack != 0U) ? 1U : 0U);
  sccb_delay();
  sccb_scl(1U);
  sccb_delay();
  sccb_scl(0U);
  sccb_delay();
  sccb_sda_out(1U);           /* 收尾把总线放开 */

  return byte;
}

/**
  * @brief  SCCB 写一个寄存器（单次尝试，不含重试）
  *
  *  事务格式 —— 本驱动采用【2 相写】：
  *      [0x78, reg_hi, reg_lo, val_hi, val_lo]
  *  为什么不用 OmniVision SCCB 规范里的 3 相写
  *  ([0x78,reg_hi,reg_lo][0x78,val_hi,val_lo][0x78,0x00,0x00])：
  *  3 相写只是把“寄存器地址阶段/数据阶段/空闲阶段”显式拆开，中间还要插 STOP；
  *  OV5640 对两者都接受，而 Linux ov5640.c、OpenMV、ArduCAM、正点原子例程
  *  用的都是 2 相写，少两次 START/STOP，出错概率更低、速度更快，所以选它。
  */
static OV5640_Status sccb_write_once(uint16_t reg, uint8_t val)
{
  OV5640_Status st = OV5640_OK;

  sccb_start();
  if (sccb_write_byte(OV5640_SCCB_ADDR_W) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else if (sccb_write_byte((uint8_t)((reg >> 8U) & 0x00FFU)) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else if (sccb_write_byte((uint8_t)(reg & 0x00FFU)) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else if (sccb_write_byte(val) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else
  {
    /* 正常结束 */
  }
  sccb_stop();

  return st;
}

/**
  * @brief  SCCB 读一个寄存器（单次尝试，不含重试）
  *
  *  事务格式 —— 2 相读：
  *      相 1：START [0x78, reg_hi, reg_lo] STOP     （先告诉从机要读哪个寄存器）
  *      相 2：START [0x79] 读 1 字节(NAK) STOP      （SCCB 没有重复起始条件，
  *                                                   只能 STOP 之后重新 START）
  */
static OV5640_Status sccb_read_once(uint16_t reg, uint8_t *val)
{
  OV5640_Status st = OV5640_OK;

  sccb_start();
  if (sccb_write_byte(OV5640_SCCB_ADDR_W) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else if (sccb_write_byte((uint8_t)((reg >> 8U) & 0x00FFU)) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else if (sccb_write_byte((uint8_t)(reg & 0x00FFU)) != 0U)
  {
    st = OV5640_ERR_SCCB;
  }
  else
  {
    /* 正常结束 */
  }
  sccb_stop();

  if (st == OV5640_OK)
  {
    sccb_delay();
    sccb_start();
    if (sccb_write_byte(OV5640_SCCB_ADDR_R) != 0U)
    {
      st = OV5640_ERR_SCCB;
      sccb_stop();
    }
    else
    {
      *val = sccb_read_byte(1U);    /* 只读 1 字节，最后一位回 NAK */
      sccb_stop();
    }
  }

  sccb_sda_out(1U);                 /* 不管成败都放开总线，避免卡死 */
  return st;
}

/* ==========================================================================
 *                               公共 SCCB 接口
 * ========================================================================== */

OV5640_Status OV5640_WriteReg(uint16_t reg, uint8_t val)
{
  uint32_t i;

  for (i = 0U; i < OV5640_SCCB_RETRY; i++)
  {
    if (sccb_write_once(reg, val) == OV5640_OK)
    {
      return OV5640_OK;
    }
    BSP_DelayUs(50U);               /* 失败后歇一下再试，给从机恢复时间 */
  }

  return OV5640_ERR_SCCB;
}

OV5640_Status OV5640_ReadReg(uint16_t reg, uint8_t *val)
{
  uint32_t i;

  if (val == NULL)
  {
    return OV5640_ERR_SCCB;         /* 枚举里没有“参数非法”，按总线错误返回 */
  }

  for (i = 0U; i < OV5640_SCCB_RETRY; i++)
  {
    if (sccb_read_once(reg, val) == OV5640_OK)
    {
      return OV5640_OK;
    }
    BSP_DelayUs(50U);
  }

  return OV5640_ERR_SCCB;
}

/* ==========================================================================
 *                         GPIO / 复位 / 静态状态
 * ========================================================================== */

/** 由 HAL 的 GPIO_PIN_x 位掩码反推出引脚序号 0..15（用于直接操作 MODER/PUPDR） */
static uint32_t ov5640_pin_pos(uint16_t pin)
{
  uint32_t mask = (uint32_t)pin;
  uint32_t pos  = 0U;

  while (((mask & 1U) == 0U) && (pos < 15U))
  {
    mask >>= 1U;
    pos++;
  }

  return pos;
}

/** SCCB 两个引脚初始化：都是开漏输出（靠板上 4.7K 上拉；内部上拉只作兜底） */
static void ov5640_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* PA15 = 复位，PB3/PB4 = SDA/SCL，两个端口的时钟都要开 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  s_sda_pos = ov5640_pin_pos(OV5640_SDA_PIN);

  gpio.Mode  = GPIO_MODE_OUTPUT_OD;
  gpio.Pull  = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gpio.Pin = OV5640_SCL_PIN;
  HAL_GPIO_Init(OV5640_SCL_PORT, &gpio);

  gpio.Pin = OV5640_SDA_PIN;
  HAL_GPIO_Init(OV5640_SDA_PORT, &gpio);

  sccb_scl(1U);
  sccb_sda_out(1U);
  BSP_DelayUs(10U);               /* 空总线一会儿，让从机自己回到空闲态 */
}

/** 硬件复位：PA15 拉低 >=5ms，再拉高，等模块晶振起振 + 内部复位（>=20ms） */
static void ov5640_hw_reset(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin   = OV5640_RST_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OV5640_RST_PORT, &gpio);

  HAL_GPIO_WritePin(OV5640_RST_PORT, OV5640_RST_PIN, GPIO_PIN_RESET);  /* 低电平复位 */
  BSP_DelayMs(10U);                                                   /* 手册要求 >=5ms */
  HAL_GPIO_WritePin(OV5640_RST_PORT, OV5640_RST_PIN, GPIO_PIN_SET);   /* 释放复位 */
  BSP_DelayMs(30U);                                                   /* 手册要求 >=20ms */
}

static uint8_t s_ready = 0U;      /* 1 = OV5640_Init() 曾经成功过 */

/* ==========================================================================
 *              OV5640 RGB565 + QVGA(320x240) 寄存器表
 *
 *  顺序即写入顺序。表中 {0xFFFF, n} 表示“延时 n 毫秒”（见 OV5640_REG_DELAY）。
 *  与来源表相比只做了两处结构调整，都已在注释里说明：
 *    1) 把“退出软件掉电 0x3008=0x02”统一挪到表尾，配置期间不出图；
 *    2) 把 50/60Hz 的 0x3c00 也提到上电之前写。
 * ========================================================================== */

typedef struct {
  uint16_t reg;
  uint8_t  val;
} Ov5640Reg;

static const Ov5640Reg ov5640_rgb565_qvga[] = {

  /* ---- 1. 软复位 / 软件掉电 / 引脚使能  [B] + [A] ---- */
  {0x3103, 0x13},   /* 系统时钟来自 PLL（bit[1]），软复位后必须重写 */
  {0x3008, 0x82},   /* bit[7]=1 软复位：所有寄存器回默认值 */
  {0xFFFF, 10},     /* 等软复位完成 */
  {0x3008, 0x42},   /* bit[6]=1 软件掉电：配置期间不出图 */
  {0x3017, 0xff},   /* DVP 输出引脚使能：VSYNC/HREF/PCLK/D[9:7] */
  {0x3018, 0xff},   /* DVP 输出引脚使能：D[5:0] */
  {0x302c, 0xc3},   /* 输出驱动能力 4x（bit[7:6]=11），排线长也不怕 */
  {0x4740, 0x21},   /* DVP 时序：PCLK 上升沿采样 + VSYNC 高有效 */
  {0x4713, 0x02},   /* JPEG 模式选择：DVP 直出用 mode2 */

  /* ---- 2. PLL 配置（24MHz 晶振 -> PCLK）  [A] + [C] ---- */
  {0x3034, 0x1a},   /* PLL bit divider = 2.5，10bit 模式 */
  {0x3035, 0x41},   /* 系统时钟分频 /4 -> PCLK 约 14MHz（QVGA 稳妥档）
                       想更快可改 0x21(约 28MHz) / 0x11(约 56MHz，DCMI 上限 54MHz) */
  {0x3036, 0x46},   /* PLL 倍频 = 70 */
  {0x3037, 0x13},   /* 预分频 /3 + 根分频 /2 */
  {0x3108, 0x01},   /* PCLK 根分频 = /1，sclk 根分频 = /2 */
  {0x3824, 0x01},   /* PCLK 手动分频比（配合 0x460c 的 bit1） */
  {0x460c, 0x22},   /* VFIFO/PCLK 手动模式，同时关掉 JPEG 尾标记 */

  /* ---- 3. 内部 MCU 复位 + 时钟门控  [B] ---- */
  {0x3000, 0x20},   /* 复位内部协处理器（MCU） */
  {0xFFFF, 10},
  {0x3002, 0x1c},   /* 复位 JFIFO / SFIFO / JPEG */
  {0x3004, 0xff},   /* 时钟全开 */
  {0x3006, 0xc3},   /* 关掉 JPEG2X / JPEG 时钟（DVP 直出用不到） */

  /* ---- 4. ISP 顶层开关  [B] ---- */
  {0x5000, 0xa7},   /* LENC + gamma + BPC + WPC + CIP 全开 */
  {0x5001, 0xa3},   /* SDE + 缩放 + UV 平均 + CMX + AWB 全开（缩放必须开） */
  {0x5003, 0x08},   /* 特殊数字效果使能 */
  {0x501d, 0x40},   /* 允许手动对比度偏移 */

  /* ---- 5. 模拟前端 / 时序基础寄存器  [A] ---- */
  {0x3630, 0x2e},
  {0x3632, 0xe2},
  {0x3633, 0x23},
  {0x3621, 0xe0},
  {0x3704, 0xa0},
  {0x3703, 0x5a},
  {0x3715, 0x78},
  {0x3717, 0x01},
  {0x370b, 0x60},
  {0x3705, 0x1a},
  {0x3905, 0x02},
  {0x3906, 0x10},
  {0x3901, 0x0a},
  {0x3731, 0x12},
  {0x3600, 0x08},   /* VCM 调试寄存器 */
  {0x3601, 0x33},
  {0x302d, 0x60},
  {0x3620, 0x52},
  {0x371b, 0x20},
  {0x471c, 0x50},   /* DVP/JPEG 控制，非 JPEG 输出取 0x50 */
  {0x3635, 0x1c},
  {0x3634, 0x40},
  {0x3622, 0x01},

  /* ---- 6. 50/60Hz 工频检测 + AEC/AGC 曝光控制  [B] ---- */
  {0x3c01, 0xa4},   /* bit[7]=1 手动选择工频；bit[5]=1 自动累加 */
  {0x3c04, 0x28},
  {0x3c05, 0x98},
  {0x3c06, 0x00},
  {0x3c07, 0x08},   /* 光照计 1 阈值[7:0] */
  {0x3c08, 0x00},
  {0x3c09, 0x1c},
  {0x3c0a, 0x9c},
  {0x3c0b, 0x40},
  {0x3c00, 0x04},   /* 50Hz 工频（国内）；60Hz 地区改成 0x00 */
  {0x3a02, 0x03},   /* 60Hz 最大曝光[15:8] = 0x03d8 */
  {0x3a03, 0xd8},   /* 60Hz 最大曝光[7:0] */
  {0x3a14, 0x03},   /* 50Hz 最大曝光[15:8] = 0x03d8 */
  {0x3a15, 0xd8},   /* 50Hz 最大曝光[7:0] */
  {0x3a08, 0x01},   /* 50Hz 步长[15:8] */
  {0x3a09, 0x27},
  {0x3a0a, 0x00},   /* 60Hz 步长[15:8] */
  {0x3a0b, 0xf6},
  {0x3a0d, 0x04},   /* 60Hz 单帧最大 band 数 */
  {0x3a0e, 0x03},   /* 50Hz 单帧最大 band 数 */
  {0x3a0f, 0x30},   /* AEC 稳定区间上限（进入） */
  {0x3a10, 0x28},   /* AEC 稳定区间下限（进入） */
  {0x3a11, 0x60},   /* AEC 快速区上限 */
  {0x3a1b, 0x30},   /* AEC 控制区间上限（退出） */
  {0x3a1e, 0x26},   /* AEC 控制区间下限（退出） */
  {0x3a1f, 0x14},   /* AEC 快速区下限 */
  {0x3a13, 0x43},   /* 前置增益使能 + 增益 1x */
  {0x3a18, 0x00},   /* AGC 增益上限[9:8] */
  {0x3a19, 0xf8},   /* AGC 增益上限[7:0] -> 0x3f8 */
  {0x3503, 0x00},   /* AEC/AGC 全自动（bit0/bit1 = 0） */
  {0x3406, 0x00},   /* AWB 增益自动（手动增益 0x3400~0x3405 不生效） */

  /* ---- 7. 黑电平校正 BLC  [B] ---- */
  {0x4001, 0x02},
  {0x4004, 0x02},

  /* ---- 8. 自动白平衡 AWB 参数  [B] ---- */
  {0x5180, 0xff},
  {0x5181, 0xf2},
  {0x5182, 0x00},
  {0x5183, 0x14},
  {0x5184, 0x25},
  {0x5185, 0x24},
  {0x5186, 0x09},
  {0x5187, 0x09},
  {0x5188, 0x09},
  {0x5189, 0x75},
  {0x518a, 0x54},
  {0x518b, 0xe0},
  {0x518c, 0xb2},
  {0x518d, 0x42},
  {0x518e, 0x3d},
  {0x518f, 0x56},
  {0x5190, 0x46},
  {0x5191, 0xf8},   /* AWB 上限 */
  {0x5192, 0x04},   /* AWB 下限 */
  {0x5193, 0x70},
  {0x5194, 0xf0},
  {0x5195, 0xf0},
  {0x5196, 0x03},
  {0x5197, 0x01},
  {0x5198, 0x04},
  {0x5199, 0x12},
  {0x519a, 0x04},
  {0x519b, 0x00},
  {0x519c, 0x06},
  {0x519d, 0x82},
  {0x519e, 0x38},

  /* ---- 9. 颜色矩阵 CMX（RGB->YUV，饱和度）  [B] ---- */
  {0x5381, 0x1e},   /* CMX 使能 + 偏移 */
  {0x5382, 0x5b},
  {0x5383, 0x08},
  {0x5384, 0x0a},
  {0x5385, 0x7e},
  {0x5386, 0x88},
  {0x5387, 0x7c},
  {0x5388, 0x6c},
  {0x5389, 0x10},
  {0x538a, 0x01},
  {0x538b, 0x98},

  /* ---- 10. 锐度 / 降噪 CIP  [B] ---- */
  {0x5300, 0x10},
  {0x5301, 0x10},
  {0x5302, 0x18},
  {0x5303, 0x19},
  {0x5304, 0x10},
  {0x5305, 0x10},
  {0x5306, 0x08},   /* 降噪强度 */
  {0x5307, 0x16},
  {0x5308, 0x40},   /* 锐度自动 */
  {0x5309, 0x10},
  {0x530a, 0x10},
  {0x530b, 0x04},
  {0x530c, 0x06},

  /* ---- 11. Gamma 曲线  [B] ---- */
  {0x5480, 0x01},
  {0x5481, 0x00},
  {0x5482, 0x1e},
  {0x5483, 0x3b},
  {0x5484, 0x58},
  {0x5485, 0x66},
  {0x5486, 0x71},
  {0x5487, 0x7d},
  {0x5488, 0x83},
  {0x5489, 0x8f},
  {0x548a, 0x98},
  {0x548b, 0xa6},
  {0x548c, 0xb8},
  {0x548d, 0xca},
  {0x548e, 0xd7},
  {0x548f, 0xe3},
  {0x5490, 0x1d},

  /* ---- 12. SDE / UV 调整（亮度对比度色度）  [B] ---- */
  {0x5580, 0x06},   /* 打开亮度、对比度调整 */
  {0x5583, 0x40},
  {0x5584, 0x10},
  {0x5586, 0x20},   /* 对比度 */
  {0x5587, 0x00},   /* 亮度 */
  {0x5588, 0x00},
  {0x5589, 0x10},
  {0x558a, 0x00},
  {0x558b, 0xf8},

  /* ---- 13. ISP 输入窗口 + QVGA 输出 + 时序 + 子采样  [A] + [C] ---- */
  {0x3800, 0x00},   /* ISP 窗口起点 X[11:8] */
  {0x3801, 0x00},   /* ISP 窗口起点 X[7:0]  */
  {0x3802, 0x00},   /* ISP 窗口起点 Y[10:8] */
  {0x3803, 0x04},   /* ISP 窗口起点 Y[7:0] = 4（传感器上面 4 行 dummy） */
  {0x3804, 0x0a},   /* ISP 窗口终点 X[11:8] */
  {0x3805, 0x3f},   /* -> 0x0a3f = 2623 */
  {0x3806, 0x07},   /* ISP 窗口终点 Y[10:8] */
  {0x3807, 0x9b},   /* -> 0x079b = 1947 */
  {0x3808, 0x01},   /* DVPHO = 320 (0x0140) —— QVGA 宽 */
  {0x3809, 0x40},
  {0x380a, 0x00},   /* DVPVO = 240 (0x00f0) —— QVGA 高 */
  {0x380b, 0xf0},
  {0x380c, 0x07},   /* HTS = 1896 (0x0768) */
  {0x380d, 0x68},
  {0x380e, 0x03},   /* VTS = 984  (0x03d8) */
  {0x380f, 0xd8},
  {0x3810, 0x00},   /* ISP 水平偏移 X[11:8] */
  {0x3811, 0x10},   /* -> 16 列 dummy */
  {0x3812, 0x00},   /* ISP 垂直偏移 Y[10:8] */
  {0x3813, 0x06},   /* -> 6 行 */
  {0x3814, 0x31},   /* X 子采样增量：奇数行 3、偶数行 1（2x 子采样） */
  {0x3815, 0x31},   /* Y 子采样增量：同上 */
  {0x3820, 0x41},   /* 传感器不翻转；ISP 垂直翻转关、垂直 binning 关 */
  {0x3821, 0x01},   /* ISP 水平镜像关；水平 binning 开 */
  /* 子采样通路的“必须值”三件套（来自 OmniVision DVP 序列）：
     开 2x 子采样时 0x3618=0x00 / 0x3612=0x29 / 0x3709=0x52 / 0x370c=0x03；
     若关掉子采样（0x3814/0x3815=0x11），这一组要改成
     0x3618=0x04 / 0x3612=0x2b / 0x3709=0x12 / 0x370c=0x00。 */
  {0x3618, 0x00},
  {0x3612, 0x29},
  {0x3708, 0x64},
  {0x3709, 0x52},
  {0x370c, 0x03},

  /* ---- 14. 输出格式：RGB565 + DVP 细节  [B] + [A] ---- */
  {0x4711, 0x00},   /* DVP 控制寄存器：本序列不用它，显式写回默认 0，
                       保证从 JPEG 模式切回来时状态干净 */
  {0x4300, 0x61},   /* 格式控制 00：bit[7:4]=0110 -> RGB565，bit[3:0]=1 -> BGR */
  {0x501f, 0x01},   /* ISP 输出格式选择：001 = RGB（RGB565 必须写 0x01） */
  {0x503d, 0x00},   /* 关闭测试彩条/测试图案 */

  /* ---- 15. 结束：退出软件掉电，开始出图  [B] ---- */
  {0x3008, 0x02},   /* bit6=0 退出软件掉电 -> 开始输出 DVP 数据流 */
  {0xFFFF, 200},    /* 给 AEC/AWB 一点收敛时间，第一帧才不会是黑的 */
};

/* 说明：0x5800~0x583d（镜头阴影校正 LENC 系数，OmniVision DVP 序列里那 62 个值）
   没有放进本表。OpenMV/esp32-camera 的官方默认寄存器组同样不含这组系数，
   芯片会用默认/OTP 值，实测出图正常；如果整机上发现四角偏暗、偏色，
   再参考 OmniVision《OV5640 DVP Application Note》把这一组补进第 8~12 块之间。 */

#define OV5640_TABLE_LEN   (sizeof(ov5640_rgb565_qvga) / sizeof(ov5640_rgb565_qvga[0]))

/** 按表写寄存器；遇到伪寄存器 OV5640_REG_DELAY 就延时 val 毫秒 */
static OV5640_Status ov5640_write_array(const Ov5640Reg *table, uint32_t len)
{
  uint32_t i;

  for (i = 0U; i < len; i++)
  {
    if (table[i].reg == OV5640_REG_DELAY)
    {
      BSP_DelayMs(table[i].val);
      continue;
    }

    if (OV5640_WriteReg(table[i].reg, table[i].val) != OV5640_OK)
    {
      return OV5640_ERR_SCCB;
    }
  }

  return OV5640_OK;
}

/* ==========================================================================
 *                                 公共 API
 * ========================================================================== */

OV5640_Status OV5640_Probe(uint16_t *pid)
{
  uint8_t hi = 0U;
  uint8_t lo = 0U;
  uint16_t id;
  OV5640_Status st;

  st = OV5640_ReadReg(OV5640_ID_REG_H, &hi);
  if (st == OV5640_OK)
  {
    st = OV5640_ReadReg(OV5640_ID_REG_L, &lo);
  }
  if (st != OV5640_OK)
  {
    return st;                    /* SCCB 都不通，谈不上 ID */
  }

  id = (uint16_t)(((uint16_t)hi << 8U) | (uint16_t)lo);
  if (pid != NULL)
  {
    *pid = id;
  }

  return (id == OV5640_CHIP_ID) ? OV5640_OK : OV5640_ERR_ID;
}

/**
  * @brief  初始化 OV5640：配 SCCB 引脚 -> 硬件复位 -> 查 ID -> 写 RGB565 QVGA 表
  *
  *  可重复调用（幂等）：每次都会重新硬件复位 + 重新写整张表，失败后可以再调一次。
  *  任何一步失败都会返回错误码并清掉“就绪”标志。
  */
OV5640_Status OV5640_Init(void)
{
  uint16_t pid = 0U;
  OV5640_Status st;

  s_ready = 0U;

  ov5640_gpio_init();
  ov5640_hw_reset();

  st = OV5640_Probe(&pid);
  if (st != OV5640_OK)
  {
    return st;                    /* OV5640_ERR_SCCB 或 OV5640_ERR_ID */
  }

  st = ov5640_write_array(ov5640_rgb565_qvga, (uint32_t)OV5640_TABLE_LEN);
  if (st != OV5640_OK)
  {
    return st;
  }

  s_ready = 1U;
  return OV5640_OK;
}

/**
  * @brief  切换输出尺寸
  *
  *  只改 PLL 系统时钟分频(0x3035) 和输出窗口(0x3808~0x380b)：
  *    - 320x240：0x3035 = 0x41（PCLK 约 14MHz，最稳）
  *    - 640x480：0x3035 = 0x21（PCLK 约 28MHz，DCMI 上限 54MHz）
  *  缩放(0x5001 bit5) 在初始化表里已经是开着的，所以这里不用再动。
  *  ISP 输入窗口/HTS/VTS 保持不变（像素多的那一档靠缩放器出图）。
  */
OV5640_Status OV5640_SetOutputSize(uint16_t w, uint16_t h)
{
  OV5640_Status st;

  if ((w == OV5640_QVGA_WIDTH) && (h == OV5640_QVGA_HEIGHT))
  {
    st = OV5640_WriteReg(OV5640_REG_PLL_CTRL1, 0x41U);
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_W_H, 0x01U); }
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_W_L, 0x40U); }
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_H_H, 0x00U); }
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_H_L, 0xf0U); }
  }
  else if ((w == OV5640_VGA_WIDTH) && (h == OV5640_VGA_HEIGHT))
  {
    st = OV5640_WriteReg(OV5640_REG_PLL_CTRL1, 0x21U);
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_W_H, 0x02U); }
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_W_L, 0x80U); }
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_H_H, 0x01U); }
    if (st == OV5640_OK) { st = OV5640_WriteReg(OV5640_REG_OUT_H_L, 0xe0U); }
  }
  else
  {
    /* 枚举里没有“不支持的参数”这一项，用 TIMEOUT 明确表示“这个尺寸不支持”。
       目前只支持 320x240 与 640x480（见 ov5640.h 的宏）。 */
    return OV5640_ERR_TIMEOUT;
  }

  return st;
}

uint8_t OV5640_IsReady(void)
{
  return (s_ready != 0U) ? 1U : 0U;
}

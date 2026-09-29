/**
  ******************************************************************************
  * @file    w25qxx.c
  * @brief   W25Q 系列 SPI NOR Flash 驱动实现（板载 W25Q256，SPI5）
  *
  *  硬件（pinmap.h 第 5 节，IO 分配表逐脚核对，板上已连好，无需飞线）：
  *      SPI5_SCK  = PF7   AF5
  *      SPI5_MISO = PF8   AF5
  *      SPI5_MOSI = PF9   AF5
  *      F_CS      = PF6   普通推挽输出，软件控制，空闲高电平
  *
  *  ★ 4 字节地址模式（本驱动的核心）：
  *      W25Q256 容量 32MB = 2^25 字节，需要 25 位地址；标准 SPI 命令
  *      （0x03/0x02/0x20/0xD8）只带 24 位地址，最多覆盖 16MB，访问高 16MB
  *      时地址会回卷到低 16MB（写操作直接把低 16MB 的图库/字库写坏）。
  *      所以 W25Q_Init() 里发 0xB7（Enter 4-Byte Address Mode），之后只用
  *      4 字节地址命令：0x13 读 / 0x12 页编程 / 0x21 4KB 扇区擦除 /
  *      0xDC 64KB 块擦除。
  *      读命令选 0x13（Read Data with 4-Byte Address）而不是 0x0C（Fast Read
  *      with 4-Byte Address）：22.5MHz 的 SCK 远低于 0x13 的 50MHz 上限，
  *      而 0x13 不需要额外的 dummy 字节，代码更短。
  *      0xB7 是【易失】设置，掉电/复位后失效，因此每次上电都要调用
  *      W25Q_Init()（本文件的公共 API 还有懒初始化兜底，见 w25q_ensure_init）。
  *      本板 W25Q256 原生支持 4 字节地址模式，不需要"3 字节命令 + 扩展地址
  *      寄存器（0xC5 写 / 0xC8 读 ExtAdd）"那套老器件回退方案；为了换片子
  *      也能跑，识别到 W25Q128(0xEF4018)/W25Q64(0xEF4017) 时自动停在 3 字节
  *      模式并改用 0x03/0x02/0x20/0xD8，绝不发 0xB7。
  *
  *  时序：tSHSL（两次事务之间 CS 的高电平时间）>= 50ns，由 w25q_delay() 的空
  *        循环保证；tCLCH/tCHCL 由 22.5MHz 的 SPI 时钟保证。所有 WIP 等待都
  *        用本文件自己的计数环做超时，全程不使用 HAL_Delay()（不依赖 SysTick
  *        中断，也不影响 delay 模块）。
  ******************************************************************************
  */

#include "w25qxx.h"
#include "pinmap.h"

#include <stddef.h>

/* ==========================================================================
 * 1. 命令码 / 状态位 / 器件 JEDEC ID
 * ========================================================================== */
#define W25Q_CMD_WRITE_ENABLE     0x06U   /* 写使能，编程/擦除前必发          */
#define W25Q_CMD_READ_STATUS1     0x05U   /* 读状态寄存器 1，bit0 = WIP       */
#define W25Q_CMD_JEDEC_ID         0x9FU   /* 读 JEDEC ID，3 字节              */
#define W25Q_CMD_READ_DATA4       0x13U   /* 4 字节地址读（4 字节模式命令）   */
#define W25Q_CMD_PAGE_PROGRAM4    0x12U   /* 4 字节地址页编程（256B/页）      */
#define W25Q_CMD_SECTOR_ERASE4    0x21U   /* 4 字节地址 4KB 扇区擦除          */
#define W25Q_CMD_BLOCK_ERASE64K4  0xDCU   /* 4 字节地址 64KB 块擦除           */
#define W25Q_CMD_ENTER_4BYTE      0xB7U   /* 进入 4 字节地址模式（易失）      */
#define W25Q_CMD_RELEASE_PWRDN    0xABU   /* 释放深度掉电 / 读器件 ID         */
#define W25Q_CMD_READ_DATA3       0x03U   /* 3 字节地址读（W25Q128/64 用）    */
#define W25Q_CMD_PAGE_PROGRAM3    0x02U   /* 3 字节地址页编程                 */
#define W25Q_CMD_SECTOR_ERASE3    0x20U   /* 3 字节地址 4KB 扇区擦除          */
#define W25Q_CMD_BLOCK_ERASE64K3  0xD8U   /* 3 字节地址 64KB 块擦除           */

#define W25Q_SR1_WIP              0x01U   /* 擦/写进行中（Write In Progress） */
#define W25Q_SR1_WEL              0x02U   /* 写使能锁存已置位                 */

#define W25Q_JEDEC_W25Q256        0xEF4019U
#define W25Q_JEDEC_W25Q128        0xEF4018U
#define W25Q_JEDEC_W25Q64         0xEF4017U

/* ==========================================================================
 * 2. 超时 / 延时参数
 * ========================================================================== */
#define W25Q_HSPI_TIMEOUT_MS      100U      /* 单次 HAL 事务超时；正常一次 < 100us */
#define W25Q_TSHSL_LOOPS          64U       /* CS 拉高保持 >= 50ns(tSHSL)，余量充足 */
#define W25Q_TRES_LOOPS           8192U     /* 0xAB 唤醒等待 tRES1，约几十 us       */

/* WIP 轮询次数上限（单位是"轮询次数"，不是精确毫秒）：
 *   一次轮询 = 发 0x05(1B) + 收状态(1B) = 2 字节 @22.5MHz 约 0.7us，再加上两次
 *   HAL 调用的软件开销，这里保守按 5us/次 估算：
 *       4000   次 x 5us 约 20ms   （页编程   tPP typ 0.7ms / max 3ms  ）
 *       100000 次 x 5us 约 500ms  （4KB 擦除 tSE typ 45ms  / max 400ms）
 *       600000 次 x 5us 约 3s     （64KB 擦除 tBE typ 150ms / max 2s  ）
 *   若主频 / 优化等级 / SPI 分频改了，这三个值需要按实测重新标定。 */
#define W25Q_TMO_PROGRAM          4000U
#define W25Q_TMO_ERASE4K          100000U
#define W25Q_TMO_ERASE64K         600000U

/* 页编程回读校验：1 = 每条页编程后回读比对，不一致返回 W25Q_ERR_WRITE。
 * 注意 NOR 只能 1->0，往未擦除区域写必然校验失败，上层必须先擦后写；
 * 批量写入且介质可信时改成 0 可省掉一半总线时间。 */
#define W25Q_WRITE_VERIFY         1

/* ==========================================================================
 * 3. 驱动静态状态
 * ========================================================================== */
static SPI_HandleTypeDef s_hspi;        /* SPI5 句柄（阻塞式，无中断 / 无 DMA） */
static uint8_t  s_hw_ready = 0U;        /* 时钟 / GPIO / SPI5 已配置        */
static uint8_t  s_inited   = 0U;        /* W25Q_Init() 成功过（W25Q_IsReady）*/
static uint8_t  s_4byte    = 0U;        /* 1 = 芯片处于 4 字节地址模式      */
static uint32_t s_capacity = 0U;        /* 检测到的容量（字节）             */

/* ==========================================================================
 * 4. 底层：CS / 微延时 / SPI 收发
 * ========================================================================== */
static void w25q_cs_low(void)
{
  HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_RESET);
}

static void w25q_cs_high(void)
{
  HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_SET);
}

/* 空循环微延时：用于 tSHSL / tRES1 这类小等待，故意不用 HAL_Delay()（不依赖中断） */
static void w25q_delay(volatile uint32_t loops)
{
  while (loops != 0U)
  {
    __NOP();
    loops--;
  }
}

/* HAL 返回值 -> W25Q_Status */
static W25Q_Status w25q_hal_status(HAL_StatusTypeDef st)
{
  if (st == HAL_OK)
  {
    return W25Q_OK;
  }
  if (st == HAL_TIMEOUT)
  {
    return W25Q_ERR_TIMEOUT;
  }
  return W25Q_ERR_WRITE;
}

static W25Q_Status w25q_tx(const uint8_t *buf, uint16_t len)
{
  /* HAL 的形参不是 const，这里只读不改，强制转换是安全的 */
  return w25q_hal_status(HAL_SPI_Transmit(&s_hspi, (uint8_t *)buf, len, W25Q_HSPI_TIMEOUT_MS));
}

static W25Q_Status w25q_rx(uint8_t *buf, uint16_t len)
{
  return w25q_hal_status(HAL_SPI_Receive(&s_hspi, buf, len, W25Q_HSPI_TIMEOUT_MS));
}

/* ==========================================================================
 * 5. 状态寄存器 / 写使能 / 忙等待 / 地址头
 * ========================================================================== */

/* 读状态寄存器 1（0x05），bit0 = WIP */
static W25Q_Status w25q_read_status1(uint8_t *sr)
{
  const uint8_t cmd = W25Q_CMD_READ_STATUS1;
  W25Q_Status st;

  w25q_cs_low();
  st = w25q_tx(&cmd, 1U);
  if (st == W25Q_OK)
  {
    st = w25q_rx(sr, 1U);
  }
  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);
  return st;
}

/* 写使能：发 0x06 后回读 SR1 确认 WEL=1（WEL 在每条编程/擦除命令后自动清零） */
static W25Q_Status w25q_write_enable(void)
{
  const uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
  uint8_t sr = 0U;
  W25Q_Status st;

  w25q_cs_low();
  st = w25q_tx(&cmd, 1U);
  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);
  if (st != W25Q_OK)
  {
    return st;
  }

  st = w25q_read_status1(&sr);
  if (st != W25Q_OK)
  {
    return st;
  }
  if ((sr & W25Q_SR1_WEL) == 0U)
  {
    return W25Q_ERR_WRITE;      /* 写使能没生效：介质异常 */
  }
  return W25Q_OK;
}

/* 有界等待 WIP 清零；计数用尽 -> W25Q_ERR_TIMEOUT（不用 HAL_Delay） */
static W25Q_Status w25q_wait_ready(uint32_t timeout)
{
  uint8_t sr = 0U;
  W25Q_Status st;

  while (timeout != 0U)
  {
    st = w25q_read_status1(&sr);
    if (st != W25Q_OK)
    {
      return st;
    }
    if ((sr & W25Q_SR1_WIP) == 0U)
    {
      return W25Q_OK;
    }
    timeout--;
  }
  return W25Q_ERR_TIMEOUT;
}

/* 拼"命令 + 地址"头：4 字节模式 5 字节，3 字节模式 4 字节；out 至少 5 字节 */
static uint8_t w25q_build_header(uint8_t cmd4, uint8_t cmd3, uint32_t addr, uint8_t *out)
{
  if (s_4byte != 0U)
  {
    out[0] = cmd4;
    out[1] = (uint8_t)((addr >> 24) & 0xFFU);
    out[2] = (uint8_t)((addr >> 16) & 0xFFU);
    out[3] = (uint8_t)((addr >> 8) & 0xFFU);
    out[4] = (uint8_t)(addr & 0xFFU);
    return 5U;
  }

  out[0] = cmd3;
  out[1] = (uint8_t)((addr >> 16) & 0xFFU);
  out[2] = (uint8_t)((addr >> 8) & 0xFFU);
  out[3] = (uint8_t)(addr & 0xFFU);
  return 4U;
}

/* ==========================================================================
 * 6. 片级操作：JEDEC ID / 唤醒 / 硬件初始化 / 擦除 / 回读校验
 * ========================================================================== */

/* 读 24 位 JEDEC ID（0x9F）：0xEF4019 = W25Q256 */
static W25Q_Status w25q_read_jedec_id(uint32_t *id)
{
  const uint8_t cmd = W25Q_CMD_JEDEC_ID;
  uint8_t rx[3] = { 0U, 0U, 0U };
  W25Q_Status st;

  w25q_cs_low();
  st = w25q_tx(&cmd, 1U);
  if (st == W25Q_OK)
  {
    st = w25q_rx(rx, 3U);
  }
  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);

  if (st == W25Q_OK)
  {
    *id = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | (uint32_t)rx[2];
  }
  return st;
}

/* 释放深度掉电 / 读器件 ID（0xAB + 3 个空字节 + 1 字节器件 ID）。
 * 芯片若被 0xB9 打入深度掉电，只有它能唤醒；正常上电调用也无害。 */
static W25Q_Status w25q_release_power_down(void)
{
  const uint8_t cmd = W25Q_CMD_RELEASE_PWRDN;
  uint8_t dummy[3] = { 0xFFU, 0xFFU, 0xFFU };   /* 3 个空字节，只为产生时钟 */
  uint8_t dev_id = 0U;                          /* W25Q64=0x17 / 128=0x18 / 256=0x19 */
  W25Q_Status st;

  w25q_cs_low();
  st = w25q_tx(&cmd, 1U);
  if (st == W25Q_OK)
  {
    st = w25q_tx(dummy, 3U);
  }
  if (st == W25Q_OK)
  {
    st = w25q_rx(&dev_id, 1U);
  }
  w25q_cs_high();
  w25q_delay(W25Q_TRES_LOOPS);   /* 等 tRES1，唤醒完成前不接受新命令 */

  (void)dev_id;                  /* 只作参考，器件判定交给 0x9F JEDEC ID */
  return st;
}

/* SPI5 + 4 个引脚的一次性初始化；s_hw_ready 保证 HAL_SPI_Init 不会被重复调用 */
static void w25q_hw_init(void)
{
  GPIO_InitTypeDef gpio = { 0 };

  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_SPI5_CLK_ENABLE();

  /* ---- SPI5：主机、模式 0、8 位、MSB First、软件 NSS、无 CRC、无 TI 模式 ----
   * 波特率：SPI5 挂在 APB2（本工程 HCLK = 180MHz、APB2 分频 = 1 -> 90MHz），
   *         SCK = fPCLK2 / 预分频：
   *             SPI_BAUDRATEPRESCALER_2 -> 90 / 2 = 45   MHz
   *             SPI_BAUDRATEPRESCALER_4 -> 90 / 4 = 22.5 MHz   <-- 本驱动选用
   *             SPI_BAUDRATEPRESCALER_8 -> 90 / 8 = 11.25 MHz
   *         选 4 分频的理由：22.5MHz 在 22.5~45MHz 目标区间内，且 0x13 普通读的
   *         fR 上限是 50MHz，45MHz（2 分频）余量偏小；人脸图库一次读几 KB，
   *         22.5MHz 已经够快。要更高吞吐可改成 SPI_BAUDRATEPRESCALER_2。
   * NSS 用软件：CS 是 PF6 普通 GPIO，HAL 内部会把 SSI 置 1（主机模式必须），
   * 不占用硬件 NSS 引脚，也不会在收发间隙自动抬起 CS。 */
  s_hspi.Instance               = W25Q_SPI;
  s_hspi.Init.Mode              = SPI_MODE_MASTER;
  s_hspi.Init.Direction         = SPI_DIRECTION_2LINES;
  s_hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
  s_hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;         /* CPOL = 0        */
  s_hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;          /* CPHA = 0 -> 模式 0 */
  s_hspi.Init.NSS               = SPI_NSS_SOFT;
  s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  s_hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  s_hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
  s_hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  s_hspi.Init.CRCPolynomial     = 10U;

  if (HAL_SPI_Init(&s_hspi) != HAL_OK)
  {
    Error_Handler();      /* 与本工程其它 BSP 一致：外设初始化失败进死循环 */
  }

  /* ---- GPIO 放在 HAL_SPI_Init() 之后配置：SPI5 在本工程没有自定义 MspInit，
   *      这里显式配置保证引脚设置最终生效（4 根线按 pinmap.h 各查各的端口）。
   *      CS 先写 1（输出寄存器预置高电平）再切成输出，避免上电瞬间拉低 CS
   *      被从机误当成一条命令。 ---- */
  HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_SET);

  gpio.Pin   = W25Q_CS_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(W25Q_CS_PORT, &gpio);

  /* SCK / MOSI 复用推挽输出，MISO 复用输入：上拉可以避免 CS 无效时 MISO 悬空 */
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_PULLUP;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = W25Q_SPI_AF;

  gpio.Pin = W25Q_SCK_PIN;
  HAL_GPIO_Init(W25Q_SCK_PORT, &gpio);

  gpio.Pin = W25Q_MISO_PIN;
  HAL_GPIO_Init(W25Q_MISO_PORT, &gpio);

  gpio.Pin = W25Q_MOSI_PIN;
  HAL_GPIO_Init(W25Q_MOSI_PORT, &gpio);

  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);
}

/* 公共 API 的懒初始化：状态码枚举里没有"未初始化"，所以没 Init 过就补一次，
 * 保证 face_db 之类的调用者即使漏调 W25Q_Init() 也能正常工作 */
static W25Q_Status w25q_ensure_init(void)
{
  if (s_inited != 0U)
  {
    return W25Q_OK;
  }
  if (W25Q_Init() == 0U)
  {
    return W25Q_OK;
  }
  return W25Q_ERR_ID;
}

/* 通用擦除：写使能 -> 命令 + 4/3 字节地址 -> 等 WIP 清零 */
static W25Q_Status w25q_erase_cmd(uint8_t cmd4, uint8_t cmd3, uint32_t addr, uint32_t timeout)
{
  uint8_t header[5];
  uint8_t hlen;
  W25Q_Status st;

  if (addr >= s_capacity)
  {
    return W25Q_ERR_ADDRESS;
  }

  st = w25q_write_enable();
  if (st != W25Q_OK)
  {
    return st;
  }

  hlen = w25q_build_header(cmd4, cmd3, addr, header);
  w25q_cs_low();
  st = w25q_tx(header, hlen);
  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);
  if (st != W25Q_OK)
  {
    return st;
  }

  return w25q_wait_ready(timeout);
}

#if (W25Q_WRITE_VERIFY != 0)
/* 回读校验刚写入的数据：len 必须 <= 一页（W25Q_Write 保证） */
static W25Q_Status w25q_verify(uint32_t addr, const uint8_t *buf, uint32_t len)
{
  uint8_t tmp[W25Q_PAGE_SIZE];
  uint32_t i;
  W25Q_Status st;

  if (len > W25Q_PAGE_SIZE)
  {
    return W25Q_ERR_PARAM;
  }

  st = W25Q_Read(addr, tmp, len);
  if (st != W25Q_OK)
  {
    return st;
  }
  for (i = 0U; i < len; i++)
  {
    if (tmp[i] != buf[i])
    {
      return W25Q_ERR_WRITE;    /* 写入失败或目标区域未擦除干净 */
    }
  }
  return W25Q_OK;
}
#endif

/* ==========================================================================
 * 7. 公共 API
 * ========================================================================== */
uint8_t W25Q_Init(void)
{
  const uint8_t cmd_en4b = W25Q_CMD_ENTER_4BYTE;
  uint32_t id = 0U;
  W25Q_Status st;

  if (s_hw_ready == 0U)
  {
    w25q_hw_init();
    s_hw_ready = 1U;
  }

  /* 幂等：每次调用都重建状态（0xB7 易失，掉电/软复位后必须重发） */
  s_inited   = 0U;
  s_4byte    = 0U;
  s_capacity = 0U;

  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);

  /* 1) 释放深度掉电（0xAB，可选步骤）：保证后续命令一定被响应 */
  st = w25q_release_power_down();
  if (st != W25Q_OK)
  {
    return (uint8_t)st;
  }

  /* 2) 读 JEDEC ID 认器件 */
  st = w25q_read_jedec_id(&id);
  if (st != W25Q_OK)
  {
    return (uint8_t)st;
  }

  /* 3) 器件表：只有 W25Q256 需要（也支持）4 字节地址模式 */
  if (id == W25Q_JEDEC_W25Q256)
  {
    s_capacity = 32U * 1024U * 1024U;   /* 32MB = 2^25，必须用 4 字节地址 */
    s_4byte    = 1U;
  }
  else if (id == W25Q_JEDEC_W25Q128)
  {
    s_capacity = 16U * 1024U * 1024U;   /* 16MB，24 位地址刚好，停在 3 字节模式 */
  }
  else if (id == W25Q_JEDEC_W25Q64)
  {
    s_capacity = 8U * 1024U * 1024U;    /* 8MB，3 字节模式 */
  }
  else
  {
    return (uint8_t)W25Q_ERR_ID;        /* 不认识的器件，拒绝使用 */
  }

  /* 4) W25Q256：进入 4 字节地址模式（0xB7） */
  if (s_4byte != 0U)
  {
    w25q_cs_low();
    st = w25q_tx(&cmd_en4b, 1U);
    w25q_cs_high();
    w25q_delay(W25Q_TSHSL_LOOPS);
    if (st != W25Q_OK)
    {
      s_4byte    = 0U;
      s_capacity = 0U;
      return (uint8_t)st;
    }

    /* 补充说明：0xB7 是易失的 EN4B；W25Q256JV 还可以把状态寄存器 3 的 ADS
     * 位置 1（0x06 + 0xC5）做成非易失设置，本驱动刻意不用它：每次上电由本
     * 函数重新进入 4 字节模式即可，避免把非易失位写坏后无法恢复。
     * 3 字节地址的老器件（W25Q128/W25Q64）不支持 0xB7，绝不发这条命令。 */
  }

  s_inited = 1U;
  return 0U;
}

uint32_t W25Q_ReadID(void)
{
  uint32_t id = 0U;

  if (s_hw_ready == 0U)
  {
    w25q_hw_init();
    s_hw_ready = 1U;
  }

  /* 直接读，不依赖 s_inited：ID 不受支持时也要把真实 ID 交给上层判别 */
  if (w25q_read_jedec_id(&id) != W25Q_OK)
  {
    return 0xFFFFFFFFU;
  }
  return id;
}

uint32_t W25Q_CapacityBytes(void)
{
  if (s_capacity == 0U)
  {
    (void)w25q_ensure_init();
  }
  return s_capacity;
}

W25Q_Status W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
  uint8_t header[5];
  uint8_t hlen;
  uint16_t chunk;
  W25Q_Status st;

  if ((buf == NULL) || (len == 0U))
  {
    return W25Q_ERR_PARAM;
  }
  st = w25q_ensure_init();
  if (st != W25Q_OK)
  {
    return st;
  }
  if ((addr >= s_capacity) || (len > (s_capacity - addr)))
  {
    return W25Q_ERR_ADDRESS;
  }

  /* 一条读命令 + CS 全程拉低 = 一次"长读"，芯片内部地址自动递增；
   * 只是 HAL 的 Size 是 uint16_t，超过 65535 字节要分几次收，中间 CS 不抬起。 */
  hlen = w25q_build_header(W25Q_CMD_READ_DATA4, W25Q_CMD_READ_DATA3, addr, header);
  w25q_cs_low();
  st = w25q_tx(header, hlen);
  while ((st == W25Q_OK) && (len != 0U))
  {
    chunk = (len > 0xFFFFU) ? 0xFFFFU : (uint16_t)len;
    st = w25q_rx(buf, chunk);
    buf += chunk;
    len -= (uint32_t)chunk;
  }
  w25q_cs_high();
  w25q_delay(W25Q_TSHSL_LOOPS);

  return st;
}

W25Q_Status W25Q_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
  uint8_t header[5];
  uint8_t hlen;
  uint32_t page_left;
  uint16_t n;
  W25Q_Status st;

  if ((buf == NULL) || (len == 0U))
  {
    return W25Q_ERR_PARAM;
  }
  st = w25q_ensure_init();
  if (st != W25Q_OK)
  {
    return st;
  }
  if ((addr >= s_capacity) || (len > (s_capacity - addr)))
  {
    return W25Q_ERR_ADDRESS;
  }

  while (len != 0U)
  {
    /* 跨页自动拆分：每条 Page Program 绝不越过 256 字节页边界 */
    page_left = W25Q_PAGE_SIZE - (addr % W25Q_PAGE_SIZE);
    n = (len > page_left) ? (uint16_t)page_left : (uint16_t)len;

    st = w25q_write_enable();
    if (st != W25Q_OK)
    {
      return st;
    }

    hlen = w25q_build_header(W25Q_CMD_PAGE_PROGRAM4, W25Q_CMD_PAGE_PROGRAM3, addr, header);
    w25q_cs_low();
    st = w25q_tx(header, hlen);
    if (st == W25Q_OK)
    {
      st = w25q_tx(buf, n);
    }
    w25q_cs_high();
    w25q_delay(W25Q_TSHSL_LOOPS);
    if (st != W25Q_OK)
    {
      return st;
    }

    st = w25q_wait_ready(W25Q_TMO_PROGRAM);
    if (st != W25Q_OK)
    {
      return st;
    }

#if (W25Q_WRITE_VERIFY != 0)
    st = w25q_verify(addr, buf, (uint32_t)n);
    if (st != W25Q_OK)
    {
      return st;
    }
#endif

    addr += (uint32_t)n;
    buf  += n;
    len  -= (uint32_t)n;
  }

  return W25Q_OK;
}

W25Q_Status W25Q_EraseSector(uint32_t addr)
{
  W25Q_Status st = w25q_ensure_init();

  if (st != W25Q_OK)
  {
    return st;
  }
  /* addr 低 12 位被器件忽略：擦除的是 addr 所在的 4KB 扇区 */
  return w25q_erase_cmd(W25Q_CMD_SECTOR_ERASE4, W25Q_CMD_SECTOR_ERASE3, addr, W25Q_TMO_ERASE4K);
}

W25Q_Status W25Q_EraseBlock64K(uint32_t addr)
{
  W25Q_Status st = w25q_ensure_init();

  if (st != W25Q_OK)
  {
    return st;
  }
  /* addr 低 16 位被器件忽略：擦除的是 addr 所在的 64KB 块 */
  return w25q_erase_cmd(W25Q_CMD_BLOCK_ERASE64K4, W25Q_CMD_BLOCK_ERASE64K3, addr, W25Q_TMO_ERASE64K);
}

W25Q_Status W25Q_CheckBlank(uint32_t addr, uint32_t len)
{
  uint8_t tmp[W25Q_PAGE_SIZE];
  uint32_t i;
  uint32_t chunk;
  W25Q_Status st;

  if (len == 0U)
  {
    return W25Q_ERR_PARAM;
  }
  st = w25q_ensure_init();
  if (st != W25Q_OK)
  {
    return st;
  }
  if ((addr >= s_capacity) || (len > (s_capacity - addr)))
  {
    return W25Q_ERR_ADDRESS;
  }

  while (len != 0U)
  {
    chunk = (len > W25Q_PAGE_SIZE) ? W25Q_PAGE_SIZE : len;
    st = W25Q_Read(addr, tmp, chunk);
    if (st != W25Q_OK)
    {
      return st;
    }
    for (i = 0U; i < chunk; i++)
    {
      if (tmp[i] != 0xFFU)
      {
        return W25Q_ERR_WRITE;   /* 存在非 0xFF 数据：按"校验失败"上报 */
      }
    }
    addr += chunk;
    len  -= chunk;
  }

  return W25Q_OK;
}

uint8_t W25Q_IsReady(void)
{
  return s_inited;
}

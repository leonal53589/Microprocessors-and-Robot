/**
  ******************************************************************************
  * @file    w25qxx.h
  * @brief   W25Q 系列 SPI NOR Flash 驱动（板载 W25Q256，SPI5）—— 人脸图库持久化
  *
  *  一、硬件连线（依据《阿波罗V2 STM32F429开发板IO引脚分配表》，引脚定义见
  *      pinmap.h 第 5 节；这四根线板上已连好 W25Q256，不需要任何外部飞线）：
  *          SCK  = PF7   AF5 = SPI5_SCK
  *          MISO = PF8   AF5 = SPI5_MISO
  *          MOSI = PF9   AF5 = SPI5_MOSI
  *          CS   = PF6   普通推挽输出（F_CS），软件控制，空闲保持高电平
  *      SPI5 挂在 APB2（本工程 APB2 = 90MHz），主机模式：
  *          模式 0（CPOL = Low / CPHA = 1Edge）、8 位、MSB First、软件 NSS、
  *          SCK = 90MHz / 4 = 22.5MHz；只用阻塞式 HAL_SPI_Transmit/Receive，
  *          不使用 SPI 中断与 DMA。
  *
  *  二、★ 为什么必须用 4 字节地址模式：
  *      W25Q256 容量 32MB = 2^25 字节，需要 25 位地址；标准 SPI 命令
  *      （0x03 读 / 0x02 页编程 / 0x20 扇区擦除 / 0xD8 块擦除）只带 24 位
  *      地址，最多寻址 16MB，超出部分会回卷到低 16MB，写高 16MB 会把低
  *      16MB 的数据写坏。因此 W25Q_Init() 里必须发 0xB7（Enter 4-Byte
  *      Address Mode），之后只用 4 字节地址命令：
  *          0x13 Read Data with 4-Byte Address     （本驱动选用；0x0C Fast
  *                                                  Read with 4-Byte Address
  *                                                  也可，但要多 1 个 dummy
  *                                                  字节，22.5MHz 下没必要）
  *          0x12 Page Program with 4-Byte Address  （页大小 256 字节）
  *          0x21 Sector Erase 4KB with 4-Byte Address
  *          0xDC Block Erase 64KB with 4-Byte Address
  *          0x9F JEDEC ID / 0x05 Read Status-1 / 0x06 Write Enable
  *      0xB7 是【易失】设置，掉电或复位后失效，所以每次上电都必须重新调用
  *      W25Q_Init()（不能只在 main 里调一次就完事，休眠唤醒后同样要重调）。
  *
  *  三、兼容性（3 字节器件回退）：
  *      本板 W25Q256 原生支持 4 字节地址模式，不需要"3 字节命令 + 扩展地址
  *      寄存器（0xC5 写 / 0xC8 读 ExtAdd）"那套老器件回退方案。为了换片子
  *      （例如插上 W25Q128/W25Q64 的兼容板）也能跑，W25Q_Init() 内置器件表：
  *      识别到 W25Q128（0xEF4018，16MB）或 W25Q64（0xEF4017，8MB）时驱动
  *      停在 3 字节地址模式，改用 0x03/0x02/0x20/0xD8，绝不发 0xB7。
  *      其余 ID 一律返回 W25Q_ERR_ID。
  *
  *  四、分区约定（人脸图库）：
  *      pinmap.h 中 FACE_FLASH_BASE = 0x01000000（16MB 偏移），人脸图库从
  *      该偏移开始存放（1MB 预留区 FACE_FLASH_RESERVED），因此 Flash 前
  *      16MB 由字库/其它资源占用，图库读写不得越过 FACE_FLASH_BASE 向下写。
  *      驱动本身暴露整片容量，不做分区裁剪，越界保护由上层（face_db 模块）
  *      按 FACE_FLASH_BASE / FACE_FLASH_RESERVED 负责。
  ******************************************************************************
  */

#ifndef __W25QXX_H
#define __W25QXX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ==========================================================================
 * 介质几何参数（W25Q256：4KB 扇区 / 64KB 块 / 256B 页）
 * ========================================================================== */
#define W25Q_PAGE_SIZE          256U
#define W25Q_SECTOR_SIZE        4096U
#define W25Q_BLOCK64K_SIZE      65536U

/* ==========================================================================
 * 返回状态码
 * ========================================================================== */
typedef enum {
  W25Q_OK = 0,
  W25Q_ERR_PARAM,
  W25Q_ERR_ID,          /* JEDEC ID not a supported W25Qxx            */
  W25Q_ERR_TIMEOUT,     /* busy wait exceeded                         */
  W25Q_ERR_WRITE,       /* write/erase verify failed                  */
  W25Q_ERR_ADDRESS      /* out of the detected capacity               */
} W25Q_Status;

/* ==========================================================================
 * 公共 API
 * ========================================================================== */

/**
  * @brief  初始化 SPI5 + 引脚，识别器件，必要时进入 4 字节地址模式。
  * @note   幂等：可重复调用（每次都会重新核对 ID 并重建 4 字节模式）。
  * @retval 0 = 成功；非 0 时返回对应的 W25Q_Status 值（如 W25Q_ERR_ID = 2）
  */
uint8_t     W25Q_Init(void);

/**
  * @brief  读 24 位 JEDEC ID（0x9F），如 W25Q256 = 0xEF4019。
  * @retval 24 位 ID；总线/SPI 收发失败时返回 0xFFFFFFFF
  */
uint32_t    W25Q_ReadID(void);

/**
  * @brief  返回 W25Q_Init() 检测到的容量（字节），未初始化返回 0。
  * @note   W25Q256 -> 32*1024*1024；W25Q128 -> 16*1024*1024；W25Q64 -> 8*1024*1024
  */
uint32_t    W25Q_CapacityBytes(void);

/**
  * @brief  从 addr 连续读 len 字节（一次读命令，CS 全程拉低，地址自增）。
  * @retval W25Q_OK / W25Q_ERR_PARAM（buf 为空或 len 为 0）/
  *         W25Q_ERR_ADDRESS（越界）/ W25Q_ERR_ID / W25Q_ERR_TIMEOUT / W25Q_ERR_WRITE
  */
W25Q_Status W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
  * @brief  从 addr 写 len 字节；跨 256 字节页边界自动拆分，每页一条页编程命令。
  * @note   写前必须已擦除（NOR 只能 1->0）；默认开启回读校验（见 w25qxx.c）。
  * @retval W25Q_OK / W25Q_ERR_PARAM / W25Q_ERR_ADDRESS / W25Q_ERR_WRITE /
  *         W25Q_ERR_TIMEOUT / W25Q_ERR_ID
  */
W25Q_Status W25Q_Write(uint32_t addr, const uint8_t *buf, uint32_t len);

/**
  * @brief  擦除 addr 所在的 4KB 扇区（0x21 / 0x20），addr 低 12 位被器件忽略。
  */
W25Q_Status W25Q_EraseSector(uint32_t addr);

/**
  * @brief  擦除 addr 所在的 64KB 块（0xDC / 0xD8），addr 低 16 位被器件忽略。
  */
W25Q_Status W25Q_EraseBlock64K(uint32_t addr);

/**
  * @brief  检查 [addr, addr+len) 是否全为 0xFF（空白）。
  * @retval W25Q_OK = 全 0xFF；W25Q_ERR_WRITE = 存在非 0xFF 数据；
  *         其它同 W25Q_Read()。len 为 0 返回 W25Q_ERR_PARAM。
  */
W25Q_Status W25Q_CheckBlank(uint32_t addr, uint32_t len);

/**
  * @brief  W25Q_Init() 是否成功过（1 = 就绪可用，0 = 未就绪）。
  */
uint8_t     W25Q_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __W25QXX_H */

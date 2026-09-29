/**
  ******************************************************************************
  * @file    face_db.h
  * @brief   人脸图库：SDRAM 运行时镜像 + 板载 SPI NOR Flash 持久化
  *
  *  存储设计（两条线，互相印证）：
  *      SDRAM 镜像：30 条记录（5 人 x 6 槽），直接用结构体数组，
  *                  算法和界面都从这里取数据，读写快。
  *      Flash 备份：每条记录占 2 个 4KB 扇区（8192B），起始于
  *                  pinmap.h 的 FACE_FLASH_BASE = 0x01000000（16MiB 偏移），
  *                  刻意避开板载 Flash 前半部分可能存在的字库等资源，
  *                  满足"不得覆盖已有程序或资源数据"的要求。
  *
  *  每条记录的 Flash 布局（小端序，显式按字节打包，不依赖结构体对齐）：
  *      偏移 0   4B  magic 'FREC'
  *      偏移 4   1B  version
  *      偏移 5   1B  人员编号 1..5
  *      偏移 6   1B  该人第几张 0..5
  *      偏移 7   1B  flags
  *      偏移 8   4B  写入时刻（HAL_GetTick，仅供调试）
  *      偏移 12  4608B  RGB565 人脸图
  *      偏移 4620 2304B 灰度图
  *      偏移 6924 4B  前 6924 字节的 CRC32
  *  读回时逐条校验 magic + CRC32，任何一条坏了就只丢那一条，不影响其它人。
  ******************************************************************************
  */

#ifndef __FACE_DB_H
#define __FACE_DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "image_proc.h"
#include "config.h"

#define FACE_MAX_RECORDS        (CFG_MAX_RECORDS)      /* 30 */
#define FACE_FLASH_SLOT_BYTES   8192U                  /* 每槽 2 个 4KB 扇区 */
#define FACE_FLASH_HDR_OFFSET   0U                     /* 库头在第 1 个扇区 */
#define FACE_FLASH_DATA_OFFSET  4096U                  /* 记录从第 2 个扇区开始 */
#define FACE_FLASH_MAGIC_REC    0x43455246U            /* 'F','R','E','C' */
#define FACE_FLASH_MAGIC_HDR    0x31424446U            /* 'F','D','B','1' */
#define FACE_FLASH_VERSION      1U

/* 一条图库记录（放 SDRAM，约 6.9KB） */
typedef struct
{
  uint8_t   used;             /* 1 = 有效         */
  uint8_t   person;           /* 1..CFG_PERSON_COUNT */
  uint8_t   slot;             /* 该人第几张 0..   */
  uint8_t   from_flash;       /* 1 = 本次开机从 Flash 读回 */
  uint32_t  timestamp;        /* 写入时刻         */
  uint32_t  crc;              /* 读回时校验用的 CRC32 */
  FaceImage img;
} FaceRecord;

/* 图库对象（本体很小，放内部 RAM；记录数组指向 SDRAM） */
typedef struct
{
  uint8_t   flash_ok;                             /* NOR Flash 是否可用      */
  uint8_t   flash_id_ok;                          /* 器件 ID 是否受支持      */
  uint32_t  flash_id;                             /* JEDEC ID                */
  uint32_t  flash_capacity;                       /* 字节数                  */
  uint8_t   count;                                /* 有效记录总数            */
  uint8_t   person_count[CFG_PERSON_COUNT];        /* 每人有效张数            */
  FaceRecord *rec;                                /* 指向 SDRAM 记录数组     */
} FaceDb;

/* 初始化：开 Flash、核对器件 ID、读回并逐条校验图库。返回 0 表示 Flash 可用 */
uint8_t FaceDb_Init(FaceDb *db);

/* 给某人加一张图（写 Flash 并回读校验）。返回 0 成功 */
uint8_t FaceDb_Add(FaceDb *db, uint8_t person, const FaceImage *img);

/* 删掉某人最后一张（用于"拍糊了重拍"）。返回 0 成功 */
uint8_t FaceDb_DeleteLast(FaceDb *db, uint8_t person);

/* 清空整个图库（擦除用到的 Flash 扇区） */
uint8_t FaceDb_EraseAll(FaceDb *db);

uint8_t FaceDb_CountOf(const FaceDb *db, uint8_t person);
uint8_t FaceDb_Total(const FaceDb *db);

/* 每人是否都达到 CFG_MIN_SAMPLES 张（训练样本是否够用） */
uint8_t FaceDb_IsComplete(const FaceDb *db);

/* 单人 CRC32（写 Flash 前算、读回后再算，用于完整性验证） */
uint32_t FaceDb_RecordCrc(const FaceImage *img, uint8_t person, uint8_t slot, uint32_t ts);

#ifdef __cplusplus
}
#endif

#endif /* __FACE_DB_H */

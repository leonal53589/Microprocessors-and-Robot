/**
  ******************************************************************************
  * @file    face_db.c
  * @brief   人脸图库的 Flash 持久化与校验
  *
  *  关键设计：
  *    1) 不申请大缓冲：记录 6928B 而链接脚本的栈只有几 KB，所以
  *       写/读都按"头部 -> RGB565 -> 灰度 -> CRC"分四段流式操作，
  *       CRC32 也是流式累加的（边写边算、边读边算）。
  *    2) 写入后立刻回读校验：擦除-写入-回读-CRC 比对全过了才认为成功，
  *       失败就把该槽标为无效，绝不让"写坏的图"进入图库。
  *    3) 读回时逐条独立校验：一条坏了只丢一条。
  ******************************************************************************
  */

#include "face_db.h"
#include "pinmap.h"
#include "w25qxx.h"

/* 编译期检查：SDRAM 里给图库划的区域必须装得下全部记录。
 * 这类"越界/重叠"的错误在真机上表现为莫名其妙的图库错乱，必须在编译期挡住。 */
typedef char face_db_area_check[(sizeof(FaceRecord) * FACE_MAX_RECORDS <= FACE_DB_RESERVED) ? 1 : -1];

/* ==========================================================================
 * 小工具：显式按字节打包，避免结构体对齐差异
 * ========================================================================== */
static void put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFFU);
  p[1] = (uint8_t)((v >> 8) & 0xFFU);
  p[2] = (uint8_t)((v >> 16) & 0xFFU);
  p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t get_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 流式 CRC32（多项式 0xEDB88320，逐位实现，不用查表省 Flash） */
static uint32_t crc32_step(uint32_t crc, const uint8_t *buf, uint32_t len)
{
  for (uint32_t i = 0U; i < len; i++)
  {
    crc ^= (uint32_t)buf[i];
    for (uint8_t b = 0U; b < 8U; b++)
    {
      crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(crc & 1U)));
    }
  }
  return crc;
}

/* 一条记录的头部固定 12 字节 */
static void pack_header(uint8_t *h, uint8_t person, uint8_t slot, uint32_t ts)
{
  put_u32(h, FACE_FLASH_MAGIC_REC);
  h[4] = FACE_FLASH_VERSION;
  h[5] = person;
  h[6] = slot;
  h[7] = FACE_FLASH_FLAG_RGB565;
  put_u32(&h[8], ts);
}

uint32_t FaceDb_RecordCrc(const FaceImage *img, uint8_t person, uint8_t slot, uint32_t ts)
{
  uint8_t  hdr[12];
  uint32_t crc = 0xFFFFFFFFU;

  pack_header(hdr, person, slot, ts);
  crc = crc32_step(crc, hdr, sizeof(hdr));
  crc = crc32_step(crc, (const uint8_t *)img->rgb565, sizeof(img->rgb565));
  crc = crc32_step(crc, img->gray, sizeof(img->gray));

  return ~crc;
}

static uint32_t slot_flash_addr(uint8_t slot)
{
  return FACE_FLASH_BASE + FACE_FLASH_DATA_OFFSET + ((uint32_t)slot * FACE_FLASH_SLOT_BYTES);
}

/* ==========================================================================
 * 库头（第 1 个扇区，16 字节有效内容）
 * ========================================================================== */
static uint8_t hdr_write(const FaceDb *db)
{
  uint8_t  buf[16];
  uint32_t crc;

  put_u32(&buf[0], FACE_FLASH_MAGIC_HDR);
  buf[4] = FACE_FLASH_VERSION;
  buf[5] = db->count;
  for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
  {
    buf[6U + i] = db->person_count[i];
  }
  buf[11] = (uint8_t)db->flash_ok;
  crc = ~crc32_step(0xFFFFFFFFU, buf, 12U);
  put_u32(&buf[12], crc);

  if (W25Q_EraseSector(FACE_FLASH_BASE) != W25Q_OK)
  {
    return 1U;
  }
  if (W25Q_Write(FACE_FLASH_BASE, buf, sizeof(buf)) != W25Q_OK)
  {
    return 2U;
  }
  return 0U;
}

static uint8_t hdr_read(FaceDb *db)
{
  uint8_t  buf[16];
  uint32_t crc;

  if (W25Q_Read(FACE_FLASH_BASE, buf, sizeof(buf)) != W25Q_OK)
  {
    return 1U;
  }
  if (get_u32(&buf[0]) != FACE_FLASH_MAGIC_HDR)
  {
    return 2U;                       /* 空白或不是本工程的库 */
  }
  crc = ~crc32_step(0xFFFFFFFFU, buf, 12U);
  if (crc != get_u32(&buf[12]))
  {
    return 3U;                       /* 库头坏了 */
  }
  if (buf[4] != FACE_FLASH_VERSION)
  {
    return 4U;
  }

  db->count = buf[5];
  if (db->count > FACE_MAX_RECORDS)
  {
    db->count = 0U;
    return 5U;
  }
  for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
  {
    db->person_count[i] = buf[6U + i];
  }
  return 0U;
}

/* ==========================================================================
 * 单条记录的写 / 读（都是分段流式，不用大缓冲）
 * ========================================================================== */
static uint8_t rec_write(uint8_t slot, uint8_t person, uint8_t person_slot,
                         const FaceImage *img, uint32_t ts)
{
  uint8_t  hdr[12];
  uint32_t addr = slot_flash_addr(slot);
  uint32_t crc;
  uint8_t  rd[64];

  pack_header(hdr, person, person_slot, ts);
  crc = crc32_step(0xFFFFFFFFU, hdr, sizeof(hdr));
  crc = crc32_step(crc, (const uint8_t *)img->rgb565, sizeof(img->rgb565));
  crc = crc32_step(crc, img->gray, sizeof(img->gray));

  /* 一个槽占 2 个扇区，必须两个都擦 */
  if (W25Q_EraseSector(addr) != W25Q_OK)
  {
    return 1U;
  }
  if (W25Q_EraseSector(addr + 4096U) != W25Q_OK)
  {
    return 2U;
  }

  if (W25Q_Write(addr, hdr, sizeof(hdr)) != W25Q_OK)
  {
    return 3U;
  }
  if (W25Q_Write(addr + 12U, (const uint8_t *)img->rgb565, sizeof(img->rgb565)) != W25Q_OK)
  {
    return 4U;
  }
  if (W25Q_Write(addr + 12U + sizeof(img->rgb565), img->gray, sizeof(img->gray)) != W25Q_OK)
  {
    return 5U;
  }
  {
    uint8_t c[4];
    put_u32(c, ~crc);
    if (W25Q_Write(addr + 12U + sizeof(img->rgb565) + sizeof(img->gray), c, 4U) != W25Q_OK)
    {
      return 6U;
    }
  }

  /* 分块回读整条记录，验证 RGB/灰度/头部的 CRC，避免只核对 magic 就报成功。
   * 只用 64B 栈空间，不分配第二张人脸图。 */
  {
    uint32_t stored_crc = ~crc;
    uint32_t verify_crc = 0xFFFFFFFFU;
    uint32_t payload_bytes = 12U + sizeof(img->rgb565) + sizeof(img->gray);
    for (uint32_t offset = 0U; offset < payload_bytes; offset += sizeof(rd))
    {
      uint32_t n = payload_bytes - offset;
      if (n > sizeof(rd)) { n = sizeof(rd); }
      if (W25Q_Read(addr + offset, rd, n) != W25Q_OK) { return 7U; }
      verify_crc = crc32_step(verify_crc, rd, n);
    }
    if (W25Q_Read(addr + payload_bytes, rd, 4U) != W25Q_OK) { return 7U; }
    if ((~verify_crc != stored_crc) || (get_u32(rd) != stored_crc)) { return 8U; }
  }
  return 0U;
}

/* 读回一条并校验 CRC；成功则填充 rec */
static uint8_t rec_read(uint8_t slot, FaceRecord *rec)
{
  uint8_t  hdr[12];
  uint8_t  crc_buf[4];
  uint32_t addr = slot_flash_addr(slot);
  uint32_t crc;

  if (W25Q_Read(addr, hdr, sizeof(hdr)) != W25Q_OK)
  {
    return 1U;
  }
  if (get_u32(&hdr[0]) != FACE_FLASH_MAGIC_REC)
  {
    return 2U;                        /* 空槽 */
  }
  if ((hdr[4] != FACE_FLASH_VERSION) || (hdr[5] < 1U) || (hdr[5] > CFG_PERSON_COUNT) ||
      (hdr[6] >= CFG_SLOTS_PER_PERSON) || ((hdr[7] & ~FACE_FLASH_FLAG_RGB565) != 0U))
  {
    return 3U;
  }

  if (W25Q_Read(addr + 12U, (uint8_t *)rec->img.rgb565, sizeof(rec->img.rgb565)) != W25Q_OK)
  {
    return 4U;
  }
  if (W25Q_Read(addr + 12U + sizeof(rec->img.rgb565), rec->img.gray, sizeof(rec->img.gray)) != W25Q_OK)
  {
    return 5U;
  }
  if (W25Q_Read(addr + 12U + sizeof(rec->img.rgb565) + sizeof(rec->img.gray), crc_buf, 4U) != W25Q_OK)
  {
    return 6U;
  }

  /* 逐字节重算 CRC，和数据比对 */
  crc = crc32_step(0xFFFFFFFFU, hdr, sizeof(hdr));
  if (hdr[7] == 0U) { crc = ~crc; }  /* 兼容旧版在头部后提前取反的 CRC 算法。 */
  crc = crc32_step(crc, (const uint8_t *)rec->img.rgb565, sizeof(rec->img.rgb565));
  crc = crc32_step(crc, rec->img.gray, sizeof(rec->img.gray));
  crc = ~crc;
  if (crc != get_u32(crc_buf))
  {
    return 7U;                        /* CRC 不符：这条记录坏了 */
  }

  rec->used       = 1U;
  rec->person     = hdr[5];
  rec->slot       = hdr[6];
  rec->from_flash = 1U;
  rec->timestamp  = get_u32(&hdr[8]);
  if (hdr[7] == 0U)
  {
    /* 原工程未在 DMA 完成后交换字节，旧图库同时保存了错色 RGB 和错误灰度。
     * 先验证原始记录完整性，再转成当前统一格式；保留 Flash 原数据不自动重写。 */
    Image_RGB565SwapBytes(rec->img.rgb565, FACE_PIXELS);
    Image_ToGray(&rec->img);
  }
  rec->crc = FaceDb_RecordCrc(&rec->img, rec->person, rec->slot, rec->timestamp);
  rec->img.quality = IMG_OK;
  return 0U;
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */
uint8_t FaceDb_Init(FaceDb *db)
{
  uint8_t rc;

  if (db == NULL)
  {
    return 1U;
  }

  db->rec = (FaceRecord *)FACE_DB_ADDR;
  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    db->rec[i].used = 0U;
    db->rec[i].person = 0U;
    db->rec[i].slot = 0U;
    db->rec[i].from_flash = 0U;
  }
  db->count = 0U;
  for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
  {
    db->person_count[i] = 0U;
  }
  db->flash_ok = 0U;
  db->flash_id_ok = 0U;
  db->flash_id = 0U;
  db->flash_capacity = 0U;

  /* ---- 打开 NOR Flash 并核对器件 ID ---- */
  if (W25Q_Init() != 0U)
  {
    return 2U;                        /* Flash 不可用：上层退化为 SDRAM 临时图库 */
  }
  db->flash_id = W25Q_ReadID();
  db->flash_capacity = W25Q_CapacityBytes();

  if ((db->flash_id != 0xEF4019U) && (db->flash_id != 0xEF4018U) && (db->flash_id != 0xEF4017U))
  {
    return 3U;                        /* ID 不受支持，拒绝写入 */
  }
  db->flash_id_ok = 1U;
  if (db->flash_capacity < (FACE_FLASH_BASE + FACE_FLASH_RESERVED))
  {
    return 4U;                        /* 容量不够放人脸库区域 */
  }
  db->flash_ok = 1U;

  /* ---- 读库头；空白或损坏就新建一个 ---- */
  rc = hdr_read(db);
  if (rc != 0U)
  {
    db->count = 0U;
    for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
    {
      db->person_count[i] = 0U;
    }
    (void)hdr_write(db);
    return 0U;                        /* Flash 可用，但图库是空的 */
  }

  /* ---- 按库头逐条读回并校验 ---- */
  db->count = 0U;
  for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
  {
    db->person_count[i] = 0U;
  }
  for (uint8_t s = 0U; s < FACE_MAX_RECORDS; s++)
  {
    if (rec_read(s, &db->rec[s]) == 0U)
    {
      if (db->rec[s].person <= CFG_PERSON_COUNT)
      {
        db->person_count[db->rec[s].person - 1U]++;
        db->count++;
      }
    }
    else
    {
      db->rec[s].used = 0U;
    }
  }

  return 0U;
}

uint8_t FaceDb_CountOf(const FaceDb *db, uint8_t person)
{
  if ((db == NULL) || (person < 1U) || (person > CFG_PERSON_COUNT))
  {
    return 0U;
  }
  return db->person_count[person - 1U];
}

uint8_t FaceDb_Total(const FaceDb *db)
{
  return (db == NULL) ? 0U : db->count;
}

uint8_t FaceDb_IsComplete(const FaceDb *db)
{
  if (db == NULL)
  {
    return 0U;
  }
  for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
  {
    if (db->person_count[i] < CFG_MIN_SAMPLES)
    {
      return 0U;
    }
  }
  return 1U;
}

uint8_t FaceDb_Add(FaceDb *db, uint8_t person, const FaceImage *img)
{
  uint8_t person_slot;
  uint8_t idx;
  uint32_t ts;

  if ((db == NULL) || (img == NULL) || (person < 1U) || (person > CFG_PERSON_COUNT))
  {
    return 1U;
  }
  if (db->person_count[person - 1U] >= CFG_SLOTS_PER_PERSON)
  {
    return 2U;                        /* 这个人槽位满了 */
  }
  if (db->count >= FACE_MAX_RECORDS)
  {
    return 3U;                        /* 图库满了 */
  }

  /* 找一个空闲槽（SDRAM 数组里的位置） */
  for (idx = 0U; idx < FACE_MAX_RECORDS; idx++)
  {
    if (db->rec[idx].used == 0U)
    {
      break;
    }
  }
  if (idx >= FACE_MAX_RECORDS)
  {
    return 4U;
  }

  person_slot = db->person_count[person - 1U];
  ts = HAL_GetTick();

  /* 先写 Flash（要求：Flash 可用时必须落盘；不可用时退化为 SDRAM 临时图库） */
  if (db->flash_ok != 0U)
  {
    if (rec_write(idx, person, person_slot, img, ts) != 0U)
    {
      return 5U;                      /* 写失败：不更新内存，保持原状 */
    }
  }

  db->rec[idx].used = 1U;
  db->rec[idx].person = person;
  db->rec[idx].slot = person_slot;
  db->rec[idx].from_flash = 0U;
  db->rec[idx].timestamp = ts;
  db->rec[idx].crc = FaceDb_RecordCrc(img, person, person_slot, ts);
  db->rec[idx].img = *img;

  db->person_count[person - 1U]++;
  db->count++;

  if (db->flash_ok != 0U)
  {
    (void)hdr_write(db);
  }
  return 0U;
}

uint8_t FaceDb_DeleteLast(FaceDb *db, uint8_t person)
{
  int16_t last = -1;

  if ((db == NULL) || (person < 1U) || (person > CFG_PERSON_COUNT))
  {
    return 1U;
  }
  if (db->person_count[person - 1U] == 0U)
  {
    return 2U;                        /* 本来就没有 */
  }

  /* 找该人最后加进来的一条 */
  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    if ((db->rec[i].used != 0U) && (db->rec[i].person == person))
    {
      if ((last < 0) || (db->rec[i].slot >= db->rec[last].slot))
      {
        last = (int16_t)i;
      }
    }
  }
  if (last < 0)
  {
    return 3U;
  }

  if (db->flash_ok != 0U)
  {
    /* 把该槽的两个扇区擦掉，保证重新上电不会又冒出来 */
    uint32_t addr = slot_flash_addr((uint8_t)last);
    (void)W25Q_EraseSector(addr);
    (void)W25Q_EraseSector(addr + 4096U);
  }

  db->rec[last].used = 0U;
  db->rec[last].person = 0U;
  db->rec[last].slot = 0U;
  db->person_count[person - 1U]--;
  db->count--;

  if (db->flash_ok != 0U)
  {
    (void)hdr_write(db);
  }
  return 0U;
}

uint8_t FaceDb_EraseAll(FaceDb *db)
{
  if (db == NULL)
  {
    return 1U;
  }

  if (db->flash_ok != 0U)
  {
    /* 只擦用到的扇区：库头 + 每条有效记录占的 2 个扇区（整片 1MiB 太慢） */
    (void)W25Q_EraseSector(FACE_FLASH_BASE);
    for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
    {
      if (db->rec[i].used != 0U)
      {
        uint32_t addr = slot_flash_addr(i);
        (void)W25Q_EraseSector(addr);
        (void)W25Q_EraseSector(addr + 4096U);
      }
    }
  }

  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    db->rec[i].used = 0U;
    db->rec[i].person = 0U;
    db->rec[i].slot = 0U;
  }
  db->count = 0U;
  for (uint8_t i = 0U; i < CFG_PERSON_COUNT; i++)
  {
    db->person_count[i] = 0U;
  }

  if (db->flash_ok != 0U)
  {
    (void)hdr_write(db);
  }
  return 0U;
}

/**
  ******************************************************************************
  * @file    algo_hist.c
  * @brief   算法二：手工特征直方图匹配（LBP 分区直方图 + 颜色直方图对照）
  *
  *  LBP 特征（核心方案）：
  *      3x3 邻域 8 位 LBP 编码 + 4x4 分区，每区 256 桶 -> 4096 维，
  *      每个分区单独归一化到 10000，特征存在 SDRAM（见 face_algo.h 的地址）。
  *      距离用卡方： chi2 = sum (a-b)^2 / (a+b+1) / 归一化和
  *
  *  颜色直方图（对照特征）：
  *      RGB 各取高 3 位 -> 512 桶，整体归一化。
  *      它完全没有空间信息：同一个人换表情影响小（看着"稳"），
  *      但背景/衣服颜色一变就跟着变，所以未录入者的误通过率通常明显高于 LBP。
  *      把它和 LBP 放一起对比，正是实验要求里"比较颜色信息与局部纹理信息"。
  *
  *  两个特征各自独立阈值、独立统计，互不影响。
  ******************************************************************************
  */

#include "face_algo.h"

/* 特征表：按"图库记录下标"索引，未使用的记录跳过 */
static uint16_t *const s_lbp_feat   = (uint16_t *)LBP_FEAT_ADDR;
static uint16_t *const s_color_feat = (uint16_t *)COLOR_FEAT_ADDR;

/* -------------------------------------------------------------------------- */
/* 建特征库                                                                    */
/* -------------------------------------------------------------------------- */

void AlgoHist_BuildLbp(const FaceDb *db)
{
  if (db == NULL)
  {
    return;
  }

  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    if (db->rec[i].used == 0U)
    {
      continue;
    }
    Image_LbpHistogram(db->rec[i].img.gray, &s_lbp_feat[(uint32_t)i * LBP_HIST_LEN]);
  }
}

void AlgoHist_BuildColor(const FaceDb *db)
{
  if (db == NULL)
  {
    return;
  }

  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    if (db->rec[i].used == 0U)
    {
      continue;
    }
    Image_ColorHistogram(db->rec[i].img.rgb565, &s_color_feat[(uint32_t)i * COLOR_BINS]);
  }
}

/* -------------------------------------------------------------------------- */
/* 查询特征                                                                    */
/* -------------------------------------------------------------------------- */

void AlgoHist_QueryLbp(const FaceImage *img, uint16_t *out)
{
  if ((img == NULL) || (out == NULL))
  {
    return;
  }
  Image_LbpHistogram(img->gray, out);
}

void AlgoHist_QueryColor(const FaceImage *img, uint16_t *out)
{
  if ((img == NULL) || (out == NULL))
  {
    return;
  }
  Image_ColorHistogram(img->rgb565, out);
}

/* -------------------------------------------------------------------------- */
/* 匹配                                                                        */
/* -------------------------------------------------------------------------- */

float AlgoHist_MatchLbp(const FaceDb *db, const uint16_t *q, uint8_t *person)
{
  float   best = -1.0f;
  uint8_t best_person = 0U;

  if ((db == NULL) || (q == NULL) || (db->count == 0U))
  {
    if (person != NULL)
    {
      *person = 0U;
    }
    return -1.0f;
  }

  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    uint16_t *ref;

    if (db->rec[i].used == 0U)
    {
      continue;
    }
    ref = &s_lbp_feat[(uint32_t)i * LBP_HIST_LEN];

    float d = Image_ChiSquare(q, ref, LBP_HIST_LEN, LBP_CELL_SUM);
    if ((best < 0.0f) || (d < best))
    {
      best = d;
      best_person = db->rec[i].person;
    }
  }

  if (person != NULL)
  {
    *person = best_person;
  }
  return best;
}

float AlgoHist_MatchColor(const FaceDb *db, const uint16_t *q, uint8_t *person)
{
  float   best = -1.0f;
  uint8_t best_person = 0U;

  if ((db == NULL) || (q == NULL) || (db->count == 0U))
  {
    if (person != NULL)
    {
      *person = 0U;
    }
    return -1.0f;
  }

  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    uint16_t *ref;

    if (db->rec[i].used == 0U)
    {
      continue;
    }
    ref = &s_color_feat[(uint32_t)i * COLOR_BINS];

    float d = Image_ChiSquare(q, ref, COLOR_BINS, COLOR_HIST_SUM);
    if ((best < 0.0f) || (d < best))
    {
      best = d;
      best_person = db->rec[i].person;
    }
  }

  if (person != NULL)
  {
    *person = best_person;
  }
  return best;
}

/* -------------------------------------------------------------------------- */
/* 占用                                                                        */
/* -------------------------------------------------------------------------- */

uint32_t AlgoHist_LbpBytes(void)
{
  return (uint32_t)FACE_MAX_RECORDS * LBP_HIST_LEN * 2U;
}

uint32_t AlgoHist_ColorBytes(void)
{
  return (uint32_t)FACE_MAX_RECORDS * COLOR_BINS * 2U;
}

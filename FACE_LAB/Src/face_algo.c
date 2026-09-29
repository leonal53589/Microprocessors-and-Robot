/**
  ******************************************************************************
  * @file    face_algo.c
  * @brief   四种识别方式的统一调度：建模型 / 识别 / 阈值 / 验证集标定 / 统计
  *
  *  内存布局（都在 pinmap.h 划好的 SDRAM 区里，绝不占用片内 SRAM）：
  *      LBP 特征     : FACE_FEAT_ADDR           30 x 4096 x 2B = 240KiB
  *      颜色特征     : FACE_FEAT_ADDR + 240KiB  30 x  512 x 2B =  30KiB
  *      验证集缓存   : FACE_FEAT_ADDR + 276KiB  12 x 6912B      =  81KiB
  *      PCA 模型     : ALGO_WS_ADDR             约 240KiB（均值脸 + 16 张特征脸 + 投影）
  *      PCA 训练样本 : ALGO_WS_ADDR + 256KiB    30 x 2304B      =  68KiB
  *
  *  片内 RAM 只放一个 8KiB 的查询特征缓冲（直方图），不能放栈上：
  *  链接脚本的栈只有几 KiB，8KiB 的局部数组会直接踩爆栈。
  ******************************************************************************
  */

#include <string.h>

#include "face_algo.h"
#include "algo_pca.h"

/* -------------------------------------------------------------------------- */
/* 其它算法文件提供的内部接口                                                  */
/* -------------------------------------------------------------------------- */
float    AlgoMse_Match(const FaceDb *db, const uint8_t *gray, uint8_t *person);
void     AlgoHist_BuildLbp(const FaceDb *db);
void     AlgoHist_BuildColor(const FaceDb *db);
void     AlgoHist_QueryLbp(const FaceImage *img, uint16_t *out);
void     AlgoHist_QueryColor(const FaceImage *img, uint16_t *out);
float    AlgoHist_MatchLbp(const FaceDb *db, const uint16_t *q, uint8_t *person);
float    AlgoHist_MatchColor(const FaceDb *db, const uint16_t *q, uint8_t *person);
uint32_t AlgoHist_LbpBytes(void);
uint32_t AlgoHist_ColorBytes(void);

/* -------------------------------------------------------------------------- */
/* 状态                                                                        */
/* -------------------------------------------------------------------------- */
static FaceDb *s_db;
static float   s_thresh[ALGO_COUNT];
static uint8_t s_ready[ALGO_COUNT];
static uint8_t s_calibrated[ALGO_COUNT];
static uint32_t s_model_bytes[ALGO_COUNT];
static uint32_t s_train_ms;

/* 查询特征缓冲（LBP 用；颜色直方图复用它，因为两者不会同时用） */
static uint16_t s_query_hist[LBP_HIST_LEN];

/* PCA 模型与训练样本（SDRAM） */
static PcaModel *const s_pca   = (PcaModel *)ALGO_WS_ADDR;
static uint8_t  *const s_train = (uint8_t *)(ALGO_WS_ADDR + 0x00040000U);
static uint8_t         s_train_person[PCA_MAX_SAMPLES];

/* 验证集缓存（SDRAM，最多 12 张） */
static FaceImage *const s_val = (FaceImage *)VAL_CACHE_ADDR;
static uint8_t   s_val_truth[VAL_CACHE_MAX];
static uint8_t   s_val_count;

/* 编译期检查：特征区必须装得下，且各块互不重叠
 *   偏移 0                  : LBP 特征    30 x 4096 x 2B = 240KiB
 *   偏移 240KiB             : 颜色特征    30 x  512 x 2B =  30KiB（结束于 270KiB）
 *   偏移 0x45000(276KiB)    : 验证集缓存  12 x 6916B     =  81KiB
 * 注意 0x45000 是从区首算起的绝对偏移，已经包含了前面两个特征区，
 * 所以判据是"取两者较大值"，不能把特征尺寸再加一遍。 */
typedef char algo_feat_area_check[
  (((((uint32_t)FACE_MAX_RECORDS * LBP_HIST_LEN * 2U)
     + ((uint32_t)FACE_MAX_RECORDS * COLOR_BINS * 2U)) <= 0x45000U)
   && ((0x45000U + ((uint32_t)VAL_CACHE_MAX * sizeof(FaceImage))) <= FACE_FEAT_RESERVED)) ? 1 : -1];

typedef char algo_ws_area_check[
  ((sizeof(PcaModel) <= ALGO_WS_RESERVED)
   && ((0x40000U + ((uint32_t)PCA_MAX_SAMPLES * FACE_PIXELS)) <= ALGO_WS_RESERVED)) ? 1 : -1];

typedef char algo_ws_no_overlap_check[(0x40000U >= sizeof(PcaModel)) ? 1 : -1];

/* 统计：s_stats[验证/测试][算法] */
static AlgoStats s_stats[2][ALGO_COUNT];

/* -------------------------------------------------------------------------- */
/* 小工具                                                                      */
/* -------------------------------------------------------------------------- */

/* DWT 周期计数器 -> 微秒（bsp_delay.c 里已使能 CYCCNT） */
static uint32_t us_now(void)
{
  uint32_t per_us = SystemCoreClock / 1000000U;

  if (per_us == 0U)
  {
    per_us = 1U;
  }
  return (uint32_t)(DWT->CYCCNT / per_us);
}

void FaceAlgo_Bind(FaceDb *db)
{
  s_db = db;
}

uint8_t FaceAlgo_SampleCount(void)
{
  return (s_db == NULL) ? 0U : s_db->count;
}

uint8_t FaceAlgo_IsReady(FaceAlgo a)
{
  return (a < ALGO_COUNT) ? s_ready[a] : 0U;
}

float FaceAlgo_Threshold(FaceAlgo a)
{
  return (a < ALGO_COUNT) ? s_thresh[a] : 0.0f;
}

void FaceAlgo_SetThreshold(FaceAlgo a, float v)
{
  if (a < ALGO_COUNT)
  {
    s_thresh[a] = v;
    s_calibrated[a] = 1U;          /* 手工改过也算标定过 */
  }
}

uint8_t FaceAlgo_IsCalibrated(FaceAlgo a)
{
  return (a < ALGO_COUNT) ? s_calibrated[a] : 0U;
}

uint32_t FaceAlgo_Bytes(FaceAlgo a)
{
  return (a < ALGO_COUNT) ? s_model_bytes[a] : 0U;
}

uint32_t FaceAlgo_TrainMs(void)
{
  return s_train_ms;
}

const char *FaceAlgo_Name(FaceAlgo a)
{
  static const char *const names[ALGO_COUNT] = { "MSE", "LBP", "COLOR", "PCA" };
  return (a < ALGO_COUNT) ? names[a] : "?";
}

const char *FaceAlgo_CnName(FaceAlgo a)
{
  static const char *const names[ALGO_COUNT] = { "MSE模板", "LBP纹理", "颜色对照", "PCA子空间" };
  return (a < ALGO_COUNT) ? names[a] : "?";
}

/* -------------------------------------------------------------------------- */
/* 建模型                                                                      */
/* -------------------------------------------------------------------------- */
uint8_t FaceAlgo_Build(void)
{
  uint16_t n = 0U;
  uint8_t  fail = 0U;

  /* 阈值处理：只有"还没标定过"的方式才回到 config.h 的默认值。
     否则学生刚用验证集标定好阈值，再录入一张照片就把阈值冲掉了。
     s_calibrated[] 刻意【不】在这里清零。 */
  for (uint8_t i = 0U; i < ALGO_COUNT; i++)
  {
    if (s_calibrated[i] == 0U)
    {
      switch ((FaceAlgo)i)
      {
        case ALGO_MSE:   s_thresh[i] = CFG_MSE_THRESHOLD;   break;
        case ALGO_LBP:   s_thresh[i] = CFG_LBP_THRESHOLD;   break;
        case ALGO_COLOR: s_thresh[i] = CFG_COLOR_THRESHOLD; break;
        default:         s_thresh[i] = CFG_PCA_THRESHOLD;   break;
      }
    }
    s_ready[i] = 0U;
    s_model_bytes[i] = 0U;
  }
  s_train_ms = 0U;
  Pca_Reset(s_pca);

  if ((s_db == NULL) || (s_db->count == 0U))
  {
    return 0xFFU;                 /* 没有任何样本 */
  }

  /* ---- 1) MSE：模板就是图库里的灰度图，不需要额外内存 ---- */
  s_ready[ALGO_MSE] = 1U;
  s_model_bytes[ALGO_MSE] = 0U;   /* 直接复用图库，不额外占内存 */

  /* ---- 2) LBP 与颜色直方图：逐样本提特征到 SDRAM ---- */
  AlgoHist_BuildLbp(s_db);
  AlgoHist_BuildColor(s_db);
  s_ready[ALGO_LBP] = 1U;
  s_ready[ALGO_COLOR] = 1U;
  s_model_bytes[ALGO_LBP] = AlgoHist_LbpBytes();
  s_model_bytes[ALGO_COLOR] = AlgoHist_ColorBytes();

  /* ---- 3) PCA：把有效样本拼成连续数组再训练 ---- */
  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    if (s_db->rec[i].used == 0U)
    {
      continue;
    }
    if (n >= PCA_MAX_SAMPLES)
    {
      break;
    }
    memcpy(&s_train[(uint32_t)n * FACE_PIXELS], s_db->rec[i].img.gray, FACE_PIXELS);
    s_train_person[n] = s_db->rec[i].person;
    n++;
  }

  if (n >= 2U)
  {
    if (Pca_Train(s_pca, s_train, s_train_person, n, CFG_PCA_K) == 0)
    {
      s_ready[ALGO_PCA] = 1U;
      s_model_bytes[ALGO_PCA] = Pca_ModelBytes(s_pca->k);
      s_train_ms = s_pca->train_ms;
    }
    else
    {
      fail |= (uint8_t)(1U << ALGO_PCA);   /* 数值失败：样本几乎全同色 */
    }
  }
  else
  {
    fail |= (uint8_t)(1U << ALGO_PCA);     /* 样本不足 2 张，无法做子空间 */
  }

  for (uint8_t i = 0U; i < ALGO_COUNT; i++)
  {
    if (s_ready[i] == 0U)
    {
      fail |= (uint8_t)(1U << i);
    }
  }
  return fail;
}

/* -------------------------------------------------------------------------- */
/* 识别                                                                        */
/* -------------------------------------------------------------------------- */
uint8_t FaceAlgo_Recognize(FaceAlgo a, const FaceImage *q, FaceResult *res)
{
  uint32_t t0;
  float    score = -1.0f;
  uint8_t  person = 0U;

  if ((res == NULL) || (q == NULL) || (a >= ALGO_COUNT))
  {
    return 1U;
  }

  res->ok = 0U;
  res->pass = 0U;
  res->person = 0U;
  res->calibrated = s_calibrated[a];
  res->score = -1.0f;
  res->threshold = s_thresh[a];
  res->time_us = 0U;

  if ((s_db == NULL) || (s_db->count == 0U) || (s_ready[a] == 0U))
  {
    return 2U;                    /* 没模型：上层必须提示"样本不足/未训练" */
  }

  t0 = us_now();

  switch (a)
  {
    case ALGO_MSE:
      score = AlgoMse_Match(s_db, q->gray, &person);
      break;

    case ALGO_LBP:
      AlgoHist_QueryLbp(q, s_query_hist);
      score = AlgoHist_MatchLbp(s_db, s_query_hist, &person);
      break;

    case ALGO_COLOR:
      AlgoHist_QueryColor(q, s_query_hist);
      score = AlgoHist_MatchColor(s_db, s_query_hist, &person);
      break;

    case ALGO_PCA:
    {
      float proj[PCA_MAX_K];
      if (Pca_Project(s_pca, q->gray, proj) == 0)
      {
        (void)Pca_Match(s_pca, proj, &person, &score);
      }
      break;
    }

    default:
      break;
  }

  res->ok = 1U;
  res->score = score;
  res->person = person;
  /* 开放集判定：最近邻距离必须小于等于阈值，否则即使认出了人也不能算通过 */
  res->pass = ((score >= 0.0f) && (score <= s_thresh[a])) ? 1U : 0U;
  res->time_us = us_now() - t0;

  return 0U;
}

/* -------------------------------------------------------------------------- */
/* 验证集缓存                                                                  */
/* -------------------------------------------------------------------------- */
uint8_t FaceAlgo_ValAdd(uint8_t truth_person, const FaceImage *img)
{
  if ((img == NULL) || (truth_person > CFG_PERSON_COUNT))
  {
    return 1U;
  }
  if (s_val_count >= VAL_CACHE_MAX)
  {
    return 2U;
  }

  s_val[s_val_count] = *img;            /* 整张拷进 SDRAM 缓存 */
  s_val_truth[s_val_count] = truth_person;
  s_val_count++;
  return 0U;
}

uint8_t FaceAlgo_ValCount(void)
{
  return s_val_count;
}

void FaceAlgo_ValClear(void)
{
  s_val_count = 0U;
  memset(s_val_truth, 0, sizeof(s_val_truth));
}

uint8_t FaceAlgo_ValTruth(uint8_t idx)
{
  return (idx < s_val_count) ? s_val_truth[idx] : 0U;
}

const FaceImage *FaceAlgo_ValImage(uint8_t idx)
{
  return (idx < s_val_count) ? &s_val[idx] : NULL;
}

/* -------------------------------------------------------------------------- */
/* 阈值标定：用验证集样本找"库内距离"和"库外距离"的分界点                     */
/* -------------------------------------------------------------------------- */
#define CAL_MAX_CAND    (VAL_CACHE_MAX * 2U + 2U)

uint8_t FaceAlgo_Calibrate(FaceAlgo a, float *out_threshold)
{
  float    d_en[VAL_CACHE_MAX];         /* 库内（最近邻认对了人）的距离 */
  float    d_st[VAL_CACHE_MAX];         /* 库外（未录入者）的距离       */
  uint8_t  n_en = 0U;
  uint8_t  n_st = 0U;
  float    best_t = 0.0f;
  int16_t  best_err = 0x7FFF;
  float    best_margin = -1.0f;

  if ((a >= ALGO_COUNT) || (s_db == NULL) || (s_ready[a] == 0U))
  {
    return 1U;
  }

  /* ---- 1) 把验证集缓存全部跑一遍，收集两类距离 ---- */
  for (uint8_t i = 0U; i < s_val_count; i++)
  {
    FaceResult r;

    if (FaceAlgo_Recognize(a, &s_val[i], &r) != 0U)
    {
      return 2U;
    }
    if (s_val_truth[i] == 0U)
    {
      d_st[n_st++] = r.score;           /* 未录入者：距离本身就是"库外距离" */
    }
    else if (r.person == s_val_truth[i])
    {
      d_en[n_en++] = r.score;           /* 认对了人，这个距离才用于定阈值   */
    }
    else
    {
      /* 认错人的样本与阈值选择无关（那是特征分辨力问题，不是阈值问题） */
    }
  }

  if ((n_en == 0U) || (n_st == 0U))
  {
    return 3U;                          /* 两类样本都要有才能标定 */
  }

  /* ---- 2) 候选阈值：所有"库内距离"和"库外距离"之间的中点 ---- */
  {
    float cand[CAL_MAX_CAND];
    uint8_t nc = 0U;

    for (uint8_t i = 0U; i < n_en && nc < CAL_MAX_CAND; i++)
    {
      for (uint8_t j = 0U; j < n_st && nc < CAL_MAX_CAND; j++)
      {
        cand[nc++] = (d_en[i] + d_st[j]) * 0.5f;
      }
    }
    if (nc == 0U)
    {
      return 4U;
    }

    /* ---- 3) 扫一遍，取"判错次数最少"的阈值；并列时取两类间隔最大的 ---- */
    for (uint8_t c = 0U; c < nc; c++)
    {
      float   t = cand[c];
      int16_t err = 0;
      float   max_en = -1.0f;
      float   min_st = 1.0e30f;

      for (uint8_t i = 0U; i < n_en; i++)
      {
        if (d_en[i] > t)
        {
          err++;                        /* 库内被误拒 */
        }
        else if (d_en[i] > max_en)
        {
          max_en = d_en[i];
        }
      }
      for (uint8_t j = 0U; j < n_st; j++)
      {
        if (d_st[j] <= t)
        {
          err++;                        /* 库外被误通过 */
        }
        else if (d_st[j] < min_st)
        {
          min_st = d_st[j];
        }
      }

      float margin = ((max_en >= 0.0f) && (min_st < 1.0e29f)) ? (min_st - max_en) : -1.0f;

      if ((err < best_err) || ((err == best_err) && (margin > best_margin)))
      {
        best_err = err;
        best_margin = margin;
        best_t = t;
      }
    }
  }

  s_thresh[a] = best_t;
  s_calibrated[a] = 1U;
  if (out_threshold != NULL)
  {
    *out_threshold = best_t;
  }
  return 0U;
}

/* -------------------------------------------------------------------------- */
/* 统计                                                                        */
/* -------------------------------------------------------------------------- */
void FaceAlgo_StatsReset(uint8_t mode)
{
  uint8_t m = (uint8_t)(mode & 1U);

  memset(&s_stats[m][0], 0, sizeof(s_stats[m]));
}

void FaceAlgo_StatsResetAll(void)
{
  memset(&s_stats[0][0], 0, sizeof(s_stats));
}

void FaceAlgo_StatsUpdate(uint8_t mode, FaceAlgo a, uint8_t truth_person,
                          const FaceResult *r)
{
  AlgoStats *st;

  if ((a >= ALGO_COUNT) || (r == NULL))
  {
    return;
  }
  st = &s_stats[mode & 1U][a];

  if ((truth_person >= 1U) && (truth_person <= CFG_PERSON_COUNT))
  {
    st->enrolled_total++;
    if ((r->pass != 0U) && (r->person == truth_person))
    {
      st->enrolled_pass++;                  /* 正确通过 */
    }
    else if (r->pass == 0U)
    {
      st->enrolled_reject++;                /* 误拒     */
    }
    else
    {
      st->enrolled_wrong++;                 /* 通过但认成别人 */
    }
  }
  else
  {
    st->stranger_total++;
    if (r->pass != 0U)
    {
      st->stranger_pass++;                  /* 误通过   */
    }
  }

  st->time_us_sum += r->time_us;
  if (st->time_count < 0xFFFFU)
  {
    st->time_count++;
  }
}

const AlgoStats *FaceAlgo_StatsGet(uint8_t mode, FaceAlgo a)
{
  if (a >= ALGO_COUNT)
  {
    return NULL;
  }
  return &s_stats[mode & 1U][a];
}

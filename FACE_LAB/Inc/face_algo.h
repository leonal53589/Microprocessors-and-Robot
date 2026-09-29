/**
  ******************************************************************************
  * @file    face_algo.h
  * @brief   三种识别算法的统一接口 + 阈值 + 验证集标定 + 统计
  *
  *  三种算法（外加一个对照特征）：
  *      ALGO_MSE    : 灰度模板逐像素均方误差，最近邻 + 阈值（开放集）
  *      ALGO_LBP    : 3x3 LBP + 4x4 分区直方图，卡方距离（手工特征，核心方案）
  *      ALGO_COLOR  : 8x8x8 颜色直方图，卡方距离（对照特征，只用颜色无空间信息）
  *      ALGO_PCA    : SVD/PCA 特征脸子空间，低维欧氏距离
  *
  *  统一约定：
  *      1) 距离都是"越小越像"，最近邻距离 <= 阈值 才判"通过"（开放集）。
  *      2) 阈值必须用【验证样本】标定（FaceAlgo_Calibrate），
  *         最终测试样本只能用一次。
  *      3) 每个算法的统计按 验证集/测试集 分开计数。
  ******************************************************************************
  */

#ifndef __FACE_ALGO_H
#define __FACE_ALGO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "image_proc.h"
#include "face_db.h"
#include "pinmap.h"          /* SDRAM 各区起始地址（FACE_FEAT_ADDR / ALGO_WS_ADDR） */

typedef enum
{
  ALGO_MSE = 0,
  ALGO_LBP,
  ALGO_COLOR,
  ALGO_PCA,
  ALGO_COUNT
} FaceAlgo;

/* 一次识别结果 */
typedef struct
{
  uint8_t  ok;            /* 1 = 该算法可用（模型/特征就绪） */
  uint8_t  pass;          /* 1 = 开放集判定"通过"            */
  uint8_t  person;        /* 最近邻属于哪个人员 1..5，0=无    */
  uint8_t  calibrated;    /* 1 = 阈值已用验证集标定           */
  float    score;         /* 最近邻距离                       */
  float    threshold;     /* 本次使用的阈值                   */
  uint32_t time_us;       /* 识别耗时（微秒）                 */
} FaceResult;

/* 每个算法的统计（验证集/测试集各一份） */
typedef struct
{
  uint16_t enrolled_total;    /* 真值为已录入者的次数            */
  uint16_t enrolled_pass;     /* 其中：预测正确且通过            */
  uint16_t enrolled_reject;   /* 其中：被误拒（不通过）          */
  uint16_t enrolled_wrong;    /* 其中：通过了但认成了别人        */
  uint16_t stranger_total;    /* 真值为未录入者的次数            */
  uint16_t stranger_pass;     /* 其中：被误通过                  */
  uint32_t time_us_sum;
  uint16_t time_count;
} AlgoStats;

/* 特征/模型在 SDRAM 里的位置（在 pinmap.h 的 FACE_FEAT / ALGO_WS 区内） */
#define LBP_FEAT_ADDR       (FACE_FEAT_ADDR)
#define COLOR_FEAT_ADDR     (FACE_FEAT_ADDR + ((uint32_t)FACE_MAX_RECORDS * LBP_HIST_LEN * 2U))
#define VAL_CACHE_ADDR      (FACE_FEAT_ADDR + 0x00045000U)
#define VAL_CACHE_MAX       12U

/* ---- 生命周期 ---- */
void    FaceAlgo_Bind(FaceDb *db);
uint8_t FaceAlgo_Build(void);                 /* 提特征 + 训练 PCA；返回失败算法的位掩码 */
uint8_t FaceAlgo_IsReady(FaceAlgo a);
uint8_t FaceAlgo_SampleCount(void);

/* ---- 识别 ---- */
uint8_t FaceAlgo_Recognize(FaceAlgo a, const FaceImage *q, FaceResult *res);

/* ---- 阈值 ---- */
float   FaceAlgo_Threshold(FaceAlgo a);
void    FaceAlgo_SetThreshold(FaceAlgo a, float v);
uint8_t FaceAlgo_IsCalibrated(FaceAlgo a);

/* ---- 验证集缓存与阈值标定 ---- */
uint8_t FaceAlgo_ValAdd(uint8_t truth_person, const FaceImage *img);  /* 0=成功 2=缓存满 */
uint8_t FaceAlgo_ValCount(void);
void    FaceAlgo_ValClear(void);
uint8_t FaceAlgo_ValTruth(uint8_t idx);
const FaceImage *FaceAlgo_ValImage(uint8_t idx);
uint8_t FaceAlgo_Calibrate(FaceAlgo a, float *out_threshold);         /* 0=成功 */

/* ---- 统计 ---- */
void            FaceAlgo_StatsReset(uint8_t mode);
void            FaceAlgo_StatsResetAll(void);
void            FaceAlgo_StatsUpdate(uint8_t mode, FaceAlgo a, uint8_t truth_person,
                                     const FaceResult *r);
const AlgoStats *FaceAlgo_StatsGet(uint8_t mode, FaceAlgo a);

/* ---- 文字与占用 ---- */
const char *FaceAlgo_Name(FaceAlgo a);        /* "MSE"/"LBP"/"COLOR"/"PCA" */
const char *FaceAlgo_CnName(FaceAlgo a);      /* 中文名，给 LCD 用          */
uint32_t    FaceAlgo_Bytes(FaceAlgo a);       /* 该算法的特征/模型占用字节   */
uint32_t    FaceAlgo_TrainMs(void);           /* PCA 训练耗时（微秒级用 ms）  */

#ifdef __cplusplus
}
#endif

#endif /* __FACE_ALGO_H */

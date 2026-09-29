/**
  ******************************************************************************
  * @file    algo_mse.c
  * @brief   算法一：灰度模板匹配（MSE 最近邻 + 开放集阈值）
  *
  *  公式：  MSE(q, g) = (1/N) * sum_{p=1..N} ( q_p - g_p )^2 ，N = 48*48 = 2304
  *  决策：  取全库最小 MSE 的那个样本作为候选；只有当 MSE <= 阈值 才判"通过"。
  *
  *  计算量： 每个样本 2304 次减法+乘加；全库 30 条约 7 万次乘加，
  *           @180MHz 不到 1ms，是本实验最"轻"的算法。
  *
  *  为什么对亮度/位置敏感：
  *      1) MSE 逐像素对齐比较，人脸在取景框里平移几个像素，边缘处就会出现
  *         很大的差值（"错位惩罚"是平方级的），所以对位置偏差最敏感；
  *      2) 它是绝对灰度差，整体变亮/变暗会让所有像素同向偏移，
  *         MSE 随之整体抬高，所以对光照也敏感（不像直方图特征会归一化）。
  *      这两点正是报告里要对比的"三种算法的差异"。
  ******************************************************************************
  */

#include "face_algo.h"

/**
  * @brief  在整库上做 MSE 最近邻
  * @param  db     图库（用其中的 gray 模板）
  * @param  gray   查询图灰度
  * @param  person 输出：最近邻属于哪个人员
  * @return 最小 MSE；图库为空时返回 -1.0f
  */
float AlgoMse_Match(const FaceDb *db, const uint8_t *gray, uint8_t *person)
{
  float   best = -1.0f;
  uint8_t best_person = 0U;

  if ((db == NULL) || (gray == NULL) || (db->count == 0U))
  {
    if (person != NULL)
    {
      *person = 0U;
    }
    return -1.0f;
  }

  for (uint8_t i = 0U; i < FACE_MAX_RECORDS; i++)
  {
    if (db->rec[i].used == 0U)
    {
      continue;
    }

    float d = Image_Mse(gray, db->rec[i].img.gray);

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

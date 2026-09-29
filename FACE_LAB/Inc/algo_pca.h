/**
  ******************************************************************************
  * @file    algo_pca.h
  * @brief   人脸识别算法三：PCA / SVD 特征脸（Eigenfaces）—— 公共接口
  *
  *  【算法定位】
  *      另外两个算法是 MSE 模板匹配 与 LBP/颜色直方图匹配，都属于"逐像素/逐桶"
  *      的固定特征比较；PCA 是统计学习方法：先从训练集里学出"主成分方向"
  *      （特征脸），再把每张脸压缩成 k 个系数，最后在 k 维子空间里比距离。
  *
  *  【为什么不用 2304x2304 的协方差矩阵】
  *      图像是 48x48 灰度（FACE_PIXELS = 2304 维，见 image_proc.h）。
  *      直接对协方差矩阵 C = A^T A / n 做特征分解，矩阵本身就有
  *      2304*2304*4 B = 21 MB，特征分解还是 O(2304^3) = 1.2e10 次浮点运算，
  *      MCU 上完全不可行。
  *      但训练样本数 n <= 40 < 2304（小样本问题），所以改用 Gram 矩阵
  *          G = A^T A ,  尺寸 n x n (<= 40x40)          A 的列是中心化样本 a_i
  *      G 与 C = (1/n) A A^T 的非零特征向量一一对应（特征值只差常数因子 n）；
  *      若 G 的单位特征向量矩阵是 V，则特征脸
  *          u_j = sum_i V[i][j] * a_i
  *      也就是"主成分一定落在训练图张成的子空间里"，用 n 张训练图的线性组合
  *      就能表示出来。G 只有 40x40，O(n^3) 的 Jacobi 特征分解代价可以忽略。
  *
  *  【内存（重要）】
  *      PcaModel 约 229 KB（basis 就占 24*2304*4 = 221 KB），内部 SRAM 放不下，
  *      调用者必须自己定义它 —— 本工程的 app 把它放在外部 SDRAM。
  *      ★ 严禁把 PcaModel 定义成全局/静态变量 ★
  *
  *  【浮点】
  *      Cortex-M4F 单精度 FPU（-mfpu=fpv4-sp-d16 -mfloat-abi=hard），
  *      全程用 float：2304 维、<= 40 个样本的点积用单精度完全够（相对误差 ~1e-6），
  *      用 double 只会变成慢速的软件浮点。只用到 sqrtf / fabsf。
  *
  *  【开放集判据由调用者负责】
  *      Pca_Match 只给"最近的训练样本是谁 + 距离"，是否通过要看
  *      config.h 的 CFG_PCA_THRESHOLD（距离 <= 阈值才算识别通过）。
  ******************************************************************************
  */

#ifndef __ALGO_PCA_H
#define __ALGO_PCA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "image_proc.h"
#include "config.h"

#define PCA_MAX_SAMPLES  40U    /* 训练样本数上限：5 人 x 最多 6 张，留余量 */
#define PCA_MAX_K        24U    /* 主成分个数上限（结构体按此定长分配）    */

/* PCA 模型：约 229 KB，必须由调用者分配（本工程放外部 SDRAM） */
typedef struct
{
  uint16_t sample_count;                 /* 训练样本数 n                       */
  uint16_t k;                            /* 实际使用的主成分数                 */
  uint8_t  person_of[PCA_MAX_SAMPLES];   /* 每个样本属于哪个人员(1..5)          */
  float    mean[FACE_PIXELS];            /* 均值脸                             */
  float    basis[PCA_MAX_K][FACE_PIXELS];/* 单位化主成分（特征脸），只填前 k 行 */
  float    eigen[PCA_MAX_K];             /* 对应特征值（降序，只填前 k 个）      */
  float    proj[PCA_MAX_SAMPLES][PCA_MAX_K]; /* 训练样本在子空间里的投影        */
  uint32_t train_ms;                     /* 训练耗时（HAL_GetTick 差）          */
  uint32_t model_bytes;                  /* 模型占用字节数                      */
  uint8_t  ready;                        /* 1 = 模型可用                        */
} PcaModel;

/* ---------------------------------------------------------------------------
 * 训练
 *   samples      : n 张连续存放的 48x48 灰度图（n * FACE_PIXELS 字节）
 *   person_ids   : person_ids[i] = 第 i 张图的人员编号(1..5)
 *   n            : 样本数（2..PCA_MAX_SAMPLES）
 *   k            : 期望的主成分数，内部会截断成 min(k, n-1, PCA_MAX_K)，
 *                  并再丢弃特征值 <= 1e-6 * lambda_max 的成分（正常调用传 CFG_PCA_K）
 *   返回 0  成功
 *        -1  参数错（空指针 / n > PCA_MAX_SAMPLES / k == 0）
 *        -2  样本不足（n < 2）
 *        -3  数值失败（全零图、无有效主成分、G 退化）
 *   失败时 m->ready 保持 0，模型不可用；成功时 train_ms/model_bytes/ready 都已填好。
 * ------------------------------------------------------------------------- */
int  Pca_Train(PcaModel *m, const uint8_t *samples, const uint8_t *person_ids,
               uint16_t n, uint16_t k);

/* ---------------------------------------------------------------------------
 * 投影：把一张 48x48 灰度图投到子空间
 *   out 至少要有 PCA_MAX_K 个 float；只有前 m->k 个有效（其余被清零，防止误读）。
 *   定义：out[j] = u_j · (gray - mean) , j < k
 *   返回 0 成功，-1 参数错或模型未就绪。
 * ------------------------------------------------------------------------- */
int  Pca_Project(const PcaModel *m, const uint8_t *gray, float *out);

/* ---------------------------------------------------------------------------
 * 最近邻匹配：在训练样本的投影 m->proj[0..n-1][0..k-1] 里找欧氏距离最小的那个
 *   best_person : 该样本的人员编号(1..5)
 *   best_dist   : 对应的欧氏距离（k 维子空间内）
 *   返回 0 成功，-1 参数错或模型未就绪。
 *   ★ 是否判"识别通过"由调用者按 CFG_PCA_THRESHOLD 决定，本函数不做开放集判决。
 * ------------------------------------------------------------------------- */
int  Pca_Match(const PcaModel *m, const float *proj, uint8_t *best_person, float *best_dist);

/* 复位模型（全部清零，含 ready）。m 为 NULL 时直接返回 */
void Pca_Reset(PcaModel *m);

/* 模型需要调用者预留的字节数。PcaModel 是定长结构体，
 * 所以该值恒等于 sizeof(PcaModel)（当前约 229 KB），k 只用于合法性检查。 */
uint32_t Pca_ModelBytes(uint16_t k);

#ifdef __cplusplus
}
#endif

#endif /* __ALGO_PCA_H */

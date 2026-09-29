/**
  ******************************************************************************
  * @file    algo_pca.c
  * @brief   人脸识别算法三：PCA / SVD 特征脸（Eigenfaces）实现
  *
  *  实现路线（每一步的数学都写在对应函数上方的注释里，可当教程读）：
  *      1) pca_compute_mean : mean = (1/n) * sum_i x_i          —— 均值脸
  *      2) pca_build_gram   : G[i][j] = a_i · a_j , a_i = x_i - mean —— n x n
  *      3) pca_jacobi       : 循环 Jacobi 旋转求对称矩阵 G 的特征分解
  *      4) pca_sort_desc    : 特征值降序，特征向量按列同步交换
  *      5) pca_build_basis  : u_j = sum_i V[i][j] * a_i 并单位化 —— 特征脸
  *      6) pca_build_proj   : proj_ij = u_j · (x_i - mean)      —— 训练投影
  *      7) Pca_Project / Pca_Match : 投影 + k 维欧氏最近邻
  *
  *  【SRAM 规划】
  *      本文件只有三个文件级静态工作数组（与像素数无关，只和样本数 n<=40 有关）：
  *          s_g   : Gram 矩阵 G (n x n)      40*40*4 = 6400 B
  *          s_v   : G 的特征向量矩阵 V(n x n) 40*40*4 = 6400 B
  *          s_lam : G 的特征值 (n)              40*4 =  160 B
  *      合计约 12.7 KB。
  *      ★ 刻意不缓存中心化样本 a_i 的 2304x40 浮点矩阵（那要 368 KB）：
  *        凡是需要 a_i 的地方，都从 samples[] 和 mean[] 现场重算 —— 见
  *        pca_build_gram / pca_build_basis / pca_build_proj 内的写法。
  *      ★ PcaModel 约 229 KB，本文件绝不定义它的实例，由调用者放外部 SDRAM。
  *
  *  【为什么 O(n^3) 的 Jacobi 可以接受】
  *      n <= 40，G 最多 40x40：一次 sweep 约 780 次旋转，每次旋转 O(n)，
  *      即一个 sweep 约 6 万次浮点运算；实际约 10 次 sweeps 收敛，
  *      总共几毫秒量级。对比"分解 2304x2304" 的 1.2e10 次运算，差了 5 个数量级。
  *
  *  【浮点】
  *      全程 float（Cortex-M4F 单精度 FPU，-mfloat-abi=hard），点积也在 float 里累加；
  *      2304 项、每项 <= 255*255 时和最大约 1.5e8，相对误差仍在 1e-6 量级，
  *      对小样本识别足够，且不会退化成慢速软件双精度。只用 sqrtf / fabsf。
  ******************************************************************************
  */

#include "algo_pca.h"

#include <math.h>
#include <string.h>

/* ==========================================================================
 * 常量
 * ========================================================================== */

#define PCA_JACOBI_MAX_SWEEP   100U     /* 最大 sweep 次数（正常几次就收敛）      */
#define PCA_JACOBI_EPS         1.0e-9f  /* 相对收敛阈值：‖off‖/‖A‖ < eps          */
#define PCA_KILL_EPS           1.0e-24f /* 绝对零门限（G 整体很小时用）            */
#define PCA_KILL_REL           1.0e-7f  /* 相对零门限：|a_pq| <= 1e-7*max|a_pp| 视为 0 */
#define PCA_EIG_FLOOR          1.0e-6f  /* 有效特征值下限：lambda > 1e-6*lambda_max */
#define PCA_NORM_EPS           1.0e-12f /* 特征脸 L2 范数下限（约 0 则丢弃该成分）  */

/* ==========================================================================
 * 文件级静态工作区（见文件头"SRAM 规划"，合计约 12.7 KB）
 * 注意：这些都是"与像素数无关"的小矩阵（n <= PCA_MAX_SAMPLES = 40），
 *       不是模型存储；模型 PcaModel 由调用者提供。
 * ========================================================================== */

static float s_g[PCA_MAX_SAMPLES][PCA_MAX_SAMPLES];   /* Gram 矩阵 G（也是 Jacobi 的工作矩阵） */
static float s_v[PCA_MAX_SAMPLES][PCA_MAX_SAMPLES];   /* G 的特征向量矩阵 V（每一列一个特征向量）*/
static float s_lam[PCA_MAX_SAMPLES];                  /* G 的特征值（Jacobi 收敛后取自对角线）  */

/* ==========================================================================
 * 1. 均值脸
 * ========================================================================== */

/**
 * @brief 计算训练集均值脸 mean[p] = (1/n) * sum_i x_i[p]
 *
 * 均值是 PCA 的第一步：只有把每张脸都减去均值（中心化），
 * 得到的数据云才是"以原点为中心"的，其协方差矩阵才等于 (1/n) A A^T。
 * 不做这一步，第一个主成分会退化地指向"平均亮度方向"，而不是人脸差异方向。
 */
static void pca_compute_mean(PcaModel *m, const uint8_t *samples, uint16_t n)
{
  uint16_t i;
  uint32_t p;

  /* 累加器清零 */
  for (p = 0U; p < FACE_PIXELS; p++) {
    m->mean[p] = 0.0f;
  }

  /* 逐张累加（先加后除：整张图只做一次除法，比逐像素除快） */
  for (i = 0U; i < n; i++) {
    const uint8_t *x = samples + ((uint32_t)i * (uint32_t)FACE_PIXELS);
    for (p = 0U; p < FACE_PIXELS; p++) {
      m->mean[p] += (float)x[p];
    }
  }

  {
    const float inv_n = 1.0f / (float)n;
    for (p = 0U; p < FACE_PIXELS; p++) {
      m->mean[p] *= inv_n;
    }
  }
}

/* ==========================================================================
 * 2. Gram 矩阵 G = A^T A（n x n，小样本 PCA 的关键）
 * ========================================================================== */

/**
 * @brief 构造 G[i][j] = a_i · a_j = sum_p (x_i[p]-mean[p]) * (x_j[p]-mean[p])
 *
 * 为什么不构造 2304x2304 的协方差矩阵：
 *      C = (1/n) A A^T 是 2304x2304（21 MB，而且秩 <= n-1），
 *      G = A^T A 只有 n x n（<= 40x40）。二者的非零特征向量一一对应，
 *      特征值只差常数因子 n（lambda_G = n * lambda_C，见 pca_build_basis 的说明），
 *      且 G 的特征向量 V 可以线性组合出 C 的特征向量（见 pca_build_basis）。
 * 为什么这里现场减均值而不是缓存 a_i：
 *      a_i 一共 n*2304 个 float，n=40 时是 368 KB —— 内部 SRAM 放不下，
 *      所以每个元素临时算一次减法（多花的时间远小于省下的内存代价）。
 * 复杂度：n(n+1)/2 次 2304 维点积，n=40 时约 180 万次乘加，几十毫秒量级。
 */
static void pca_build_gram(const PcaModel *m, const uint8_t *samples, uint16_t n)
{
  uint16_t i;
  uint16_t j;
  uint32_t p;

  for (i = 0U; i < n; i++) {
    const uint8_t *xi = samples + ((uint32_t)i * (uint32_t)FACE_PIXELS);

    for (j = 0U; j <= i; j++) {
      const uint8_t *xj = samples + ((uint32_t)j * (uint32_t)FACE_PIXELS);
      float s = 0.0f;   /* 用 float 累加，保持 FPU 满速 */

      for (p = 0U; p < FACE_PIXELS; p++) {
        const float ai = (float)xi[p] - m->mean[p];
        const float aj = (float)xj[p] - m->mean[p];
        s += ai * aj;
      }

      s_g[i][j] = s;
      s_g[j][i] = s;    /* G 是对称矩阵，只算一半再镜像，省一半时间 */
    }
  }
}

/* ==========================================================================
 * 3. 循环 Jacobi（cyclic Jacobi）特征分解
 * ========================================================================== */

/**
 * @brief 对称矩阵 s_g 的特征分解：s_g 的对角线 -> s_lam，特征向量 -> s_v 的列
 *
 * 【原理】对称矩阵一定能被正交相似变换对角化。Jacobi 方法不一次性求分解，
 *         而是反复"消掉一个非对角元素"：对每一对 (p,q) 左乘右乘一个
 *         只在第 p、q 行/列上非平凡的平面旋转矩阵 J(p,q,theta)：
 *             A' = J^T A J ,  V' = V J
 *         角度 theta 选得让 A'[p][q] = 0。所有 (p,q) 走一遍叫一个 sweep，
 *         一个 sweep 后原来被消掉的元素会重新长出小量，但整体非对角范数
 *         单调下降，通常几次 sweep 就降到机器精度（这个算法无条件收敛）。
 *
 *         旋转量用"较小的那个根"计算（Numerical Recipes 的写法），
 *         保证 |t| <= 1、旋转角 |theta| <= 45 度，数值上最稳：
 *             theta = (a_qq - a_pp) / (2 a_pq)
 *             t     = sgn(theta) / (|theta| + sqrt(theta^2 + 1))
 *             c     = 1 / sqrt(t^2 + 1) ,  s = t * c
 *         更新公式：
 *             a_pp' = a_pp - t*a_pq ,  a_qq' = a_qq + t*a_pq ,  a_pq' = 0
 *             a_rp' = c*a_rp - s*a_rq ,  a_rq' = s*a_rp + c*a_rq   (r != p,q)
 *
 * @return 0 成功；-3 数值失败（G 全零，即所有样本完全相同或全黑）
 */
static int pca_jacobi(uint16_t n)
{
  uint16_t p;
  uint16_t q;
  uint16_t r;
  uint16_t sweep;
  uint32_t rotated;
  float diag_sq;
  float off_sq;
  float scale;
  float max_lam;

  /* V 初始化为单位矩阵：每做一次旋转就右乘一次 J，
   * 迭代结束时 V 的每一列就是 G 的一个单位特征向量（V 恒为正交矩阵）。 */
  for (p = 0U; p < n; p++) {
    for (q = 0U; q < n; q++) {
      s_v[p][q] = (p == q) ? 1.0f : 0.0f;
    }
  }

  for (sweep = 0U; sweep < PCA_JACOBI_MAX_SWEEP; sweep++) {
    /* ---- 收敛判据：非对角元素平方和相对对角元素平方和可忽略 ----
     * ‖off‖/‖A‖ < eps  <=>  off_sq < eps^2 * diag_sq
     * （加 1e-30f 防止 diag_sq 为 0 时判据恒真） */
    diag_sq = 0.0f;
    off_sq = 0.0f;
    scale = 0.0f;   /* 矩阵量级 max|a_pp|，用于定义"相对零"（float 分辨率约 1e-7） */
    for (p = 0U; p < n; p++) {
      const float dg = fabsf(s_g[p][p]);
      diag_sq += s_g[p][p] * s_g[p][p];
      if (dg > scale) {
        scale = dg;
      }
      for (q = p + 1U; q < n; q++) {
        off_sq += s_g[p][q] * s_g[p][q];
      }
    }
    if (off_sq <= ((PCA_JACOBI_EPS * PCA_JACOBI_EPS) * (diag_sq + 1.0e-30f))) {
      break;
    }

    rotated = 0U;

    /* ---- 一个 sweep：按顺序把所有上三角元素 (p,q) 各消一次 ---- */
    for (p = 0U; (p + 1U) < n; p++) {
      for (q = p + 1U; q < n; q++) {
        const float apq = s_g[p][q];
        float app;
        float aqq;
        float theta;
        float t;
        float c;
        float s;

        /* 已经是 0、或已小到"相对零"（float 噪声量级）就不用转，也避免 0/0。
         * 用相对门限的原因：Gram 的元素量级可达 1e6，float 的相对分辨率 1.2e-7，
         * 所以舍入噪声底就在 1e-7*scale 附近，绝对门限 1e-24 永远达不到。 */
        if ((fabsf(apq) <= PCA_KILL_EPS) || (fabsf(apq) <= (PCA_KILL_REL * scale))) {
          s_g[p][q] = 0.0f;
          s_g[q][p] = 0.0f;
          continue;
        }

        app = s_g[p][p];
        aqq = s_g[q][q];

        /* 求旋转参数（见函数头公式） */
        theta = (aqq - app) / (2.0f * apq);
        if (theta >= 0.0f) {
          t = 1.0f / (theta + sqrtf((theta * theta) + 1.0f));
        } else {
          t = 1.0f / (theta - sqrtf((theta * theta) + 1.0f));
        }
        c = 1.0f / sqrtf((t * t) + 1.0f);
        s = t * c;

        /* 对角两个元素被修正，a_pq 被精确置 0 */
        s_g[p][p] = app - (t * apq);
        s_g[q][q] = aqq + (t * apq);
        s_g[p][q] = 0.0f;
        s_g[q][p] = 0.0f;

        /* 第 p、q 行/列（除 p,q 自身）按同一个旋转更新，保持对称 */
        for (r = 0U; r < n; r++) {
          if ((r != p) && (r != q)) {
            const float arp = s_g[r][p];
            const float arq = s_g[r][q];
            s_g[r][p] = (c * arp) - (s * arq);
            s_g[p][r] = s_g[r][p];
            s_g[r][q] = (s * arp) + (c * arq);
            s_g[q][r] = s_g[r][q];
          }
        }

        /* 特征向量矩阵同步右乘 J：V <- V * J */
        for (r = 0U; r < n; r++) {
          const float vrp = s_v[r][p];
          const float vrq = s_v[r][q];
          s_v[r][p] = (c * vrp) - (s * vrq);
          s_v[r][q] = (s * vrp) + (c * vrq);
        }

        rotated++;
      }
    }

    /* float 只有约 1e-7 的相对精度，上面 1e-9 的相对阈值在 float 下通常达不到；
     * 所以再给一个实用出口：一个 sweep 里一次旋转都没做，说明所有非对角元素
     * 都已降到"相对零"门限（PCA_KILL_REL * 矩阵量级）以下，即已到达 float
     * 能表示的最优精度，直接退出（正常情况下 10 次 sweep 左右）。 */
    if (rotated == 0U) {
      break;
    }
  }

  /* 收敛后 A 的对角线就是 G 的特征值（此时全为实数，且应 >= 0） */
  max_lam = 0.0f;
  for (p = 0U; p < n; p++) {
    s_lam[p] = s_g[p][p];
    if (fabsf(s_lam[p]) > max_lam) {
      max_lam = fabsf(s_lam[p]);
    }
  }
  if (max_lam <= 0.0f) {
    return -3;   /* G 全零：样本全同或全黑，没有任何可用的主成分 */
  }
  return 0;
}

/* ==========================================================================
 * 4. 特征值降序排序
 * ========================================================================== */

/**
 * @brief 简单选择排序：s_lam 降序，s_v 的对应列同步交换
 *
 * 主成分必须按特征值从大到小排列（特征值 = 该方向上的方差），
 * 取前 k 个就是"保留最多信息"的 k 个方向。n <= 40，O(n^2) 足够。
 */
static void pca_sort_desc(uint16_t n)
{
  uint16_t i;
  uint16_t j;
  uint16_t r;
  uint16_t best;

  for (i = 0U; (i + 1U) < n; i++) {
    best = i;
    for (j = i + 1U; j < n; j++) {
      if (s_lam[j] > s_lam[best]) {
        best = j;
      }
    }
    if (best != i) {
      const float tmp = s_lam[i];
      s_lam[i] = s_lam[best];
      s_lam[best] = tmp;

      /* 特征向量是 s_v 的"列"，交换必须整列一起换，否则 eigen 与 basis 对不上 */
      for (r = 0U; r < n; r++) {
        const float tv = s_v[r][i];
        s_v[r][i] = s_v[r][best];
        s_v[r][best] = tv;
      }
    }
  }
}

/* ==========================================================================
 * 5. 由 G 的特征向量合成特征脸（并单位化、并按特征值筛掉退化成分）
 * ========================================================================== */

/**
 * @brief 生成前若干张特征脸到 m->basis，特征值到 m->eigen，实际个数到 m->k
 *
 * 公式：u_j = sum_i V[i][j] * a_i ， a_i = x_i - mean
 *       （V 的第 j 列是 G 的第 j 个特征向量；u_j 与 C 的特征向量同方向）
 *
 * 单位化：u_j 必须归一化成单位向量，投影 proj_j = u_j·a 才是"沿该方向的坐标"，
 *         距离也才有统一尺度。数学上 ‖u_j‖ = sqrt(lambda_j)，但直接除以
 *         实测 L2 范数更稳（不必对 lambda 开方，也不会出现 1/0）。
 *
 * 特征值筛选（实验要求的数值保护）：
 *      lambda_j <= 1e-6 * lambda_max 的成分丢掉。中心化后秩 <= n-1，
 *      最后一个特征值理论上就是 0（甚至因舍入变成 -1e-3 量级的小负数），
 *      留着它只会往距离里灌噪声甚至引起除零，所以必须过滤。
 *
 * 效率说明：合成 k 张特征脸是 k*n*2304 次乘加（k=16,n=25 时约 88 万次），
 *           完全可以接受；因此这里刻意不缓存 a_i 矩阵，而是每张特征脸都从
 *           samples[] 和 mean[] 现场重算 a_i —— 用一点时间换 368 KB SRAM。
 *
 * @param kmax 已经截断过的 k 上限（= min(k, n-1, PCA_MAX_K)）
 * @return 0 成功；-3 无任何有效主成分
 */
static int pca_build_basis(PcaModel *m, const uint8_t *samples, uint16_t n, uint16_t kmax)
{
  uint16_t i;
  uint16_t j;
  uint16_t out;
  uint32_t p;
  float lam_max;
  float floor_eig;

  if (n == 0U) {
    return -3;
  }
  lam_max = s_lam[0];                          /* 已降序，第 0 个就是最大特征值 */

  /* 数值保护：最大特征值必须为正，否则没有可归一化的方向 */
  if (!(lam_max > 0.0f)) {
    return -3;
  }
  floor_eig = PCA_EIG_FLOOR * lam_max;         /* 有效特征值下限 */

  out = 0U;                                    /* out = 真正写出的成分个数 */

  for (j = 0U; j < kmax; j++) {
    float *row;
    float norm2;
    float nrm;

    /* 因为已降序，"不满足下限"之后的所有成分也都不满足，可以直接停 */
    if (!(s_lam[j] > floor_eig)) {
      break;
    }

    row = m->basis[out];                       /* 先当作输出行，退化时 out 不递增 */

    /* u_j = sum_i V[i][j] * (x_i - mean) */
    for (p = 0U; p < FACE_PIXELS; p++) {
      row[p] = 0.0f;
    }
    for (i = 0U; i < n; i++) {
      const float w = s_v[i][j];
      const uint8_t *xi = samples + ((uint32_t)i * (uint32_t)FACE_PIXELS);
      if (w != 0.0f) {                         /* 系数为 0 的行可以整行跳过 */
        for (p = 0U; p < FACE_PIXELS; p++) {
          row[p] += w * ((float)xi[p] - m->mean[p]);
        }
      }
    }

    /* 单位化：除以自身 L2 范数 */
    norm2 = 0.0f;
    for (p = 0U; p < FACE_PIXELS; p++) {
      norm2 += row[p] * row[p];
    }
    nrm = sqrtf(norm2);
    if (nrm <= PCA_NORM_EPS) {
      continue;   /* 范数约 0：该成分退化（理论上不该发生），跳过不算一个成分 */
    }
    {
      const float inv = 1.0f / nrm;
      for (p = 0U; p < FACE_PIXELS; p++) {
        row[p] *= inv;
      }
    }

    /* 特征值与特征脸一一对应。
     * 注意这里存的是 Gram 矩阵 G = A^T A 的特征值（= n * 协方差矩阵的特征值），
     * 二者只差一个常数因子 n，作为"该主成分的重要性/方差"排序与筛选完全等价。 */
    m->eigen[out] = s_lam[j];
    out++;
  }

  if (out == 0U) {
    return -3;                                 /* 没有任何有效主成分 */
  }
  m->k = out;
  return 0;
}

/* ==========================================================================
 * 6. 训练样本的投影（建库）
 * ========================================================================== */

/**
 * @brief proj[i][j] = u_j · (x_i - mean)，i < n，j < k
 *
 * 投影就是"这张脸在第 j 个主成分方向上有多像"。
 * 识别时拿新图的投影与这里的每一行比距离，就等价于在原空间里比较，
 * 但只需要 k 次乘加（k <= 24），而不是 2304 维。
 */
static void pca_build_proj(PcaModel *m, const uint8_t *samples, uint16_t n)
{
  uint16_t i;
  uint16_t j;
  uint32_t p;

  for (i = 0U; i < n; i++) {
    const uint8_t *xi = samples + ((uint32_t)i * (uint32_t)FACE_PIXELS);

    for (j = 0U; j < m->k; j++) {
      const float *row = m->basis[j];
      float s = 0.0f;

      for (p = 0U; p < FACE_PIXELS; p++) {
        s += row[p] * ((float)xi[p] - m->mean[p]);
      }
      m->proj[i][j] = s;
    }
  }
}

/* ==========================================================================
 * 公共 API
 * ========================================================================== */

int Pca_Train(PcaModel *m, const uint8_t *samples, const uint8_t *person_ids,
              uint16_t n, uint16_t k)
{
  uint16_t i;
  uint16_t kmax;
  uint32_t t0;
  int rc;

  /* ---- 1) 参数检查 ---- */
  if ((m == NULL) || (samples == NULL) || (person_ids == NULL)) {
    return -1;
  }
  if (n < 2U) {
    return -2;                       /* 1 张图（或 0 张）求不出任何主成分 */
  }
  if ((n > PCA_MAX_SAMPLES) || (k == 0U)) {
    return -1;                       /* 超出模块容量 / k 非法 */
  }

  /* ---- 2) k = min(k_requested, n-1, PCA_MAX_K) ----
   * 中心化后 A 的每列之和为 0，所以 rank(A) <= n-1，最多只有 n-1 个非零特征值；
   * PCA_MAX_K = 24 是结构体数组的硬上限。 */
  if (k > PCA_MAX_K) {
    k = PCA_MAX_K;
  }
  if (k > (uint16_t)(n - 1U)) {
    k = (uint16_t)(n - 1U);
  }
  kmax = k;

  /* 先清 ready：中途任何失败都不会让上层用到半成品模型 */
  m->ready = 0U;
  m->k = 0U;
  m->sample_count = n;
  for (i = 0U; i < n; i++) {
    m->person_of[i] = person_ids[i];
  }

  t0 = HAL_GetTick();                            /* 训练计时开始 */

  /* ---- 3) 均值脸 ---- */
  pca_compute_mean(m, samples, n);

  /* ---- 4) Gram 矩阵 G = A^T A (n x n) ---- */
  pca_build_gram(m, samples, n);

  /* ---- 5) Jacobi 特征分解 G = V * diag(lambda) * V^T ---- */
  rc = pca_jacobi(n);
  if (rc != 0) {
    return rc;                                   /* -3：G 退化 */
  }

  /* ---- 6) 特征值降序 ---- */
  pca_sort_desc(n);
  if (!(s_lam[0] > 0.0f)) {
    return -3;                                   /* 最大特征值非正 -> 无数值有效解 */
  }

  /* ---- 7) 合成 + 单位化特征脸，并按特征值下限筛掉退化成分 ---- */
  rc = pca_build_basis(m, samples, n, kmax);
  if (rc != 0) {
    return rc;                                   /* -3：没有有效主成分 */
  }

  /* ---- 8) 训练样本在子空间中的投影（建库） ---- */
  pca_build_proj(m, samples, n);

  /* ---- 9) 收尾 ---- */
  m->train_ms = HAL_GetTick() - t0;
  m->model_bytes = Pca_ModelBytes(m->k);
  m->ready = 1U;
  return 0;
}

int Pca_Project(const PcaModel *m, const uint8_t *gray, float *out)
{
  uint16_t j;
  uint32_t p;

  if ((m == NULL) || (gray == NULL) || (out == NULL)) {
    return -1;
  }
  if ((m->ready != 1U) || (m->k == 0U) || (m->k > PCA_MAX_K)) {
    return -1;                                   /* 模型还没训练好 */
  }

  /* proj_j = u_j · (x - mean)：与训练时完全相同的定义，否则尺度会不一致 */
  for (j = 0U; j < m->k; j++) {
    const float *row = m->basis[j];
    float s = 0.0f;

    for (p = 0U; p < FACE_PIXELS; p++) {
      s += row[p] * ((float)gray[p] - m->mean[p]);
    }
    out[j] = s;
  }

  /* 后面 k..PCA_MAX_K-1 没有意义，清零，避免调用者误用脏值 */
  for (j = m->k; j < PCA_MAX_K; j++) {
    out[j] = 0.0f;
  }
  return 0;
}

int Pca_Match(const PcaModel *m, const float *proj, uint8_t *best_person, float *best_dist)
{
  uint16_t i;
  uint16_t j;
  float best;

  if ((m == NULL) || (proj == NULL) || (best_person == NULL) || (best_dist == NULL)) {
    return -1;
  }
  if ((m->ready != 1U) || (m->k == 0U) || (m->k > PCA_MAX_K) ||
      (m->sample_count == 0U) || (m->sample_count > PCA_MAX_SAMPLES)) {
    return -1;
  }

  /* 最近邻：在训练样本的投影里找欧氏距离最小的一个（k 维子空间） */
  best = 0.0f;
  *best_person = m->person_of[0];
  *best_dist = 0.0f;

  for (i = 0U; i < m->sample_count; i++) {
    float d2 = 0.0f;                             /* 先累加平方和，最后只开一次方 */
    float d;

    for (j = 0U; j < m->k; j++) {
      const float diff = proj[j] - m->proj[i][j];
      d2 += diff * diff;
    }
    d = sqrtf(d2);

    if ((i == 0U) || (d < best)) {
      best = d;
      *best_person = m->person_of[i];            /* 1..5，由调用者录入的数据保证 */
      *best_dist = d;
    }
  }

  /* 注意：这里不做"通过/不通过"判决 —— 开放集阈值由调用者用
   * CFG_PCA_THRESHOLD 判断（best_dist <= 阈值才是识别通过）。 */
  return 0;
}

void Pca_Reset(PcaModel *m)
{
  if (m == NULL) {
    return;
  }
  (void)memset(m, 0, sizeof(PcaModel));          /* 约 229 KB，清零后 ready = 0 */
}

uint32_t Pca_ModelBytes(uint16_t k)
{
  /* k 的合法范围是 1..PCA_MAX_K，越界按上限处理（与 Pca_Train 的截断一致）。
   * 说明：PcaModel 是"定长结构体"——basis 固定 PCA_MAX_K 行、proj 固定
   * PCA_MAX_K 列，所以调用者必须预留的字节数与 k 无关，恒为 sizeof(PcaModel)
   * （当前约 229 KB）。k 只影响实际被填满的行/列数。 */
  if ((k == 0U) || (k > PCA_MAX_K)) {
    k = PCA_MAX_K;
  }
  (void)k;                                       /* 仅做范围检查，结构体定长 */
  return (uint32_t)sizeof(PcaModel);
}

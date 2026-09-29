/**
  ******************************************************************************
  * @file    image_proc.c
  * @brief   取景裁剪 / 灰度化 / 质量检查 / LBP 与颜色直方图 / 距离计算
  *
  *  这一层只做"像素到特征"的确定性变换，不含任何识别决策，
  *  好处是：录入和识别调用的是同一套函数，天然满足实验要求里
  *  "录入和识别使用同一裁剪与预处理方法"。
  ******************************************************************************
  */

#include "image_proc.h"
#include "config.h"

/* ==========================================================================
 * RGB565 -> RGB888（把 5/6/5 位按比例展开到 8 位）
 *   5 位展开：v8 = (v5 << 3) | (v5 >> 2)
 *   6 位展开：v8 = (v6 << 2) | (v6 >> 4)
 * ========================================================================== */
static uint8_t rgb565_red(uint16_t c)
{
  uint32_t v = (c >> 11) & 0x1FU;
  return (uint8_t)((v << 3) | (v >> 2));
}

static uint8_t rgb565_green(uint16_t c)
{
  uint32_t v = (c >> 5) & 0x3FU;
  return (uint8_t)((v << 2) | (v >> 4));
}

static uint8_t rgb565_blue(uint16_t c)
{
  uint32_t v = c & 0x1FU;
  return (uint8_t)((v << 3) | (v >> 2));
}

/* ==========================================================================
 * 预处理
 * ========================================================================== */

/**
  * @brief  从整幅帧里按取景框裁剪，并最近邻缩放成 48x48
  * @note   采样点取每个目标像素对应源区域的中心，避免整体偏移半格
  */
uint8_t Image_CropToFace(const uint16_t *frame, uint16_t frame_w, uint16_t frame_h,
                         uint16_t crop_x, uint16_t crop_y, uint16_t crop_w, uint16_t crop_h,
                         FaceImage *out)
{
  if ((frame == NULL) || (out == NULL) || (crop_w == 0U) || (crop_h == 0U))
  {
    return 1U;
  }
  if (((uint32_t)crop_x + crop_w > frame_w) || ((uint32_t)crop_y + crop_h > frame_h))
  {
    return 2U;                              /* 取景框超出帧范围 */
  }

  for (uint16_t y = 0U; y < FACE_H; y++)
  {
    uint32_t sy = (uint32_t)crop_y + (((uint32_t)y * crop_h) + (crop_h / 2U)) / FACE_H;
    const uint16_t *src_row = &frame[sy * frame_w];

    for (uint16_t x = 0U; x < FACE_W; x++)
    {
      uint32_t sx = (uint32_t)crop_x + (((uint32_t)x * crop_w) + (crop_w / 2U)) / FACE_W;
      out->rgb565[(uint32_t)y * FACE_W + x] = src_row[sx];
    }
  }

  Image_ToGray(out);
  (void)Image_QualityCheck(out);

  return 0U;
}

/**
  * @brief  RGB565 -> 灰度，BT.601 亮度公式
  *         Y = (77R + 150G + 29B) >> 8   （77+150+29 = 256，刚好右移 8 位）
  */
void Image_ToGray(FaceImage *img)
{
  if (img == NULL)
  {
    return;
  }

  for (uint32_t i = 0U; i < FACE_PIXELS; i++)
  {
    uint16_t c = img->rgb565[i];
    uint32_t y = (77U * (uint32_t)rgb565_red(c))
               + (150U * (uint32_t)rgb565_green(c))
               + (29U * (uint32_t)rgb565_blue(c));
    img->gray[i] = (uint8_t)(y >> 8);
  }
}

uint32_t Image_MeanBrightness(const uint8_t *gray)
{
  uint32_t sum = 0U;

  if (gray == NULL)
  {
    return 0U;
  }
  for (uint32_t i = 0U; i < FACE_PIXELS; i++)
  {
    sum += gray[i];
  }
  return sum / FACE_PIXELS;
}

/**
  * @brief  用 5% / 95% 分位差近似"对比度动态范围"
  *         比 max-min 稳：单个坏点不会把范围拉开
  */
uint32_t Image_ContrastRange(const uint8_t *gray)
{
  uint16_t hist[256];
  uint32_t acc = 0U;
  uint32_t lo = 0U;
  uint32_t hi = 255U;
  uint32_t target_lo = (FACE_PIXELS * 5U) / 100U;
  uint32_t target_hi = (FACE_PIXELS * 95U) / 100U;

  if (gray == NULL)
  {
    return 0U;
  }

  for (uint32_t i = 0U; i < 256U; i++)
  {
    hist[i] = 0U;
  }
  for (uint32_t i = 0U; i < FACE_PIXELS; i++)
  {
    hist[gray[i]]++;
  }

  for (uint32_t i = 0U; i < 256U; i++)
  {
    acc += hist[i];
    if (acc >= target_lo)
    {
      lo = i;
      break;
    }
  }

  acc = 0U;
  for (uint32_t i = 0U; i < 256U; i++)
  {
    acc += hist[i];
    if (acc >= target_hi)
    {
      hi = i;
      break;
    }
  }

  return (hi > lo) ? (hi - lo) : 0U;
}

uint8_t Image_QualityCheck(FaceImage *img)
{
  uint32_t mean;
  uint32_t range;
  uint8_t  q = IMG_OK;

  if (img == NULL)
  {
    return IMG_OK;
  }

  mean  = Image_MeanBrightness(img->gray);
  range = Image_ContrastRange(img->gray);

  if (mean < CFG_BRIGHT_MIN)
  {
    q = IMG_TOO_DARK;
  }
  else if (mean > CFG_BRIGHT_MAX)
  {
    q = IMG_TOO_BRIGHT;
  }
  else if (range < CFG_CONTRAST_MIN)
  {
    q = IMG_LOW_CONTRAST;
  }

  img->quality = q;
  return q;
}

/* ==========================================================================
 * 手工特征
 * ========================================================================== */

/**
  * @brief  3x3 邻域 LBP + 4x4 分区直方图
  *
  *  编码规则：以中心像素为阈值，周围 8 个邻域"大于等于中心"记 1，
  *            按顺时针从左上角开始组成 8 位二进制数（0~255）。
  *  分区规则：48x48 均分 4x4，每区 12x12，各自统计 256 桶直方图。
  *  归一化  ：每个分区单独归一化到 LBP_CELL_SUM，
  *            这样"亮一点暗一点"不影响距离，同时保留五官的空间位置。
  *
  *  为什么保留分区：整幅图共用一个直方图会丢掉空间信息
  *  （眼睛和脸颊的纹理混在一起），分区统计正是 LBP 人脸识别的经典做法。
  */
void Image_LbpHistogram(const uint8_t *gray, uint16_t *hist)
{
  const uint16_t cell_w = FACE_W / 4U;      /* 12 */
  const uint16_t cell_h = FACE_H / 4U;      /* 12 */

  if ((gray == NULL) || (hist == NULL))
  {
    return;
  }

  for (uint32_t i = 0U; i < LBP_HIST_LEN; i++)
  {
    hist[i] = 0U;
  }

  /* 边界一圈不参与（没有完整 3x3 邻域） */
  for (uint16_t y = 1U; y + 1U < FACE_H; y++)
  {
    const uint8_t *row_up = &gray[(uint32_t)(y - 1U) * FACE_W];
    const uint8_t *row_md = &gray[(uint32_t)y * FACE_W];
    const uint8_t *row_dn = &gray[(uint32_t)(y + 1U) * FACE_W];

    for (uint16_t x = 1U; x + 1U < FACE_W; x++)
    {
      uint8_t c = row_md[x];
      uint8_t code = 0U;

      if (row_up[x - 1U] >= c) { code |= 0x01U; }
      if (row_up[x]      >= c) { code |= 0x02U; }
      if (row_up[x + 1U] >= c) { code |= 0x04U; }
      if (row_md[x + 1U] >= c) { code |= 0x08U; }
      if (row_dn[x + 1U] >= c) { code |= 0x10U; }
      if (row_dn[x]      >= c) { code |= 0x20U; }
      if (row_dn[x - 1U] >= c) { code |= 0x40U; }
      if (row_md[x - 1U] >= c) { code |= 0x80U; }

      uint16_t cell = (uint16_t)(((y / cell_h) * 4U) + (x / cell_w));
      hist[(uint32_t)cell * LBP_BINS + code]++;
    }
  }

  /* 逐分区归一化 */
  for (uint16_t cell = 0U; cell < LBP_CELLS; cell++)
  {
    uint16_t *h = &hist[(uint32_t)cell * LBP_BINS];
    uint32_t sum = 0U;

    for (uint16_t b = 0U; b < LBP_BINS; b++)
    {
      sum += h[b];
    }
    if (sum == 0U)
    {
      continue;                             /* 全空分区保持全 0 */
    }
    for (uint16_t b = 0U; b < LBP_BINS; b++)
    {
      h[b] = (uint16_t)(((uint32_t)h[b] * LBP_CELL_SUM) / sum);
    }
  }
}

/**
  * @brief  RGB 各取高 3 位 -> 8x8x8 = 512 桶颜色直方图，整体归一化
  * @note   这是"只用颜色、没有空间信息"的对照特征：
  *         同一个人换个表情/姿态颜色分布变化不大（因此看着很稳），
  *         但背景颜色一变就会误判（因此误通过率高），正好用来对比 LBP。
  */
void Image_ColorHistogram(const uint16_t *rgb565, uint16_t *hist)
{
  uint32_t sum = 0U;

  if ((rgb565 == NULL) || (hist == NULL))
  {
    return;
  }

  for (uint32_t i = 0U; i < COLOR_BINS; i++)
  {
    hist[i] = 0U;
  }

  for (uint32_t i = 0U; i < FACE_PIXELS; i++)
  {
    uint16_t c = rgb565[i];
    uint8_t  r = (uint8_t)((c >> 13) & 0x07U);   /* 红高 3 位 */
    uint8_t  g = (uint8_t)((c >> 8) & 0x07U);    /* 绿高 3 位 */
    uint8_t  b = (uint8_t)((c >> 2) & 0x07U);    /* 蓝高 3 位 */
    hist[(uint16_t)((r << 6) | (g << 3) | b)]++;
  }

  for (uint32_t i = 0U; i < COLOR_BINS; i++)
  {
    sum += hist[i];
  }
  if (sum == 0U)
  {
    return;
  }
  for (uint32_t i = 0U; i < COLOR_BINS; i++)
  {
    hist[i] = (uint16_t)(((uint32_t)hist[i] * COLOR_HIST_SUM) / sum);
  }
}

/* ==========================================================================
 * 距离
 * ========================================================================== */

/**
  * @brief  灰度图均方误差  MSE = (1/N) * sum (a-b)^2
  * @note   最大累加值 2304 x 65025 = 1.5e8，uint32 放得下
  */
float Image_Mse(const uint8_t *a, const uint8_t *b)
{
  uint32_t acc = 0U;

  if ((a == NULL) || (b == NULL))
  {
    return 0.0f;
  }

  for (uint32_t i = 0U; i < FACE_PIXELS; i++)
  {
    int32_t d = (int32_t)a[i] - (int32_t)b[i];
    acc += (uint32_t)(d * d);
  }

  return (float)acc / (float)FACE_PIXELS;
}

/**
  * @brief  卡方距离  chi2 = sum (a-b)^2 / (a+b)
  * @param  norm_sum 直方图的归一化和（LBP 用 LBP_CELL_SUM，颜色用 COLOR_HIST_SUM）
  * @note   加 1 是为了 a=b=0 时也不除以 0；除以 norm_sum 让不同特征维度可比较
  */
float Image_ChiSquare(const uint16_t *a, const uint16_t *b, uint32_t len, uint16_t norm_sum)
{
  float acc = 0.0f;

  if ((a == NULL) || (b == NULL) || (len == 0U) || (norm_sum == 0U))
  {
    return 0.0f;
  }

  for (uint32_t i = 0U; i < len; i++)
  {
    float x = (float)a[i];
    float y = (float)b[i];
    float d = x - y;
    acc += (d * d) / (x + y + 1.0f);
  }

  return acc / (float)norm_sum;
}

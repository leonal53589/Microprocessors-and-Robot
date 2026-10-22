/**
  ******************************************************************************
  * @file    image_proc.h
  * @brief   图像预处理与手工特征：取景裁剪、灰度化、质量检查、LBP/颜色直方图
  *
  *  统一规格（录入与识别必须完全一致，这是实验要求的硬性前提）：
  *      取景框（QVGA 坐标，见 config.h 的 CFG_CROP_*）
  *        -> 最近邻缩放成 48x48
  *        -> 保留 RGB565 一份（存 Flash、LCD 回显）
  *        -> 生成灰度一份（所有算法都在灰度上算）
  *
  *  特征定义：
  *      LBP   : 3x3 邻域 8 位 LBP 编码（中心像素为阈值的二值比较），
  *              把 48x48 分成 4x4 = 16 个 12x12 分区，每区统计 256 桶直方图，
  *              每区归一化到 LBP_CELL_SUM，拼成 4096 维特征（保留空间信息）。
  *      COLOR : RGB 各 8 桶（3 位）-> 512 桶颜色直方图，整体归一化到
  *              COLOR_HIST_SUM，作为"只用颜色、没有空间信息"的对照特征。
  *
  *  距离（都是"越小越像"）：
  *      MSE       : 灰度差的平方平均值（整数像素，0~65025）
  *      ChiSquare : sum((a-b)^2/(a+b+eps)) / 归一化系数，用于上面两种直方图
  ******************************************************************************
  */

#ifndef __IMAGE_PROC_H
#define __IMAGE_PROC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define FACE_W              48U                          /* 人脸图宽（像素） */
#define FACE_H              48U                          /* 人脸图高（像素） */
#define FACE_PIXELS         (FACE_W * FACE_H)            /* 2304 */

#define LBP_CELLS           16U                          /* 4x4 分区 */
#define LBP_BINS            256U                         /* 3x3 邻域 -> 8 位编码 */
#define LBP_HIST_LEN        (LBP_CELLS * LBP_BINS)       /* 4096 维 */
#define LBP_CELL_SUM        10000U                       /* 每个分区归一化后的和 */

#define COLOR_BINS          512U                         /* 8x8x8 RGB */
#define COLOR_HIST_SUM      10000U

/* 全流程统一 R[15:11]/G[10:5]/B[4:0]，uint16_t 在 STM32 内存中为小端。
 * 摄像头高字节先到的数据只在完整帧入口交换一次，裁剪/存储/显示不再交换。 */
typedef struct
{
  uint16_t rgb565[FACE_PIXELS];
  uint8_t  gray[FACE_PIXELS];
  uint8_t  quality;             /* Image_QualityCheck 的返回值，0 表示合格 */
  uint8_t  reserved[3];
} FaceImage;

/* 质量检查结果 */
#define IMG_OK              0U
#define IMG_TOO_DARK        1U
#define IMG_TOO_BRIGHT      2U
#define IMG_LOW_CONTRAST    3U

/* ---- 预处理 ---- */

/* 将高字节先到的 DMA 原始像素转换成 CPU/LTDC 使用的 RGB565 数值。 */
void Image_RGB565SwapBytes(uint16_t *pixels, uint32_t count);

/* 从整幅 RGB565 帧里按取景框裁剪并最近邻缩放成 48x48；0 = 成功 */
uint8_t Image_CropToFace(const uint16_t *frame, uint16_t frame_w, uint16_t frame_h,
                         uint16_t crop_x, uint16_t crop_y, uint16_t crop_w, uint16_t crop_h,
                         FaceImage *out);

/* 用 BT.601 系数把 RGB565 转灰度：Y = (77R + 150G + 29B) >> 8 */
void Image_ToGray(FaceImage *img);

/* 0=合格，1=太暗，2=太亮，3=对比度太低（写进 img->quality 并返回） */
uint8_t Image_QualityCheck(FaceImage *img);

uint32_t Image_MeanBrightness(const uint8_t *gray);
uint32_t Image_ContrastRange(const uint8_t *gray);

/* ---- 手工特征 ---- */

/* 3x3 LBP + 4x4 分区直方图，每区归一化到 LBP_CELL_SUM */
void Image_LbpHistogram(const uint8_t *gray, uint16_t *hist);

/* RGB 8x8x8 颜色直方图，整体归一化到 COLOR_HIST_SUM */
void Image_ColorHistogram(const uint16_t *rgb565, uint16_t *hist);

/* ---- 距离 ---- */

/* 灰度图的均方误差（逐像素差的平方平均） */
float Image_Mse(const uint8_t *a, const uint8_t *b);

/* 卡方距离：sum((a-b)^2/(a+b)) / norm_sum，用于直方图特征 */
float Image_ChiSquare(const uint16_t *a, const uint16_t *b, uint32_t len, uint16_t norm_sum);

#ifdef __cplusplus
}
#endif

#endif /* __IMAGE_PROC_H */

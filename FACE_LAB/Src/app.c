/**
  ******************************************************************************
  * @file    app.c
  * @brief   实验四 人脸录入与识别 —— 应用层（界面 + 流程 + 状态机）
  *
  *  一、模式与界面
  *      录入模式 : 实时预览 + 取景框，UP 选人、K0 拍照存一张、K1 删掉最后一张
  *      识别模式 : 实时预览 + 取景框，UP 选真值、K1 切算法、K0 拍照并跑全部算法
  *      结果界面 : 抓拍的人脸 + 四种方式的距离/阈值/判定，UP 换"以哪个为准"
  *      统计界面 : 每种方式的 正确通过率 / 误拒率 / 误通过率 / 平均耗时
  *                 （验证集与最终测试集分开计数，UP 切换）
  *      信息界面 : Flash 器件与图库、模型占用、训练耗时、SDRAM 自检
  *      K2 在四种模式间循环；结果界面按 K1 返回预览。
  *
  *  二、为什么用"取景框 + 按键拍照"而不是自动人脸检测
  *      实验要求明确说人脸检测与身份识别是不同问题，本实验用固定取景框，
  *      由人对准后再拍，这样录入与识别用的是完全相同的裁剪方式。
  *
  *  三、非阻塞设计
  *      摄像头一帧搬完 -> 放大显示 -> 立刻启动下一帧；
  *      按键、界面、统计都按时间戳调度，主循环里没有等待
  *      （初始化阶段的延时除外）。
  ******************************************************************************
  */

#include <string.h>

#include "app.h"
#include "config.h"
#include "pinmap.h"

#include "bsp_delay.h"
#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_sdram.h"
#include "bsp_lcd.h"
#include "bsp_dcmi.h"
#include "ov5640.h"
#include "image_proc.h"
#include "face_db.h"
#include "face_algo.h"

/* ==========================================================================
 * 布局与配色
 * ========================================================================== */
#define VIEW_W          640U                              /* 预览区（QVGA 放大 2 倍） */
#define SIDE_X          (VIEW_W + 8U)
#define SIDE_W          (LCD_WIDTH - SIDE_X)
#define TOP_H           56U
#define FOOT_Y          440U

#define COLOR_BG        LCD_NAVY
#define COLOR_TOP       LCD_COLOR(12, 26, 52)
#define COLOR_CARD      LCD_COLOR(28, 44, 68)
#define COLOR_KEY_HL    LCD_COLOR(38, 58, 88)
#define COLOR_EDGE      LCD_COLOR(70, 100, 140)
#define COLOR_LABEL     LCD_COLOR(168, 190, 212)
#define COLOR_VALUE     LCD_WHITE
#define COLOR_UNIT      LCD_COLOR(150, 172, 194)
#define COLOR_OK        LCD_COLOR(70, 220, 120)
#define COLOR_BAD       LCD_COLOR(255, 72, 72)
#define COLOR_WARN      LCD_COLOR(255, 170, 0)
#define COLOR_VF        LCD_COLOR(255, 220, 0)

/* ==========================================================================
 * 状态
 * ========================================================================== */
typedef enum
{
  MODE_ENROLL = 0,
  MODE_RECOG,
  MODE_STATS,
  MODE_INFO,
  MODE_COUNT
} AppMode;

typedef enum
{
  SCREEN_LIVE = 0,
  SCREEN_RESULT,
  SCREEN_STATS,
  SCREEN_INFO
} AppScreen;

typedef enum
{
  CAP_NONE = 0,
  CAP_ENROLL,
  CAP_RECOG
} CaptureReq;

static FaceDb     s_db;
static FaceImage  s_query;                     /* 最近一次抓拍（片内 RAM 6.9KB） */
static FaceResult s_res[ALGO_COUNT];
static uint8_t    s_has_result;

static AppMode    s_mode;
static AppScreen  s_screen;
static FaceAlgo   s_sel = ALGO_LBP;            /* 以哪个方式为判定准 */
static uint8_t    s_person = 1U;
static uint8_t    s_truth  = 1U;               /* 真值：0=非库内，1..5 */
static uint8_t    s_stat_mode = CFG_STAT_VALIDATION;
static CaptureReq s_cap_req = CAP_NONE;

static uint8_t    s_cam_ready;
static uint8_t    s_cam_fail;
static uint8_t    s_cam_streaming;
static uint32_t   s_cam_start_ms;
static uint32_t   s_cam_frames;
static uint32_t   s_t_fps;
static uint32_t   s_fps_frames;
static uint32_t   s_fps_x10;

static uint32_t   s_t_key;
static uint32_t   s_t_ui;

static char       s_msg[48];

static uint8_t    s_sdram_rc;
static uint8_t    s_lcd_rc;
static uint8_t    s_build_fail;
static uint32_t   s_build_ms;
static uint32_t   s_recog_ms;

static void app_take_result(void);
static void ui_live_refresh(void);

/* ==========================================================================
 * 小工具
 * ========================================================================== */
static void msg_set(const char *s)
{
  size_t i = 0U;

  if (s == NULL)
  {
    s_msg[0] = '\0';
    return;
  }
  while ((s[i] != '\0') && (i < (sizeof(s_msg) - 1U)))
  {
    s_msg[i] = s[i];
    i++;
  }
  s_msg[i] = '\0';
}

/* 把消息按"最多 6 个字符"截断（不切断 UTF-8 多字节序列），侧栏放得下 */
static void msg_copy_short(char *out, size_t out_len, const char *in)
{
  size_t i = 0U;
  size_t o = 0U;
  uint8_t chars = 0U;

  if ((out == NULL) || (out_len == 0U) || (in == NULL))
  {
    return;
  }

  while ((in[i] != '\0') && (chars < 6U))
  {
    uint8_t len = LCD_FontUtf8Len((uint8_t)in[i]);

    if ((o + len) >= out_len)
    {
      break;
    }
    for (uint8_t k = 0U; k < len; k++)
    {
      out[o++] = in[i++];
    }
    chars++;
  }
  out[o] = '\0';
}

static const char *mode_name(AppMode m)
{
  switch (m)
  {
    case MODE_ENROLL: return "录入";
    case MODE_RECOG:  return "识别";
    case MODE_STATS:  return "统计";
    default:          return "信息";
  }
}

static const char *quality_text(uint8_t q)
{
  switch (q)
  {
    case IMG_TOO_DARK:     return "太暗，请补光重拍";
    case IMG_TOO_BRIGHT:   return "太亮，请避光重拍";
    case IMG_LOW_CONTRAST: return "未对准，请正对取景框";
    default:               return "合格";
  }
}

/* 把 48x48 人脸按 scale 倍画到 (x,y) */
static void draw_face_scaled(uint16_t x, uint16_t y, uint16_t scale, const FaceImage *img)
{
  if (img == NULL)
  {
    return;
  }
  for (uint16_t r = 0U; r < FACE_H; r++)
  {
    for (uint16_t c = 0U; c < FACE_W; c++)
    {
      BSP_LCD_FillRect((uint16_t)(x + c * scale), (uint16_t)(y + r * scale),
                       scale, scale, img->rgb565[(uint32_t)r * FACE_W + c]);
    }
  }
}

static void led_show_verdict(uint8_t pass)
{
  if (pass != 0U)
  {
    BSP_Led_Green(1U);
    BSP_Led_Red(0U);
  }
  else
  {
    BSP_Led_Green(0U);
    BSP_Led_Red(1U);
  }
}

/* ==========================================================================
 * 摄像头
 * ========================================================================== */
static void cam_start_frame(void)
{
  if (BSP_DCMI_StartFrame((uint16_t *)CAM_FB_ADDR, (320U * 240U * 2U) / 4U) == 0U)
  {
    s_cam_streaming = 1U;
    s_cam_start_ms = HAL_GetTick();
  }
  else
  {
    s_cam_streaming = 0U;
  }
}

static uint8_t cam_init(void)
{
  if (OV5640_Init() != OV5640_OK)
  {
    s_cam_ready = 0U;
    return 1U;
  }
  s_cam_ready = 1U;
  s_cam_fail = 0U;
  cam_start_frame();
  return 0U;
}

/* 取景框：把裁剪区画到 2 倍预览上 */
static void ui_draw_viewfinder(void)
{
  uint16_t x = (uint16_t)(CFG_CROP_X * 2U);
  uint16_t y = (uint16_t)(CFG_CROP_Y * 2U);
  uint16_t w = (uint16_t)(CFG_CROP_W * 2U);
  uint16_t h = (uint16_t)(CFG_CROP_H * 2U);
  uint16_t len = 28U;

  BSP_LCD_DrawHLine(x, y, w, COLOR_VF);
  BSP_LCD_DrawHLine(x, (uint16_t)(y + h - 2U), w, COLOR_VF);
  BSP_LCD_DrawVLine(x, y, h, COLOR_VF);
  BSP_LCD_DrawVLine((uint16_t)(x + w - 2U), y, h, COLOR_VF);

  for (uint16_t c = 0U; c < len; c++)
  {
    BSP_LCD_FillRect((uint16_t)(x + c), y, 2U, 4U, COLOR_VF);
    BSP_LCD_FillRect((uint16_t)(x + c), (uint16_t)(y + h - 4U), 2U, 4U, COLOR_VF);
    BSP_LCD_FillRect(x, (uint16_t)(y + c), 4U, 2U, COLOR_VF);
    BSP_LCD_FillRect((uint16_t)(x + w - 4U), (uint16_t)(y + c), 4U, 2U, COLOR_VF);
  }
}

/* 一帧搬完：显示 + 处理待处理的抓拍请求 */
static void cam_on_frame(uint32_t now)
{
  BSP_LCD_BlitRGB565x2(0U, 0U, 320U, 240U, (const uint16_t *)CAM_FB_ADDR);

  if (s_screen == SCREEN_LIVE)
  {
    ui_draw_viewfinder();
  }

  s_cam_fail = 0U;
  s_cam_frames++;
  s_fps_frames++;

  if ((now - s_t_fps) >= 1000U)
  {
    s_fps_x10 = s_fps_frames * 10U;
    s_fps_frames = 0U;
    s_t_fps = now;
  }

  if (s_cap_req != CAP_NONE)
  {
    CaptureReq req = s_cap_req;
    s_cap_req = CAP_NONE;

    if (Image_CropToFace((const uint16_t *)CAM_FB_ADDR, 320U, 240U,
                         CFG_CROP_X, CFG_CROP_Y, CFG_CROP_W, CFG_CROP_H, &s_query) != 0U)
    {
      msg_set("裁剪失败：取景框超出画面");
      ui_live_refresh();
      return;
    }

    if (s_query.quality != IMG_OK)
    {
      msg_set(quality_text(s_query.quality));
      ui_live_refresh();
      return;                       /* 画质不合格：不存、不识别，提示重拍 */
    }

    if (req == CAP_ENROLL)
    {
      uint32_t t0 = HAL_GetTick();
      uint8_t  rc = FaceDb_Add(&s_db, s_person, &s_query);

      if (rc == 0U)
      {
        s_build_fail = FaceAlgo_Build();        /* 图库变了 -> 重建模型 */
        s_build_ms = HAL_GetTick() - t0;
        msg_set((s_db.flash_ok != 0U) ? "已写入Flash并可断电保持" : "已存内存(Flash不可用)");
      }
      else if (rc == 2U)
      {
        msg_set("该人员已满6张，先删再拍");
      }
      else if (rc == 5U)
      {
        msg_set("Flash写入失败，请重试");
      }
      else
      {
        msg_set("保存失败");
      }
      ui_live_refresh();
    }
    else
    {
      app_take_result();
    }
  }
}

static void cam_task(uint32_t now)
{
  if (s_cam_ready == 0U)
  {
    return;
  }

  if (BSP_DCMI_TakeFrame() != 0U)
  {
    cam_on_frame(now);
    cam_start_frame();
    return;
  }

  if ((s_cam_streaming != 0U) && ((now - s_cam_start_ms) > CFG_CAM_TIMEOUT_MS))
  {
    s_cam_fail++;
    if (s_cam_fail >= CFG_CAM_FAIL_LIMIT)
    {
      BSP_DCMI_Stop();
      s_cam_ready = 0U;
      msg_set("摄像头无信号，按K0重试");
      if (s_screen == SCREEN_LIVE)
      {
        ui_live_refresh();
      }
    }
    else
    {
      cam_start_frame();
    }
  }
}

/* ==========================================================================
 * 识别 + 统计
 * ========================================================================== */
static void app_take_result(void)
{
  uint32_t t0 = HAL_GetTick();

  memset(s_res, 0, sizeof(s_res));
  for (uint8_t a = 0U; a < ALGO_COUNT; a++)
  {
    (void)FaceAlgo_Recognize((FaceAlgo)a, &s_query, &s_res[a]);
    FaceAlgo_StatsUpdate(s_stat_mode, (FaceAlgo)a, s_truth, &s_res[a]);
  }
  s_has_result = 1U;
  s_recog_ms = HAL_GetTick() - t0;

  /* 只有"验证集"的抓拍才进阈值标定缓存。
     最终测试集只能用一次、不得据此反复调参，所以刻意不入缓存。 */
  if (s_stat_mode == CFG_STAT_VALIDATION)
  {
    (void)FaceAlgo_ValAdd(s_truth, &s_query);
  }

  /* 绿/红灯跟随"当前选中方式"的判定 */
  if (s_res[s_sel].ok != 0U)
  {
    led_show_verdict(s_res[s_sel].pass);
  }

  s_screen = SCREEN_RESULT;
}

/* ==========================================================================
 * 实时预览界面
 * ========================================================================== */
static void ui_live_refresh(void)
{
  uint16_t x = SIDE_X + 6U;
  uint16_t y;
  char     tmp[20];

  BSP_LCD_FillRect(SIDE_X, 0U, SIDE_W, LCD_HEIGHT, COLOR_TOP);

  BSP_LCD_DrawString(x, 8U, "模式", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString((uint16_t)(x + 60U), 8U, mode_name(s_mode), COLOR_VALUE, COLOR_TOP, LCD_FONT_SMALL);

  y = 44U;
  if (s_mode == MODE_ENROLL)
  {
    BSP_LCD_DrawString(x, y, "人员", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    (void)BSP_LCD_DrawInt((uint16_t)(x + 60U), y, (int32_t)s_person, COLOR_VALUE, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString(x, (uint16_t)(y + 32U), "已录", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    (void)BSP_LCD_DrawInt((uint16_t)(x + 60U), (uint16_t)(y + 32U),
                          (int32_t)FaceDb_CountOf(&s_db, s_person), COLOR_OK, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 84U), (uint16_t)(y + 32U), "/6", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y, "真值", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    if (s_truth == 0U)
    {
      BSP_LCD_DrawString((uint16_t)(x + 60U), y, "非库内", COLOR_WARN, COLOR_TOP, LCD_FONT_SMALL);
    }
    else
    {
      (void)BSP_LCD_DrawInt((uint16_t)(x + 60U), y, (int32_t)s_truth, COLOR_VALUE, COLOR_TOP, LCD_FONT_SMALL);
      BSP_LCD_DrawString((uint16_t)(x + 84U), y, "号", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
    }
    BSP_LCD_DrawString(x, (uint16_t)(y + 32U), "算法", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 60U), (uint16_t)(y + 32U),
                       FaceAlgo_Name(s_sel), COLOR_VALUE, COLOR_TOP, LCD_FONT_SMALL);
  }

  y = 124U;
  BSP_LCD_DrawString(x, y, "图库", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt((uint16_t)(x + 60U), y, (int32_t)FaceDb_Total(&s_db), COLOR_VALUE, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString((uint16_t)(x + 84U), y, "张", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);

  y = 156U;
  BSP_LCD_DrawString(x, y, "阈值", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawFixed2((uint16_t)(x + 60U), y, FaceAlgo_Threshold(s_sel),
                           COLOR_VALUE, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(x, (uint16_t)(y + 26U),
                     FaceAlgo_IsCalibrated(s_sel) ? "已标定" : "未标定",
                     FaceAlgo_IsCalibrated(s_sel) ? COLOR_OK : COLOR_WARN,
                     COLOR_TOP, LCD_FONT_SMALL);

  y = 216U;
  BSP_LCD_FillRect(x, y, 96U, 96U, COLOR_CARD);
  if (s_has_result != 0U)
  {
    draw_face_scaled(x, y, 2U, &s_query);
  }
  else
  {
    BSP_LCD_DrawString(x, (uint16_t)(y + 36U), "未抓拍", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
  }

  y = 324U;
  if (s_cam_ready != 0U)
  {
    BSP_LCD_DrawString(x, y, "预览", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    (void)BSP_LCD_DrawInt((uint16_t)(x + 60U), y, (int32_t)(s_fps_x10 / 10U), COLOR_OK, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 84U), y, "fps", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y, "无信号", COLOR_BAD, COLOR_TOP, LCD_FONT_SMALL);
  }

  y = 350U;
  if (s_db.flash_ok != 0U)
  {
    BSP_LCD_DrawString(x, y, "Flash", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 60U), y, "OK", COLOR_OK, COLOR_TOP, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y, "仅内存", COLOR_WARN, COLOR_TOP, LCD_FONT_SMALL);
  }

  y = 378U;
  BSP_LCD_FillRect(x, y, SIDE_W - 12U, 26U, COLOR_KEY_HL);
  msg_copy_short(tmp, sizeof(tmp), s_msg);
  if (tmp[0] != '\0')
  {
    BSP_LCD_DrawString(x, (uint16_t)(y + 2U), tmp, COLOR_VALUE, COLOR_KEY_HL, LCD_FONT_SMALL);
  }

  y = 412U;
  if (s_mode == MODE_ENROLL)
  {
    BSP_LCD_DrawString(x, y, "K0存一张 K1删", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString(x, (uint16_t)(y + 22U), "UP选人 K2换模式", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y, "K0拍照识别", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
    BSP_LCD_DrawString(x, (uint16_t)(y + 22U), "K1切算法 UP真值", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  }
}

/* ==========================================================================
 * 结果界面
 * ========================================================================== */
static void ui_result_screen(void)
{
  uint16_t y = 144U;

  BSP_LCD_Clear(COLOR_BG);
  BSP_LCD_FillRect(0U, 0U, LCD_WIDTH, TOP_H, COLOR_TOP);
  BSP_LCD_DrawStringCentered(12U, "识 别 结 果", COLOR_VALUE, COLOR_TOP, LCD_FONT_LARGE);

  BSP_LCD_DrawString(24U, 66U, "真值", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  if (s_truth == 0U)
  {
    BSP_LCD_DrawString(96U, 66U, "非库内人员", COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);
  }
  else
  {
    (void)BSP_LCD_DrawInt(96U, 66U, (int32_t)s_truth, COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
    BSP_LCD_DrawString(120U, 66U, "号", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  }
  BSP_LCD_DrawString(400U, 66U, "本次识别总耗时", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(600U, 66U, (int32_t)s_recog_ms, COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(640U, 66U, "ms（四算法合计）", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);

  draw_face_scaled(32U, 110U, 4U, &s_query);
  BSP_LCD_DrawRect(31U, 109U, (uint16_t)(FACE_W * 4U + 2U), (uint16_t)(FACE_H * 4U + 2U), COLOR_EDGE);
  BSP_LCD_DrawString(32U, 312U, "画质", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(104U, 312U, quality_text(s_query.quality), COLOR_OK, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(32U, 340U, "亮度", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(104U, 340U, (int32_t)Image_MeanBrightness(s_query.gray),
                        COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);

  BSP_LCD_DrawString(272U, 110U, "方式", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(400U, 110U, "距离", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(520U, 110U, "阈值", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(624U, 110U, "判定", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(712U, 110U, "预测", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);

  for (uint8_t a = 0U; a < ALGO_COUNT; a++)
  {
    uint16_t bg = (a == (uint8_t)s_sel) ? COLOR_CARD : COLOR_BG;

    BSP_LCD_FillRect(264U, y, 520U, 34U, bg);
    BSP_LCD_DrawString(272U, (uint16_t)(y + 6U), FaceAlgo_Name((FaceAlgo)a),
                       (a == (uint8_t)s_sel) ? COLOR_VF : COLOR_LABEL, bg, LCD_FONT_SMALL);

    if (s_res[a].ok != 0U)
    {
      (void)BSP_LCD_DrawFixed2(384U, (uint16_t)(y + 6U), s_res[a].score, COLOR_VALUE, bg, LCD_FONT_SMALL);
      (void)BSP_LCD_DrawFixed2(512U, (uint16_t)(y + 6U), s_res[a].threshold, COLOR_UNIT, bg, LCD_FONT_SMALL);
      BSP_LCD_DrawString(624U, (uint16_t)(y + 6U),
                         (s_res[a].pass != 0U) ? "通过" : "不通过",
                         (s_res[a].pass != 0U) ? COLOR_OK : COLOR_BAD, bg, LCD_FONT_SMALL);
      if (s_res[a].person == 0U)
      {
        BSP_LCD_DrawString(712U, (uint16_t)(y + 6U), "--", COLOR_UNIT, bg, LCD_FONT_SMALL);
      }
      else
      {
        (void)BSP_LCD_DrawInt(712U, (uint16_t)(y + 6U), (int32_t)s_res[a].person, COLOR_VALUE, bg, LCD_FONT_SMALL);
      }
    }
    else
    {
      BSP_LCD_DrawString(384U, (uint16_t)(y + 6U), "不可用（样本不足）", COLOR_WARN, bg, LCD_FONT_SMALL);
    }
    y = (uint16_t)(y + 38U);
  }

  {
    uint16_t    box_y = 320U;
    const char *verdict;
    uint16_t    color;

    if (s_res[s_sel].ok == 0U)
    {
      verdict = "无模型";
      color = COLOR_WARN;
    }
    else if (s_res[s_sel].pass != 0U)
    {
      verdict = "通过";
      color = COLOR_OK;
    }
    else
    {
      verdict = "不通过";
      color = COLOR_BAD;
    }

    BSP_LCD_FillRect(272U, box_y, 512U, 52U, COLOR_TOP);
    BSP_LCD_DrawString(288U, (uint16_t)(box_y + 8U), verdict, color, COLOR_TOP, LCD_FONT_LARGE);
    BSP_LCD_DrawString(432U, (uint16_t)(box_y + 18U), FaceAlgo_CnName(s_sel), COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);

    if ((s_res[s_sel].ok != 0U) && (s_res[s_sel].pass != 0U) && (s_res[s_sel].person != 0U))
    {
      (void)BSP_LCD_DrawInt(600U, (uint16_t)(box_y + 8U), (int32_t)s_res[s_sel].person, color, COLOR_TOP, LCD_FONT_LARGE);
      BSP_LCD_DrawString(632U, (uint16_t)(box_y + 22U), "号", color, COLOR_TOP, LCD_FONT_SMALL);
    }
    else if (s_res[s_sel].ok == 0U)
    {
      BSP_LCD_DrawString(600U, (uint16_t)(box_y + 18U), "样本不足", COLOR_WARN, COLOR_TOP, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawString(600U, (uint16_t)(box_y + 18U), "距离超阈值", COLOR_BAD, COLOR_TOP, LCD_FONT_SMALL);
    }
  }

  BSP_LCD_DrawString(272U, 384U,
                     FaceAlgo_IsCalibrated(s_sel) ? "阈值已用验证集标定，判定可信" : "阈值尚未标定，仅作参考（去统计界面按K0标定）",
                     FaceAlgo_IsCalibrated(s_sel) ? COLOR_OK : COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);

  BSP_LCD_FillRect(0U, FOOT_Y, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - FOOT_Y), COLOR_TOP);
  BSP_LCD_DrawString(24U, (uint16_t)(FOOT_Y + 8U),
                     "UP换方式重判  K0再拍一张  K1回预览",
                     COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
}

/* ==========================================================================
 * 统计界面
 * ========================================================================== */
static void draw_rate(uint16_t x, uint16_t y, uint16_t num, uint16_t den,
                      uint16_t fg, uint16_t bg)
{
  if (den == 0U)
  {
    BSP_LCD_DrawString(x, y, "--", COLOR_UNIT, bg, LCD_FONT_SMALL);
    return;
  }
  (void)BSP_LCD_DrawFixed2(x, y, ((float)num * 100.0f) / (float)den, fg, bg, LCD_FONT_SMALL);
  BSP_LCD_DrawString((uint16_t)(x + 54U), y, "%", COLOR_UNIT, bg, LCD_FONT_SMALL);
}

static void ui_stats_screen(void)
{
  uint16_t y = 136U;

  BSP_LCD_Clear(COLOR_BG);
  BSP_LCD_FillRect(0U, 0U, LCD_WIDTH, TOP_H, COLOR_TOP);
  BSP_LCD_DrawStringCentered(12U, "统 计 与 阈 值", COLOR_VALUE, COLOR_TOP, LCD_FONT_LARGE);

  BSP_LCD_DrawString(24U, 76U, "数据集", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(120U, 76U,
                     (s_stat_mode == CFG_STAT_VALIDATION) ? "验证集（用来标定阈值）" : "最终测试集（只用一次）",
                     (s_stat_mode == CFG_STAT_VALIDATION) ? COLOR_OK : COLOR_WARN,
                     COLOR_BG, LCD_FONT_SMALL);

  BSP_LCD_DrawString(24U, 112U, "方式", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(140U, 112U, "库内次数", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(272U, 112U, "正确通过率", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(424U, 112U, "误拒率", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(536U, 112U, "库外次数", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(664U, 112U, "误通过率", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);

  for (uint8_t a = 0U; a < ALGO_COUNT; a++)
  {
    const AlgoStats *st = FaceAlgo_StatsGet(s_stat_mode, (FaceAlgo)a);
    uint8_t  sel = (a == (uint8_t)s_sel) ? 1U : 0U;
    uint16_t bg = (sel != 0U) ? COLOR_CARD : COLOR_BG;

    BSP_LCD_FillRect(16U, y, 768U, 32U, bg);
    BSP_LCD_DrawString(24U, (uint16_t)(y + 4U), FaceAlgo_Name((FaceAlgo)a),
                       (sel != 0U) ? COLOR_VF : COLOR_LABEL, bg, LCD_FONT_SMALL);
    if (st != NULL)
    {
      (void)BSP_LCD_DrawInt(140U, (uint16_t)(y + 4U), (int32_t)st->enrolled_total, COLOR_VALUE, bg, LCD_FONT_SMALL);
      draw_rate(272U, (uint16_t)(y + 4U), st->enrolled_pass, st->enrolled_total, COLOR_OK, bg);
      draw_rate(424U, (uint16_t)(y + 4U), st->enrolled_reject, st->enrolled_total, COLOR_BAD, bg);
      (void)BSP_LCD_DrawInt(536U, (uint16_t)(y + 4U), (int32_t)st->stranger_total, COLOR_VALUE, bg, LCD_FONT_SMALL);
      draw_rate(664U, (uint16_t)(y + 4U), st->stranger_pass, st->stranger_total, COLOR_BAD, bg);
    }
    y = (uint16_t)(y + 38U);
  }

  BSP_LCD_DrawString(24U, 296U, "当前方式", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(140U, 296U, FaceAlgo_Name(s_sel), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(200U, 296U, "阈值", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawFixed2(272U, 296U, FaceAlgo_Threshold(s_sel), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(380U, 296U, FaceAlgo_IsCalibrated(s_sel) ? "已标定" : "未标定",
                     FaceAlgo_IsCalibrated(s_sel) ? COLOR_OK : COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);

  BSP_LCD_DrawString(24U, 328U, "验证样本缓存", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(200U, 328U, (int32_t)FaceAlgo_ValCount(), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(226U, 328U, "/12", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(300U, 328U, "验证集每次抓拍都会进缓存，测试集不入", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);

  BSP_LCD_DrawString(24U, 360U, "模型占用", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  for (uint8_t a = 0U; a < ALGO_COUNT; a++)
  {
    uint16_t x = (uint16_t)(140U + a * 165U);
    BSP_LCD_DrawString(x, 360U, FaceAlgo_Name((FaceAlgo)a), COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    (void)BSP_LCD_DrawInt((uint16_t)(x + 60U), 360U, (int32_t)(FaceAlgo_Bytes((FaceAlgo)a) / 1024U),
                          COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 102U), 360U, "KB", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  }

  BSP_LCD_DrawString(24U, 392U, "单次识别平均耗时见结果界面；MSE 直接复用图库灰度图，不额外占内存",
                     COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);

  BSP_LCD_FillRect(0U, FOOT_Y, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - FOOT_Y), COLOR_TOP);
  BSP_LCD_DrawString(24U, (uint16_t)(FOOT_Y + 8U),
                     "K0标定阈值  K1清空统计  UP切数据集  K2换模式",
                     COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
}

/* ==========================================================================
 * 信息界面
 * ========================================================================== */
static void ui_info_screen(void)
{
  uint16_t y = 74U;

  BSP_LCD_Clear(COLOR_BG);
  BSP_LCD_FillRect(0U, 0U, LCD_WIDTH, TOP_H, COLOR_TOP);
  BSP_LCD_DrawStringCentered(12U, "系 统 信 息", COLOR_VALUE, COLOR_TOP, LCD_FONT_LARGE);

  BSP_LCD_DrawString(24U, y, "SDRAM 自检", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(200U, y, (s_sdram_rc == 0U) ? "通过" : "失败",
                     (s_sdram_rc == 0U) ? COLOR_OK : COLOR_BAD, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(300U, y, "SDRAM 刷新计数", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(500U, y, (int32_t)BSP_SDRAM_RefreshCount(), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  y = (uint16_t)(y + 32U);

  BSP_LCD_DrawString(24U, y, "LCD 初始化", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(200U, y, (s_lcd_rc == 0U) ? "通过" : "失败",
                     (s_lcd_rc == 0U) ? COLOR_OK : COLOR_BAD, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(300U, y, "累计采集帧数", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(500U, y, (int32_t)s_cam_frames, COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  y = (uint16_t)(y + 32U);

  BSP_LCD_DrawString(24U, y, "NOR Flash", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  {
    char hex[12];
    const char *digits = "0123456789ABCDEF";
    uint32_t id = s_db.flash_id;

    hex[0] = '0'; hex[1] = 'x';
    for (uint8_t i = 0U; i < 6U; i++)
    {
      hex[2U + i] = digits[(id >> ((5U - i) * 4U)) & 0x0FU];
    }
    hex[8] = '\0';
    BSP_LCD_DrawString(200U, y, "JEDEC ID", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    BSP_LCD_DrawString(320U, y, hex, COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  }
  BSP_LCD_DrawString(440U, y, "容量", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(520U, y, (int32_t)(s_db.flash_capacity / (1024U * 1024U)), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(580U, y, "MB", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  y = (uint16_t)(y + 32U);

  BSP_LCD_DrawString(24U, y, "每人已录", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  for (uint8_t p = 0U; p < CFG_PERSON_COUNT; p++)
  {
    uint16_t x = (uint16_t)(160U + p * 120U);
    (void)BSP_LCD_DrawInt(x, y, (int32_t)(p + 1U), COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 24U), y, "号", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    (void)BSP_LCD_DrawInt((uint16_t)(x + 50U), y, (int32_t)s_db.person_count[p], COLOR_OK, COLOR_BG, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 74U), y, "/6", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  }
  y = (uint16_t)(y + 32U);

  BSP_LCD_DrawString(24U, y, "训练样本", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(200U, y, (int32_t)FaceAlgo_SampleCount(), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(240U, y, "PCA 主成分 k", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(430U, y, (int32_t)CFG_PCA_K, COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(470U, y, "PCA 训练耗时", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(640U, y, (int32_t)FaceAlgo_TrainMs(), COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(690U, y, "ms", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  y = (uint16_t)(y + 32U);

  BSP_LCD_DrawString(24U, y, "模型状态", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  for (uint8_t a = 0U; a < ALGO_COUNT; a++)
  {
    uint16_t x = (uint16_t)(160U + a * 160U);
    BSP_LCD_DrawString(x, y, FaceAlgo_Name((FaceAlgo)a), COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 60U), y,
                       (FaceAlgo_IsReady((FaceAlgo)a) != 0U) ? "就绪" : "不可用",
                       (FaceAlgo_IsReady((FaceAlgo)a) != 0U) ? COLOR_OK : COLOR_WARN,
                       COLOR_BG, LCD_FONT_SMALL);
  }
  y = (uint16_t)(y + 32U);

  BSP_LCD_DrawString(24U, y, "上次建模耗时", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  (void)BSP_LCD_DrawInt(240U, y, (int32_t)s_build_ms, COLOR_VALUE, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(290U, y, "ms", COLOR_UNIT, COLOR_BG, LCD_FONT_SMALL);
  if (s_build_fail != 0U)
  {
    BSP_LCD_DrawString(360U, y, "建模失败位", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    (void)BSP_LCD_DrawInt(490U, y, (int32_t)s_build_fail, COLOR_BAD, COLOR_BG, LCD_FONT_SMALL);
  }
  y = (uint16_t)(y + 32U);

  if (s_db.flash_ok != 0U)
  {
    BSP_LCD_DrawString(24U, y, "断电保持已开启：图库写在 NOR Flash 的 0x01000000 起的独立区域，不覆盖原有数据",
                       COLOR_OK, COLOR_BG, LCD_FONT_SMALL);
  }
  else if (s_db.flash_id_ok != 0U)
  {
    BSP_LCD_DrawString(24U, y, "Flash 容量不足 17MB，已退化为仅内存图库（重新上电需重新录入）",
                       COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(24U, y, "NOR Flash 不可用，已退化为仅内存图库（重新上电需重新录入）",
                       COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);
  }

  BSP_LCD_FillRect(0U, FOOT_Y, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - FOOT_Y), COLOR_TOP);
  BSP_LCD_DrawString(24U, (uint16_t)(FOOT_Y + 8U),
                     "UP清空验证缓存  K0清空图库  K1重建模型  K2换模式",
                     COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
}

/* ==========================================================================
 * 界面切换与按键
 * ========================================================================== */
static void screen_enter(AppScreen sc)
{
  s_screen = sc;

  if (sc == SCREEN_LIVE)
  {
    ui_live_refresh();
    if (s_cam_ready != 0U)
    {
      cam_start_frame();
    }
  }
  else
  {
    BSP_DCMI_Stop();
    s_cam_streaming = 0U;
    if (sc == SCREEN_RESULT)
    {
      ui_result_screen();
    }
    else if (sc == SCREEN_STATS)
    {
      ui_stats_screen();
    }
    else
    {
      ui_info_screen();
    }
  }
}

static void app_enter_mode(AppMode m)
{
  s_mode = m;
  if (m == MODE_STATS)
  {
    screen_enter(SCREEN_STATS);
  }
  else if (m == MODE_INFO)
  {
    screen_enter(SCREEN_INFO);
  }
  else
  {
    screen_enter(SCREEN_LIVE);
  }
}

static void key_task(uint32_t now)
{
  KeyEvent ev = BSP_Key_Scan(now);

  s_t_key = now;
  if (ev == KEY_EVENT_NONE)
  {
    return;
  }

  /* ---- 结果界面 ---- */
  if (s_screen == SCREEN_RESULT)
  {
    if ((ev == KEY_EVENT_UP) || (ev == KEY_EVENT_K2))
    {
      s_sel = (FaceAlgo)(((uint8_t)s_sel + 1U) % (uint8_t)ALGO_COUNT);
      if (s_res[s_sel].ok != 0U)
      {
        led_show_verdict(s_res[s_sel].pass);
      }
      ui_result_screen();
      return;
    }
    if ((ev == KEY_EVENT_K1) || (ev == KEY_EVENT_K0))
    {
      screen_enter(SCREEN_LIVE);
      if (ev == KEY_EVENT_K0)
      {
        s_cap_req = CAP_RECOG;
        msg_set("正在重新抓拍");
        ui_live_refresh();
      }
      return;
    }
    return;
  }

  /* ---- 全局：K2 换模式 ---- */
  if (ev == KEY_EVENT_K2)
  {
    app_enter_mode((AppMode)(((uint8_t)s_mode + 1U) % (uint8_t)MODE_COUNT));
    return;
  }

  /* ---- 统计界面 ---- */
  if (s_screen == SCREEN_STATS)
  {
    if (ev == KEY_EVENT_UP)
    {
      s_stat_mode = (uint8_t)((s_stat_mode == CFG_STAT_VALIDATION) ? CFG_STAT_TEST : CFG_STAT_VALIDATION);
      ui_stats_screen();
    }
    else if (ev == KEY_EVENT_K0)
    {
      float   t = 0.0f;
      uint8_t rc = FaceAlgo_Calibrate(s_sel, &t);

      if (rc == 0U)
      {
        msg_set("阈值已标定");
      }
      else if (rc == 3U)
      {
        msg_set("验证样本需含库内与库外");
      }
      else
      {
        msg_set("标定失败：样本不足");
      }
      ui_stats_screen();
    }
    else if (ev == KEY_EVENT_K1)
    {
      FaceAlgo_StatsReset(s_stat_mode);
      ui_stats_screen();
    }
    return;
  }

  /* ---- 信息界面 ---- */
  if (s_screen == SCREEN_INFO)
  {
    if (ev == KEY_EVENT_UP)
    {
      FaceAlgo_ValClear();
      msg_set("验证缓存已清空");
      ui_info_screen();
    }
    else if (ev == KEY_EVENT_K0)
    {
      (void)FaceDb_EraseAll(&s_db);
      s_build_fail = FaceAlgo_Build();
      s_has_result = 0U;
      msg_set("图库已清空");
      ui_info_screen();
    }
    else if (ev == KEY_EVENT_K1)
    {
      s_build_fail = FaceAlgo_Build();
      msg_set("模型已重建");
      ui_info_screen();
    }
    return;
  }

  /* ---- 实时预览（录入 / 识别） ---- */
  if (ev == KEY_EVENT_UP)
  {
    if (s_mode == MODE_ENROLL)
    {
      s_person = (uint8_t)((s_person % CFG_PERSON_COUNT) + 1U);
    }
    else
    {
      s_truth = (uint8_t)((s_truth + 1U) % (CFG_PERSON_COUNT + 1U));
    }
    ui_live_refresh();
  }
  else if (ev == KEY_EVENT_K1)
  {
    if (s_mode == MODE_ENROLL)
    {
      uint8_t rc = FaceDb_DeleteLast(&s_db, s_person);

      msg_set((rc == 0U) ? "已删除最后一张" : "该人员无可删记录");
      if (rc == 0U)
      {
        s_build_fail = FaceAlgo_Build();
      }
    }
    else
    {
      s_sel = (FaceAlgo)(((uint8_t)s_sel + 1U) % (uint8_t)ALGO_COUNT);
      msg_set("已切换识别方式");
    }
    ui_live_refresh();
  }
  else if (ev == KEY_EVENT_K0)
  {
    if (s_cam_ready == 0U)
    {
      msg_set((cam_init() == 0U) ? "摄像头已重新初始化" : "摄像头初始化失败");
    }
    else
    {
      s_cap_req = (s_mode == MODE_ENROLL) ? CAP_ENROLL : CAP_RECOG;
      msg_set("正在抓拍");
    }
    ui_live_refresh();
  }
}

/* ==========================================================================
 * 初始化与主循环
 * ========================================================================== */
void App_Init(void)
{
  memset(&s_db, 0, sizeof(s_db));
  memset(&s_res, 0, sizeof(s_res));
  s_msg[0] = '\0';
  s_has_result = 0U;
  s_cam_ready = 0U;
  s_cam_fail = 0U;
  s_cam_streaming = 0U;
  s_cam_frames = 0U;
  s_fps_frames = 0U;
  s_fps_x10 = 0U;
  s_build_fail = 0U;
  s_build_ms = 0U;
  s_recog_ms = 0U;

  BSP_Led_Init();
  BSP_Led_Green(0U);
  BSP_Led_Red(0U);
  BSP_DelayInit();
  BSP_Key_Init();

  /* SDRAM：帧缓冲、图库、特征、PCA 模型都在它上面 */
  s_sdram_rc = BSP_SDRAM_Init();
  if (s_sdram_rc == 0U)
  {
    s_sdram_rc = BSP_SDRAM_Test();
  }

  s_lcd_rc = BSP_LCD_Init();

  BSP_LCD_Clear(COLOR_BG);
  BSP_LCD_DrawStringCentered(170U, EXP_TITLE, COLOR_VALUE, COLOR_BG, LCD_FONT_LARGE);
  BSP_LCD_DrawStringCentered(220U, EXP_SUBTITLE, COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);

  if (s_lcd_rc != 0U)
  {
    Error_Handler();
  }
  if (s_sdram_rc != 0U)
  {
    BSP_LCD_DrawStringCentered(270U, "SDRAM 自检失败，请检查 FMC", COLOR_BAD, COLOR_BG, LCD_FONT_SMALL);
    Error_Handler();
  }

  /* 图库：核对 Flash 器件 ID，并读回上次录入的人脸 */
  {
    uint8_t rc = FaceDb_Init(&s_db);

    if (rc == 0U)
    {
      BSP_LCD_DrawStringCentered(270U,
                                 (FaceDb_Total(&s_db) > 0U) ? "Flash 正常，已载入图库" : "Flash 正常，图库为空",
                                 COLOR_OK, COLOR_BG, LCD_FONT_SMALL);
    }
    else if (rc == 2U)
    {
      BSP_LCD_DrawStringCentered(270U, "NOR Flash 无应答：退化为仅内存图库", COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);
    }
    else if (rc == 3U)
    {
      BSP_LCD_DrawStringCentered(270U, "Flash 器件 ID 不符：已禁用持久化", COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawStringCentered(270U, "Flash 容量不足 17MB：已禁用持久化", COLOR_WARN, COLOR_BG, LCD_FONT_SMALL);
    }
  }

  BSP_DelayMs(150U);

  /* DCMI + 摄像头 */
  if (BSP_DCMI_Init() != 0U)
  {
    BSP_LCD_DrawStringCentered(300U, "DCMI 初始化失败", COLOR_BAD, COLOR_BG, LCD_FONT_SMALL);
  }
  else
  {
    (void)cam_init();
  }

  /* 图库已有样本就直接建模，省得每次上电都手动重建 */
  FaceAlgo_Bind(&s_db);
  if (FaceDb_Total(&s_db) > 0U)
  {
    s_build_fail = FaceAlgo_Build();
  }

  BSP_DelayMs(300U);

  app_enter_mode(MODE_ENROLL);
  msg_set("K0拍照录入 K2换模式");
  ui_live_refresh();

  {
    uint32_t now = HAL_GetTick();

    s_t_key = now;
    s_t_ui = now;
    s_t_fps = now;
  }
}

void App_Task(void)
{
  uint32_t now = HAL_GetTick();

  cam_task(now);

  if ((now - s_t_key) >= CFG_KEY_PERIOD_MS)
  {
    key_task(now);
  }

  if ((now - s_t_ui) >= CFG_UI_PERIOD_MS)
  {
    s_t_ui = now;
    if (s_screen == SCREEN_LIVE)
    {
      ui_live_refresh();          /* 侧栏数值每 200ms 重画一次，内容相同处不闪 */
    }
  }
}

/**
  ******************************************************************************
  * @file    app.c
  * @brief   智能家居中控终端：主页 / 摄像头预览页 + 阈值预警状态机
  *
  *  一、任务划分（全部非阻塞，主循环按时间戳调度）
  *      DHT11 采集   2.0s 一次   —— 温度、湿度、校验
  *      ADC   采集   200ms 一次  —— RV1 电压（16 次平均）
  *      按键扫描     10ms 一次   —— 20ms 消抖，只在按下边沿产生事件
  *      界面刷新     200ms 一次  —— 只重画变化的数值矩形（局部刷新）
  *      摄像头       事件驱动    —— DCMI+DMA 完成一帧就放大显示并取下一帧
  *      报警灯       250ms 翻转  —— 越限时红灯 2Hz 闪烁
  *
  *  二、预警状态机（三种状态，故障优先）
  *      NORMAL 绿常亮 红灭    三项数据都有效且都没越限
  *      WARN   绿灭   红 2Hz  任意一项越上限（带回差，见 config.h）
  *      FAULT  绿灭   红常亮  数据无效 / 传感器失联 / 采集超时
  *
  *  三、局部刷新策略
  *      静态部分（标题、卡片框、标签、阈值文字）只在进入页面时画一次；
  *      动态部分（数值、状态字）先记住"上次画的是什么"，只有变了才重画，
  *      重画时先清掉那一小块矩形再写新值，所以既不会闪、也不会留残影。
  ******************************************************************************
  */

#include "app.h"
#include "config.h"
#include "pinmap.h"

#include "bsp_delay.h"
#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_sdram.h"
#include "bsp_lcd.h"
#include "bsp_adc.h"
#include "dht11.h"
#include "ov5640.h"
#include "bsp_dcmi.h"

/* ==========================================================================
 * 颜色与布局
 * ========================================================================== */
#define COLOR_BG        LCD_NAVY
#define COLOR_TOP       LCD_COLOR(12, 26, 52)
#define COLOR_CARD      LCD_COLOR(28, 44, 68)
#define COLOR_CARD_EDGE LCD_COLOR(70, 100, 140)
#define COLOR_LABEL     LCD_COLOR(168, 190, 212)
#define COLOR_VALUE     LCD_WHITE
#define COLOR_WHITE     LCD_WHITE
#define COLOR_UNIT      LCD_COLOR(150, 172, 194)
#define COLOR_OK        LCD_COLOR(70, 220, 120)
#define COLOR_WARN      LCD_COLOR(255, 170, 0)
#define COLOR_FAULT     LCD_COLOR(255, 72, 72)

#define UI_TOP_H        64U
#define CARD_X          24U
#define CARD_W          360U
#define CARD_H          92U
#define CARD_Y1         124U
#define CARD_Y2         228U
#define CARD_Y3         332U

#define PANEL_X         408U
#define PANEL_W         368U
#define PANEL_Y1        124U
#define PANEL_H1        180U
#define PANEL_Y2        316U
#define PANEL_H2        116U

#define FOOT_Y          444U

/* 摄像头页：左边 640x480 预览（QVGA 放大 2 倍），右边状态栏 */
#define CAM_VIEW_W      (OV5640_WIDTH * 2U)
#define CAM_SIDE_X      (CAM_VIEW_W + 8U)
#define CAM_SIDE_W      (LCD_WIDTH - CAM_SIDE_X)

#define CAM_BUF_WORDS   ((uint32_t)(OV5640_WIDTH * OV5640_HEIGHT * 2U) / 4U)

/* ==========================================================================
 * 状态
 * ========================================================================== */
typedef enum
{
  ALARM_NORMAL = 0,
  ALARM_WARN,
  ALARM_FAULT
} AlarmState;

typedef enum
{
  PAGE_HOME = 0,
  PAGE_CAMERA
} PageId;

typedef struct
{
  /* DHT11 */
  DHT11_Data   dht;
  uint8_t      dht_valid;
  uint8_t      dht_fail_count;
  DHT11_Status dht_status;

  /* 电压表 */
  uint16_t     adc_raw;
  float        volt;
  uint8_t      adc_valid;

  /* 报警 */
  AlarmState   alarm;
  uint8_t      temp_over;
  uint8_t      humi_over;
  uint8_t      volt_over;

  /* 摄像头 */
  uint8_t      cam_ready;          /* OV5640 初始化成功 */
  uint8_t      cam_fail_count;
  uint8_t      cam_streaming;
  uint32_t     cam_last_start_ms;
  uint32_t     cam_fps;            /* 最近一次的近似帧率 x10 */

  /* 页面与调度 */
  PageId       page;
  uint8_t      startup;
  uint32_t     t_dht;
  uint32_t     t_adc;
  uint32_t     t_key;
  uint32_t     t_ui;
  uint32_t     t_led;
  uint32_t     led_phase;
  uint32_t     t_fps;
  uint32_t     fps_frames;
} AppState;

static AppState s_app;

/* 摄像头帧缓冲放在 SDRAM（见 pinmap.h 的地址分配） */
static uint16_t *s_cam_buf = (uint16_t *)CAM_FB_ADDR;

/* 界面缓存：记住上次画出来的内容，用于"只在变化时重画" */
static int32_t    s_shown_temp = -1000;
static int32_t    s_shown_humi = -1000;
static int32_t    s_shown_volt = -1000;      /* 0.01V 为单位 */
static int32_t    s_shown_raw = -1;
static uint8_t    s_shown_alarm = 0xFFU;
static uint8_t    s_shown_dht_ok = 0xFFU;
static uint8_t    s_shown_cam_ok = 0xFFU;
static uint8_t    s_shown_cam_fail = 0xFFU;
static int32_t    s_shown_fps = -1;
static uint8_t    s_shown_page = 0xFFU;
static uint8_t    s_shown_temp_over = 0xFFU;
static uint8_t    s_shown_humi_over = 0xFFU;
static uint8_t    s_shown_volt_over = 0xFFU;
static uint8_t    s_shown_dht_status = 0xFFU;

static void app_page_enter(PageId page);
static void app_cam_start_frame(void);
static void ui_thresholds_static(void);
static void ui_threshold_mark(uint16_t row, uint8_t over);

/* ==========================================================================
 * 小工具
 * ========================================================================== */

/* 比较浮点数是否变化（用 0.01 的量化步长，避免无意义的刷新） */
static int32_t quantize2(float v)
{
  return (int32_t)(v * 100.0f + ((v >= 0.0f) ? 0.5f : -0.5f));
}

static const char *alarm_text(AlarmState st)
{
  switch (st)
  {
    case ALARM_WARN:  return "越限报警";
    case ALARM_FAULT: return "数据故障";
    default:          return "系统正常";
  }
}

static uint16_t alarm_color(AlarmState st)
{
  switch (st)
  {
    case ALARM_WARN:  return COLOR_WARN;
    case ALARM_FAULT: return COLOR_FAULT;
    default:          return COLOR_OK;
  }
}

static const char *dht_status_text(DHT11_Status st)
{
  switch (st)
  {
    case DHT11_OK:              return "正常";
    case DHT11_ERR_NO_RESPONSE: return "无应答";
    case DHT11_ERR_TIMEOUT:     return "超时";
    case DHT11_ERR_CHECKSUM:    return "校验错";
    case DHT11_ERR_RANGE:       return "超量程";
    default:                    return "未读取";
  }
}

/* ==========================================================================
 * 首页：静态部分
 * ========================================================================== */
static void ui_card_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  BSP_LCD_FillRect(x, y, w, h, COLOR_CARD);
  BSP_LCD_DrawRect(x, y, w, h, COLOR_CARD_EDGE);
}

static void ui_panel_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *title)
{
  BSP_LCD_FillRect(x, y, w, h, COLOR_CARD);
  BSP_LCD_DrawRect(x, y, w, h, COLOR_CARD_EDGE);
  BSP_LCD_DrawString(x + 16U, y + 10U, title, COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);
}

static void ui_home_static(void)
{
  BSP_LCD_Clear(COLOR_BG);

  /* 顶栏标题 */
  BSP_LCD_FillRect(0U, 0U, LCD_WIDTH, UI_TOP_H, COLOR_TOP);
  BSP_LCD_DrawStringCentered(16U, EXP_TITLE, LCD_WHITE, COLOR_TOP, LCD_FONT_LARGE);

  /* 学生信息（由 Inc/config.h 的常量决定） */
  BSP_LCD_DrawString(24U, 78U, "姓名 " STUDENT_NAME, COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(260U, 78U, "学号 " STUDENT_ID, COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawString(560U, 78U, "班级 " STUDENT_CLASS, COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);

  /* 三张数据卡片 */
  ui_card_frame(CARD_X, CARD_Y1, CARD_W, CARD_H);
  BSP_LCD_DrawString(CARD_X + 20U, CARD_Y1 + 12U, "温度 DHT11", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);

  ui_card_frame(CARD_X, CARD_Y2, CARD_W, CARD_H);
  BSP_LCD_DrawString(CARD_X + 20U, CARD_Y2 + 12U, "湿度 DHT11", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);

  ui_card_frame(CARD_X, CARD_Y3, CARD_W, CARD_H);
  BSP_LCD_DrawString(CARD_X + 20U, CARD_Y3 + 12U, "电压 RV1/ADC1_IN5", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);

  /* 右上：阈值与越限情况 */
  ui_panel_frame(PANEL_X, PANEL_Y1, PANEL_W, PANEL_H1, "预警阈值（可在 config.h 修改）");
  ui_thresholds_static();

  /* 右下：模块状态 */
  ui_panel_frame(PANEL_X, PANEL_Y2, PANEL_W, PANEL_H2, "模块状态");

  /* 底部操作提示 */
  BSP_LCD_FillRect(0U, FOOT_Y, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - FOOT_Y), COLOR_TOP);
  BSP_LCD_DrawString(24U, FOOT_Y + 8U,
                     "K0/UP 切换页面    K1 重试摄像头    K2 立即测温湿度",
                     COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);

  /* 缓存清零，强制下面全量画一遍动态部分 */
  s_shown_temp = -1000;
  s_shown_humi = -1000;
  s_shown_volt = -1000;
  s_shown_raw  = -1;
  s_shown_alarm = 0xFFU;
  s_shown_dht_ok = 0xFFU;
  s_shown_cam_ok = 0xFFU;
  s_shown_cam_fail = 0xFFU;
  s_shown_temp_over = 0xFFU;
  s_shown_humi_over = 0xFFU;
  s_shown_volt_over = 0xFFU;
}

/* ==========================================================================
 * 首页：动态部分（只重画变化的小矩形）
 * ========================================================================== */
static void ui_temp_value(void)
{
  uint16_t x = CARD_X + 20U;
  uint16_t y = CARD_Y1 + 46U;
  int32_t  shown = s_app.dht_valid ? (int32_t)s_app.dht.temperature : -1000;

  if (shown == s_shown_temp)
  {
    return;
  }
  s_shown_temp = shown;

  BSP_LCD_FillRect(x, y, 300U, 36U, COLOR_CARD);

  if (s_app.dht_valid)
  {
    uint16_t fg = (s_app.temp_over != 0U) ? COLOR_FAULT : COLOR_VALUE;
    uint16_t w = BSP_LCD_DrawInt(x, y + 2U, (int32_t)s_app.dht.temperature,
                                 fg, COLOR_CARD, LCD_FONT_LARGE);
    BSP_LCD_DrawString((uint16_t)(x + w + 8U), y + 10U, "℃", COLOR_UNIT, COLOR_CARD, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y + 4U, "--", COLOR_FAULT, COLOR_CARD, LCD_FONT_LARGE);
    BSP_LCD_DrawString(x + 70U, y + 10U, dht_status_text(s_app.dht_status),
                       COLOR_FAULT, COLOR_CARD, LCD_FONT_SMALL);
  }
}

static void ui_humi_value(void)
{
  uint16_t x = CARD_X + 20U;
  uint16_t y = CARD_Y2 + 46U;
  int32_t  shown = s_app.dht_valid ? (int32_t)s_app.dht.humidity : -1000;

  if (shown == s_shown_humi)
  {
    return;
  }
  s_shown_humi = shown;

  BSP_LCD_FillRect(x, y, 300U, 36U, COLOR_CARD);

  if (s_app.dht_valid)
  {
    uint16_t fg = (s_app.humi_over != 0U) ? COLOR_FAULT : COLOR_VALUE;
    uint16_t w = BSP_LCD_DrawInt(x, y + 2U, (int32_t)s_app.dht.humidity,
                                 fg, COLOR_CARD, LCD_FONT_LARGE);
    BSP_LCD_DrawString((uint16_t)(x + w + 8U), y + 10U, "%RH", COLOR_UNIT, COLOR_CARD, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y + 4U, "--", COLOR_FAULT, COLOR_CARD, LCD_FONT_LARGE);
    BSP_LCD_DrawString(x + 70U, y + 10U, "传感器无有效数据",
                       COLOR_FAULT, COLOR_CARD, LCD_FONT_SMALL);
  }
}

static void ui_volt_value(void)
{
  uint16_t x = CARD_X + 20U;
  uint16_t y = CARD_Y3 + 46U;
  int32_t  shown = s_app.adc_valid ? quantize2(s_app.volt) : -10000;

  if (shown == s_shown_volt)
  {
    return;
  }
  s_shown_volt = shown;

  BSP_LCD_FillRect(x, y, 300U, 36U, COLOR_CARD);

  if (s_app.adc_valid)
  {
    uint16_t fg = (s_app.volt_over != 0U) ? COLOR_FAULT : COLOR_VALUE;
    uint16_t w = BSP_LCD_DrawFixed2(x, y + 2U, s_app.volt, fg, COLOR_CARD, LCD_FONT_LARGE);
    BSP_LCD_DrawString((uint16_t)(x + w + 8U), y + 10U, "V", COLOR_UNIT, COLOR_CARD, LCD_FONT_SMALL);
  }
  else
  {
    BSP_LCD_DrawString(x, y + 4U, "--", COLOR_FAULT, COLOR_CARD, LCD_FONT_LARGE);
    BSP_LCD_DrawString(x + 70U, y + 10U, "ADC 读取失败", COLOR_FAULT, COLOR_CARD, LCD_FONT_SMALL);
  }
}

/* 阈值面板：三行 = 名称 + 上限（取自 config.h）+ 越限标记 */
static uint16_t threshold_row_y(uint16_t row)
{
  return (uint16_t)(PANEL_Y1 + 44U + row * 42U);
}

/* 静态部分：名称与上限，进入页面时画一次（上限直接读 config.h 的宏） */
static void ui_thresholds_static(void)
{
  for (uint16_t row = 0U; row < 3U; row++)
  {
    uint16_t x = PANEL_X + 16U;
    uint16_t y = threshold_row_y(row);
    const char *name = (row == 0U) ? "温度上限" : ((row == 1U) ? "湿度上限" : "电压上限");
    float       limit = (row == 0U) ? CFG_TEMP_MAX_C
                                    : ((row == 1U) ? CFG_HUMI_MAX_PCT : CFG_VOLT_MAX_V);
    const char *unit = (row == 0U) ? "℃" : ((row == 1U) ? "%RH" : "V");
    uint16_t    w;

    BSP_LCD_FillRect(x, y, PANEL_W - 32U, 38U, COLOR_CARD);
    BSP_LCD_DrawString(x, y + 8U, name, COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);
    w = BSP_LCD_DrawFixed2((uint16_t)(x + 110U), y + 8U, limit,
                           COLOR_UNIT, COLOR_CARD, LCD_FONT_SMALL);
    BSP_LCD_DrawString((uint16_t)(x + 110U + w + 4U), y + 8U, unit,
                       COLOR_UNIT, COLOR_CARD, LCD_FONT_SMALL);
  }
}

/* 动态部分：只有"越限/正常"这一小块会在状态变化时重画 */
static void ui_threshold_mark(uint16_t row, uint8_t over)
{
  uint16_t x = (uint16_t)(PANEL_X + 16U + 250U);
  uint16_t y = (uint16_t)(threshold_row_y(row) + 8U);

  BSP_LCD_FillRect(x, y, 70U, 24U, COLOR_CARD);
  BSP_LCD_DrawString(x, y, (over != 0U) ? "越限" : "正常",
                     (over != 0U) ? COLOR_FAULT : COLOR_OK, COLOR_CARD, LCD_FONT_SMALL);
}

static void ui_threshold_marks(void)
{
  if (s_app.temp_over != s_shown_temp_over)
  {
    s_shown_temp_over = s_app.temp_over;
    ui_threshold_mark(0U, s_app.temp_over);
  }
  if (s_app.humi_over != s_shown_humi_over)
  {
    s_shown_humi_over = s_app.humi_over;
    ui_threshold_mark(1U, s_app.humi_over);
  }
  if (s_app.volt_over != s_shown_volt_over)
  {
    s_shown_volt_over = s_app.volt_over;
    ui_threshold_mark(2U, s_app.volt_over);
  }
}

/* 模块状态面板：三行 = DHT11 / 电压表(原始值+换算) / 摄像头
 * 每行单独判断是否需要重画，ADC 原始值每 200ms 会变，也只重画这一行 */
static void ui_module_status(void)
{
  uint16_t x = PANEL_X + 16U;
  uint8_t  dht_ok = (s_app.dht_valid != 0U) ? 1U : 0U;
  uint8_t  cam_ok = (s_app.cam_ready != 0U) ? 1U : 0U;
  int32_t  raw = (s_app.adc_valid != 0U) ? (int32_t)s_app.adc_raw : -1;

  /* 第 1 行：DHT11 温湿度 */
  if ((dht_ok != s_shown_dht_ok) || ((uint8_t)s_app.dht_status != s_shown_dht_status))
  {
    s_shown_dht_ok     = dht_ok;
    s_shown_dht_status = (uint8_t)s_app.dht_status;
    BSP_LCD_FillRect(x, PANEL_Y2 + 34U, PANEL_W - 32U, 26U, COLOR_CARD);
    BSP_LCD_DrawString(x, PANEL_Y2 + 36U, "DHT11", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);
    BSP_LCD_DrawString(x + 90U, PANEL_Y2 + 36U, dht_status_text(s_app.dht_status),
                       (dht_ok != 0U) ? COLOR_OK : COLOR_FAULT, COLOR_CARD, LCD_FONT_SMALL);
  }

  /* 第 2 行：电压表（原始 ADC 值 + 换算电压，实验要求两者同时显示） */
  if (raw != s_shown_raw)
  {
    s_shown_raw = raw;
    BSP_LCD_FillRect(x, PANEL_Y2 + 62U, PANEL_W - 32U, 26U, COLOR_CARD);
    BSP_LCD_DrawString(x, PANEL_Y2 + 64U, "电压表", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);

    if (raw >= 0)
    {
      uint16_t cx = BSP_LCD_DrawInt(x + 90U, PANEL_Y2 + 64U, raw,
                                    COLOR_VALUE, COLOR_CARD, LCD_FONT_SMALL);
      cx = (uint16_t)(x + 90U + cx + 10U);
      BSP_LCD_DrawString(cx, PANEL_Y2 + 64U, "->", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);
      cx = (uint16_t)(cx + 30U);
      cx = BSP_LCD_DrawFixed2(cx, PANEL_Y2 + 64U, s_app.volt,
                              COLOR_VALUE, COLOR_CARD, LCD_FONT_SMALL);
      BSP_LCD_DrawString((uint16_t)(cx + 4U), PANEL_Y2 + 64U, "V",
                         COLOR_UNIT, COLOR_CARD, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawString(x + 90U, PANEL_Y2 + 64U, "读取失败", COLOR_FAULT, COLOR_CARD, LCD_FONT_SMALL);
    }
  }

  /* 第 3 行：摄像头 */
  if (cam_ok != s_shown_cam_ok)
  {
    s_shown_cam_ok = cam_ok;
    BSP_LCD_FillRect(x, PANEL_Y2 + 90U, PANEL_W - 32U, 26U, COLOR_CARD);
    BSP_LCD_DrawString(x, PANEL_Y2 + 92U, "摄像头", COLOR_LABEL, COLOR_CARD, LCD_FONT_SMALL);
    BSP_LCD_DrawString(x + 90U, PANEL_Y2 + 92U, (cam_ok != 0U) ? "就绪" : "未初始化",
                       (cam_ok != 0U) ? COLOR_OK : COLOR_FAULT, COLOR_CARD, LCD_FONT_SMALL);
  }
}

/* 报警状态横幅：画在顶栏右侧（醒目，且不占用下方面板）
 * 三态与双色灯的对应关系在这里明确写出来，方便验收时对照：
 *      正常 -> 绿灯常亮；越限 -> 绿灯灭 + 红灯 2Hz 闪；故障 -> 绿灯灭 + 红灯常亮 */
static void ui_alarm_line(void)
{
  const char *note;
  uint16_t    x = 548U;
  uint16_t    y = 6U;
  uint16_t    w = 240U;

  if (s_app.alarm == s_shown_alarm)
  {
    return;
  }
  s_shown_alarm = (uint8_t)s_app.alarm;

  switch (s_app.alarm)
  {
    case ALARM_WARN:  note = "绿灯灭  红灯 2Hz 闪"; break;
    case ALARM_FAULT: note = "绿灯灭  红灯常亮"; break;
    default:          note = "绿灯常亮  红灯灭"; break;
  }

  BSP_LCD_FillRect(x, y, w, 52U, COLOR_TOP);
  BSP_LCD_DrawString((uint16_t)(x + 4U), (uint16_t)(y + 4U), alarm_text(s_app.alarm),
                     alarm_color(s_app.alarm), COLOR_TOP, LCD_FONT_LARGE);
  BSP_LCD_DrawString((uint16_t)(x + 4U), (uint16_t)(y + 38U), note,
                     COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
}

static void ui_home_refresh(void)
{
  ui_temp_value();
  ui_humi_value();
  ui_volt_value();
  ui_threshold_marks();
  ui_alarm_line();
  ui_module_status();
}

/* ==========================================================================
 * 摄像头页
 * ========================================================================== */
static void ui_camera_static(void)
{
  BSP_LCD_Clear(COLOR_BG);

  /* 左侧预览区边框 */
  BSP_LCD_DrawRect(0U, 0U, CAM_VIEW_W, LCD_HEIGHT, COLOR_CARD_EDGE);

  /* 右侧状态栏 */
  BSP_LCD_FillRect(CAM_SIDE_X, 0U, CAM_SIDE_W, LCD_HEIGHT, COLOR_TOP);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 12U, "摄像头预览", COLOR_WHITE, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 60U, "温度", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 130U, "湿度", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 200U, "电压", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 300U, "预警生效", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 400U, "K0 返回首页", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);
  BSP_LCD_DrawString(CAM_SIDE_X + 8U, 428U, "K1 重初始化", COLOR_LABEL, COLOR_TOP, LCD_FONT_SMALL);

  s_shown_temp = -1000;
  s_shown_humi = -1000;
  s_shown_volt = -1000;
  s_shown_alarm = 0xFFU;
  s_shown_cam_fail = 0xFFU;
  s_shown_fps = -1;
}

/* 预览区中间画提示（初始化失败 / 启动中） */
static void ui_camera_message(const char *line1, const char *line2, uint16_t color)
{
  BSP_LCD_FillRect(0U, 170U, CAM_VIEW_W, 140U, COLOR_BG);
  BSP_LCD_DrawStringCentered(200U, line1, color, COLOR_BG, LCD_FONT_LARGE);
  if (line2 != NULL)
  {
    BSP_LCD_DrawStringCentered(250U, line2, COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  }
}

static void ui_camera_side(void)
{
  uint16_t x = CAM_SIDE_X + 8U;

  /* 温度 */
  if ((s_app.dht_valid ? (int32_t)s_app.dht.temperature : -1000) != s_shown_temp)
  {
    s_shown_temp = s_app.dht_valid ? (int32_t)s_app.dht.temperature : -1000;
    BSP_LCD_FillRect(x, 90U, CAM_SIDE_W - 16U, 34U, COLOR_TOP);
    if (s_app.dht_valid)
    {
      uint16_t w = BSP_LCD_DrawInt(x, 92U, (int32_t)s_app.dht.temperature,
                                   (s_app.temp_over != 0U) ? COLOR_FAULT : COLOR_WHITE,
                                   COLOR_TOP, LCD_FONT_LARGE);
      BSP_LCD_DrawString((uint16_t)(x + w + 6U), 100U, "℃", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawString(x, 96U, "--", COLOR_FAULT, COLOR_TOP, LCD_FONT_LARGE);
    }
  }

  /* 湿度 */
  if ((s_app.dht_valid ? (int32_t)s_app.dht.humidity : -1000) != s_shown_humi)
  {
    s_shown_humi = s_app.dht_valid ? (int32_t)s_app.dht.humidity : -1000;
    BSP_LCD_FillRect(x, 160U, CAM_SIDE_W - 16U, 34U, COLOR_TOP);
    if (s_app.dht_valid)
    {
      uint16_t w = BSP_LCD_DrawInt(x, 162U, (int32_t)s_app.dht.humidity,
                                   (s_app.humi_over != 0U) ? COLOR_FAULT : COLOR_WHITE,
                                   COLOR_TOP, LCD_FONT_LARGE);
      BSP_LCD_DrawString((uint16_t)(x + w + 6U), 170U, "%", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawString(x, 166U, "--", COLOR_FAULT, COLOR_TOP, LCD_FONT_LARGE);
    }
  }

  /* 电压 */
  if ((s_app.adc_valid ? quantize2(s_app.volt) : -10000) != s_shown_volt)
  {
    s_shown_volt = s_app.adc_valid ? quantize2(s_app.volt) : -10000;
    BSP_LCD_FillRect(x, 230U, CAM_SIDE_W - 16U, 34U, COLOR_TOP);
    if (s_app.adc_valid)
    {
      uint16_t w = BSP_LCD_DrawFixed2(x, 232U, s_app.volt,
                                      (s_app.volt_over != 0U) ? COLOR_FAULT : COLOR_WHITE,
                                      COLOR_TOP, LCD_FONT_LARGE);
      BSP_LCD_DrawString((uint16_t)(x + w + 6U), 240U, "V", COLOR_UNIT, COLOR_TOP, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawString(x, 236U, "--", COLOR_FAULT, COLOR_TOP, LCD_FONT_LARGE);
    }
  }

  /* 报警状态（摄像头页也必须能看到预警是否生效） */
  if ((uint8_t)s_app.alarm != s_shown_alarm)
  {
    s_shown_alarm = (uint8_t)s_app.alarm;
    BSP_LCD_FillRect(x, 330U, CAM_SIDE_W - 16U, 34U, COLOR_TOP);
    BSP_LCD_DrawString(x, 334U, alarm_text(s_app.alarm), alarm_color(s_app.alarm),
                       COLOR_TOP, LCD_FONT_SMALL);
  }

  /* 摄像头状态与帧率 */
  if ((s_app.cam_fail_count != s_shown_cam_fail) || ((int32_t)s_app.cam_fps != s_shown_fps))
  {
    s_shown_cam_fail = s_app.cam_fail_count;
    s_shown_fps = (int32_t)s_app.cam_fps;
    BSP_LCD_FillRect(x, 360U, CAM_SIDE_W - 16U, 30U, COLOR_TOP);
    if (s_app.cam_fail_count == 0U)
    {
      BSP_LCD_DrawString(x, 364U, "预览正常", COLOR_OK, COLOR_TOP, LCD_FONT_SMALL);
    }
    else
    {
      BSP_LCD_DrawString(x, 364U, "预览异常", COLOR_FAULT, COLOR_TOP, LCD_FONT_SMALL);
    }
  }
}

/* ==========================================================================
 * 任务
 * ========================================================================== */
static void task_dht(uint32_t now)
{
  DHT11_Data data;
  DHT11_Status st = DHT11_Read(&data);

  s_app.dht_status = st;

  if (st == DHT11_OK)
  {
    s_app.dht = data;
    s_app.dht_valid = 1U;
    s_app.dht_fail_count = 0U;
  }
  else
  {
    /* 关键要求：读失败时绝不能把旧数据当成最新读数 */
    s_app.dht_valid = 0U;
    if (s_app.dht_fail_count < 0xFFU)
    {
      s_app.dht_fail_count++;
    }
  }

  s_app.t_dht = now;
}

static void task_adc(uint32_t now)
{
  uint16_t raw = BSP_ADC_ReadAverage((uint8_t)CFG_ADC_AVG_COUNT);

  if (raw != 0xFFFFU)
  {
    s_app.adc_raw   = raw;
    s_app.volt      = BSP_ADC_ToVolt(raw);
    s_app.adc_valid = 1U;
  }
  else
  {
    s_app.adc_valid = 0U;
  }

  s_app.t_adc = now;
}

/* 阈值判定：进入用"大于上限"，退出用"小于等于上限 - 回差"，形成回差带 */
static void alarm_thresholds(void)
{
  if (s_app.dht_valid != 0U)
  {
    float t = (float)s_app.dht.temperature;
    float h = (float)s_app.dht.humidity;

    if (s_app.temp_over != 0U)
    {
      if (t <= (CFG_TEMP_MAX_C - CFG_TEMP_HYST_C)) { s_app.temp_over = 0U; }
    }
    else
    {
      if (t > CFG_TEMP_MAX_C) { s_app.temp_over = 1U; }
    }

    if (s_app.humi_over != 0U)
    {
      if (h <= (CFG_HUMI_MAX_PCT - CFG_HUMI_HYST_PCT)) { s_app.humi_over = 0U; }
    }
    else
    {
      if (h > CFG_HUMI_MAX_PCT) { s_app.humi_over = 1U; }
    }
  }

  if (s_app.adc_valid != 0U)
  {
    if (s_app.volt_over != 0U)
    {
      if (s_app.volt <= (CFG_VOLT_MAX_V - CFG_VOLT_HYST_V)) { s_app.volt_over = 0U; }
    }
    else
    {
      if (s_app.volt > CFG_VOLT_MAX_V) { s_app.volt_over = 1U; }
    }
  }
}

static void alarm_evaluate(void)
{
  AlarmState next;

  alarm_thresholds();

  if ((s_app.dht_valid == 0U) || (s_app.adc_valid == 0U))
  {
    next = ALARM_FAULT;                       /* 数据无效 / 传感器失联：优先 */
  }
  else if ((s_app.temp_over != 0U) || (s_app.humi_over != 0U) || (s_app.volt_over != 0U))
  {
    next = ALARM_WARN;
  }
  else
  {
    next = ALARM_NORMAL;
  }

  /* 故障恢复后不能带着旧的越限标志，重新按当前值判一次 */
  if ((next != ALARM_FAULT) && (s_app.alarm == ALARM_FAULT))
  {
    s_app.temp_over = 0U;
    s_app.humi_over = 0U;
    s_app.volt_over = 0U;
  }

  s_app.alarm = next;
}

static void task_led(uint32_t now)
{
  switch (s_app.alarm)
  {
    case ALARM_WARN:
      BSP_Led_Green(0U);
      if ((now - s_app.t_led) >= CFG_LED_BLINK_MS)
      {
        s_app.t_led = now;
        s_app.led_phase ^= 1U;
      }
      BSP_Led_Red((s_app.led_phase != 0U) ? 1U : 0U);
      break;

    case ALARM_FAULT:
      BSP_Led_Green(0U);
      BSP_Led_Red(1U);                        /* 故障：红灯常亮 */
      s_app.led_phase = 1U;
      break;

    default:
      BSP_Led_Green(1U);                      /* 正常：绿灯常亮 */
      BSP_Led_Red(0U);
      s_app.led_phase = 0U;
      break;
  }
}

/* 摄像头：启动一帧 */
static void app_cam_start_frame(void)
{
  if (BSP_DCMI_StartFrame(s_cam_buf, CAM_BUF_WORDS) == 0U)
  {
    s_app.cam_streaming = 1U;
    s_app.cam_last_start_ms = HAL_GetTick();
  }
  else
  {
    s_app.cam_streaming = 0U;
    if (s_app.cam_fail_count < 0xFFU)
    {
      s_app.cam_fail_count++;
    }
  }
}

static void app_cam_init_once(void)
{
  OV5640_Status st = OV5640_Init();

  if (st == OV5640_OK)
  {
    s_app.cam_ready = 1U;
    s_app.cam_fail_count = 0U;
    s_app.cam_streaming = 0U;
    app_cam_start_frame();
  }
  else
  {
    s_app.cam_ready = 0U;
    s_app.cam_streaming = 0U;
  }
}

static void task_camera(uint32_t now)
{
  if (s_app.page != PAGE_CAMERA)
  {
    return;
  }

  if (s_app.cam_ready == 0U)
  {
    return;                                   /* 静态提示已经画过 */
  }

  if (BSP_DCMI_TakeFrame() != 0U)
  {
    /* 一帧搬完：先按需做字节交换，再 2 倍放大贴到左侧预览区 */
#if CFG_CAM_BYTE_SWAP
    BSP_DCMI_SwapBytes(s_cam_buf, (uint32_t)OV5640_WIDTH * OV5640_HEIGHT);
#endif
    BSP_LCD_BlitRGB565x2(0U, 0U, OV5640_WIDTH, OV5640_HEIGHT, s_cam_buf);

    s_app.cam_fail_count = 0U;
    s_app.fps_frames++;

    /* 每秒统计一次帧率（x10 保存，显示时除以 10） */
    if ((now - s_app.t_fps) >= 1000U)
    {
      s_app.cam_fps = s_app.fps_frames * 10U;
      s_app.fps_frames = 0U;
      s_app.t_fps = now;
    }

    app_cam_start_frame();
    return;
  }

  /* 超时：说明摄像头没在输出（线没插好 / PCLK 没来） */
  if ((s_app.cam_streaming != 0U) &&
      ((now - s_app.cam_last_start_ms) > CFG_CAM_TIMEOUT_MS))
  {
    s_app.cam_streaming = 0U;
    if (s_app.cam_fail_count < 0xFFU)
    {
      s_app.cam_fail_count++;
    }

    if ((BSP_DCMI_HasError() != 0U) || (s_app.cam_fail_count >= CFG_CAM_FAIL_LIMIT))
    {
      BSP_DCMI_Stop();
      BSP_DCMI_ClearError();
      s_app.cam_ready = 0U;
      ui_camera_message("摄像头无信号", "请检查 P6 连接后按 K1 重新初始化", COLOR_FAULT);
    }
    else
    {
      app_cam_start_frame();                  /* 再试一帧 */
    }
  }
}

/* 页面切换 */
static void app_page_enter(PageId page)
{
  s_app.page = page;
  s_shown_page = (uint8_t)page;

  if (page == PAGE_CAMERA)
  {
    ui_camera_static();

    if (s_app.cam_ready == 0U)
    {
      ui_camera_message("摄像头初始化中", "OV5640 @ P6 (DCMI)", COLOR_LABEL);
      app_cam_init_once();
    }

    if (s_app.cam_ready != 0U)
    {
      s_app.t_fps = HAL_GetTick();
      s_app.fps_frames = 0U;
      app_cam_start_frame();
      ui_camera_side();
    }
    else
    {
      ui_camera_message("摄像头初始化失败", "请检查 P6 连接后按 K1 重新初始化", COLOR_FAULT);
    }
  }
  else
  {
    BSP_DCMI_Stop();
    ui_home_static();
    ui_home_refresh();
  }
}

static void task_key(uint32_t now)
{
  KeyEvent ev = BSP_Key_Scan(now);

  s_app.t_key = now;

  if (ev == KEY_EVENT_NONE)
  {
    return;
  }

  switch (ev)
  {
    case KEY_EVENT_K0:
    case KEY_EVENT_UP:
      app_page_enter((s_app.page == PAGE_HOME) ? PAGE_CAMERA : PAGE_HOME);
      break;

    case KEY_EVENT_K1:
      if (s_app.page == PAGE_CAMERA)
      {
        /* 重新初始化摄像头：先停采集，再重走一遍 OV5640_Init */
        BSP_DCMI_Stop();
        s_app.cam_ready = 0U;
        s_app.cam_fail_count = 0U;
        ui_camera_message("摄像头初始化中", "OV5640 @ P6 (DCMI)", COLOR_LABEL);
        app_cam_init_once();
        if (s_app.cam_ready != 0U)
        {
          ui_camera_static();
          s_app.t_fps = now;
          s_app.fps_frames = 0U;
          app_cam_start_frame();
        }
        else
        {
          ui_camera_message("摄像头初始化失败", "请检查 P6 连接后按 K1 重新初始化", COLOR_FAULT);
        }
      }
      break;

    case KEY_EVENT_K2:
      task_dht(now);                          /* 立即测温湿度，便于现场验证 */
      break;

    default:
      break;
  }
}

static void task_ui(uint32_t now)
{
  if (s_app.startup != 0U)
  {
    return;
  }

  if (s_app.page == PAGE_HOME)
  {
    ui_home_refresh();
  }
  else
  {
    ui_camera_side();
  }

  s_app.t_ui = now;
}

/* ==========================================================================
 * 初始化与主任务
 * ========================================================================== */
void App_Init(void)
{
  uint8_t sdram_rc;

  /* 先把状态清零 */
  s_app.dht_valid       = 0U;
  s_app.dht_status      = DHT11_OK;
  s_app.adc_valid       = 0U;
  s_app.alarm           = ALARM_NORMAL;
  s_app.cam_ready       = 0U;
  s_app.cam_fail_count  = 0U;
  s_app.cam_streaming   = 0U;
  s_app.cam_fps         = 0U;
  s_app.fps_frames      = 0U;
  s_app.page            = PAGE_HOME;
  s_app.startup         = 1U;
  s_app.led_phase       = 0U;

  BSP_Led_Init();
  BSP_DelayInit();
  BSP_Key_Init();
  DHT11_Init();

  /* SDRAM：LCD 和摄像头都靠它，先做自检，出问题就直接报出来 */
  sdram_rc = BSP_SDRAM_Init();
  if (sdram_rc == 0U)
  {
    sdram_rc = BSP_SDRAM_Test();
  }

  if (BSP_LCD_Init() != 0U)
  {
    Error_Handler();
  }

  if (sdram_rc != 0U)
  {
    BSP_LCD_Clear(COLOR_BG);
    BSP_LCD_DrawStringCentered(200U, "SDRAM 自检失败", COLOR_FAULT, COLOR_BG, LCD_FONT_LARGE);
    BSP_LCD_DrawStringCentered(250U, "帧缓冲不可用，请检查 FMC 焊接/时序", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
    Error_Handler();
  }

  /* 启动画面 */
  BSP_LCD_Clear(COLOR_BG);
  BSP_LCD_DrawStringCentered(180U, EXP_TITLE, COLOR_WHITE, COLOR_BG, LCD_FONT_LARGE);
  BSP_LCD_DrawStringCentered(230U, EXP_SUBTITLE, COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);
  BSP_LCD_DrawStringCentered(280U, "系统启动中，正在初始化传感器与摄像头…", COLOR_LABEL, COLOR_BG, LCD_FONT_SMALL);

  BSP_DelayMs(120U);

  if (BSP_ADC_Init() != 0U)
  {
    BSP_LCD_DrawStringCentered(320U, "ADC 初始化失败", COLOR_FAULT, COLOR_BG, LCD_FONT_SMALL);
  }

  if (BSP_DCMI_Init() != 0U)
  {
    BSP_LCD_DrawStringCentered(350U, "DCMI 初始化失败", COLOR_FAULT, COLOR_BG, LCD_FONT_SMALL);
  }

  /* DHT11 上电后需要约 1s 稳定时间，这里等够再进主循环 */
  BSP_DelayMs(1200U);

  /* 第一次采集：让界面一上来就有真实数据 */
  task_dht(HAL_GetTick());
  task_adc(HAL_GetTick());

  s_app.startup = 0U;

  /* 进入首页并画第一次 */
  app_page_enter(PAGE_HOME);

  /* 时间戳初始化：下一次采集按周期到来 */
  {
    uint32_t now = HAL_GetTick();

    s_app.t_dht = now;
    s_app.t_adc = now - CFG_ADC_PERIOD_MS;    /* 让第一次 ADC 刷新尽快发生 */
    s_app.t_key = now;
    s_app.t_ui  = now;
    s_app.t_led = now;
    s_app.t_fps = now;
  }
}

void App_Task(void)
{
  uint32_t now = HAL_GetTick();

  /* 传感器采集：与当前页面无关，切到摄像头页也照常进行 */
  if ((now - s_app.t_dht) >= CFG_DHT_PERIOD_MS)
  {
    task_dht(now);
  }
  if ((now - s_app.t_adc) >= CFG_ADC_PERIOD_MS)
  {
    task_adc(now);
  }

  /* 报警判定 + 双色灯 */
  alarm_evaluate();
  task_led(now);

  /* 按键 */
  if ((now - s_app.t_key) >= CFG_KEY_PERIOD_MS)
  {
    task_key(now);
  }

  /* 界面 */
  if ((now - s_app.t_ui) >= CFG_UI_PERIOD_MS)
  {
    task_ui(now);
  }

  /* 摄像头 */
  task_camera(now);
}

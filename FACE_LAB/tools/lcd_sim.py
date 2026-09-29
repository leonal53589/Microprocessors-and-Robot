# -*- coding: utf-8 -*-
"""
LCD 界面离线仿真：不接硬件，按 Src/app.c 里完全相同的布局常量把
中控首页 / 越限报警 / 摄像头预览页渲染成 800x480 的 PNG，
用来在烧录之前核对：
    * 文字是否越界（右边被裁掉、覆盖其它控件）
    * 数值/状态字换一个宽度后会不会挤到别的区域
    * 中文字库有没有缺字（缺字用红色方块标出）

用法:
    py tools/lcd_sim.py [输出目录]      默认输出到 tools/sim/

注意：本脚本只做"界面核对"，几何与配色与 bsp_lcd.c / app.c 保持一致；
     真正的取字顺序由 bsp_font.c 决定（同样是按行、MSB 在左）。
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

SIMSUN = r"C:\Windows\Fonts\simsun.ttc"
W, H = 800, 480

# ---- 与 bsp_lcd.h 的 LCD_COLOR/颜色定义一致（这里用 8:8:8 再量化，近似即可） ----
def rgb565(r, g, b):
    return ((r & 0xF8), (g & 0xFC), (b & 0xF8))

BG        = rgb565(20, 40, 90)
TOP       = rgb565(12, 26, 52)
CARD      = rgb565(28, 44, 68)
CARD_EDGE = rgb565(70, 100, 140)
LABEL     = rgb565(168, 190, 212)
VALUE     = rgb565(255, 255, 255)
UNIT      = rgb565(150, 172, 194)
OK        = rgb565(70, 220, 120)
WARN      = rgb565(255, 170, 0)
FAULT     = rgb565(255, 72, 72)

# ---- 与 app.c 相同的布局常量 ----
UI_TOP_H = 64
CARD_X, CARD_W, CARD_H = 24, 360, 92
CARD_Y1, CARD_Y2, CARD_Y3 = 124, 228, 332
PANEL_X, PANEL_W = 408, 368
PANEL_Y1, PANEL_H1 = 124, 180
PANEL_Y2, PANEL_H2 = 316, 116
FOOT_Y = 444
CAM_VIEW_W = 640
CAM_SIDE_X = CAM_VIEW_W + 8
CAM_SIDE_W = W - CAM_SIDE_X

ASC_SMALL, CN_SMALL, ASC_LARGE, CN_LARGE = 12, 24, 16, 32
_fonts = {}
_overflows = []


def font_for(size):
    if size not in _fonts:
        _fonts[size] = ImageFont.truetype(SIMSUN, size)
    return _fonts[size]


def text_width(s, size):
    w = 0
    for ch in s:
        w += ASC_SMALL if ord(ch) < 0x80 else CN_SMALL
        if size == "large":
            w += (ASC_LARGE - ASC_SMALL) if ord(ch) < 0x80 else (CN_LARGE - CN_SMALL)
    return w


def draw_text(d, x, y, s, color, size="small", ctx=""):
    px = ASC_SMALL if size == "small" else ASC_LARGE
    py = CN_SMALL if size == "small" else CN_LARGE
    f = font_for(py)
    cx = x
    for ch in s:
        cw = ASC_SMALL if ord(ch) < 0x80 else CN_SMALL
        if size == "large":
            cw = ASC_LARGE if ord(ch) < 0x80 else CN_LARGE
        if cx + cw > W:
            _overflows.append("%s: 文字越界 '%s' 到 x=%d" % (ctx, s, cx + cw))
        try:
            d.text((cx, y), ch, font=f, fill=color)
        except Exception:
            d.rectangle([cx, y, cx + cw - 1, y + py - 1], outline=FAULT)
        cx += cw
    return cx - x


def rect(d, x, y, w, h, fill, outline=None):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill, outline=outline)


def centered(d, y, s, color, size="large", ctx=""):
    w = text_width(s, size)
    return draw_text(d, (W - w) // 2, y, s, color, size, ctx)


# ---------------------------------------------------------------- 首页
def home(temp=25, humi=60, volt="2.53", raw=2078):
    """按 config.h 的阈值（30℃ / 70%RH / 2.80V）自动判定越限，与固件逻辑一致"""
    over_t = temp > 30
    over_h = humi > 70
    over_v = float(volt) > 2.80
    alarm = over_t or over_h or over_v

    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # 顶栏 + 标题 + 报警横幅
    rect(d, 0, 0, W, UI_TOP_H, TOP)
    centered(d, 16, "智能家居中控实验", VALUE, "large", "首页/标题")
    if alarm:
        rect(d, 548, 6, 240, 52, TOP)
        draw_text(d, 552, 10, "越限报警", WARN, "large", "首页/横幅")
        draw_text(d, 552, 44, "绿灯灭  红灯 2Hz 闪", UNIT, "small", "首页/横幅")
    else:
        rect(d, 548, 6, 240, 52, TOP)
        draw_text(d, 552, 10, "系统正常", OK, "large", "首页/横幅")
        draw_text(d, 552, 44, "绿灯常亮  红灯灭", UNIT, "small", "首页/横幅")

    # 学生信息
    draw_text(d, 24, 78, "姓名 蓝彦扬", LABEL, "small", "首页/信息")
    draw_text(d, 260, 78, "学号 2024095004", LABEL, "small", "首页/信息")
    draw_text(d, 560, 78, "班级 计软学院", LABEL, "small", "首页/信息")

    # 三张卡片（数值超上限就画成红色）
    cards = [
        (CARD_Y1, "温度 DHT11", "%d℃" % temp, over_t),
        (CARD_Y2, "湿度 DHT11", "%d%%RH" % humi, over_h),
        (CARD_Y3, "电压 RV1/ADC1_IN5", volt + "V", over_v),
    ]
    for (cy, name, val, over) in cards:
        rect(d, CARD_X, cy, CARD_W, CARD_H, CARD, CARD_EDGE)
        draw_text(d, CARD_X + 20, cy + 12, name, LABEL, "small", "首页/卡片")
        draw_text(d, CARD_X + 20, cy + 48, val, FAULT if over else VALUE, "large", "首页/卡片")

    # 右上：阈值面板
    rect(d, PANEL_X, PANEL_Y1, PANEL_W, PANEL_H1, CARD, CARD_EDGE)
    draw_text(d, PANEL_X + 16, PANEL_Y1 + 10, "预警阈值（可在 config.h 修改）", LABEL, "small", "首页/阈值")
    rows = [("温度上限", "30.00", "℃", over_t), ("湿度上限", "70.00", "%RH", over_h),
            ("电压上限", "2.80", "V", over_v)]
    for i, (name, lim, unit, over) in enumerate(rows):
        y = PANEL_Y1 + 44 + i * 42
        draw_text(d, PANEL_X + 16, y + 8, name, LABEL, "small", "首页/阈值")
        w = draw_text(d, PANEL_X + 126, y + 8, lim, UNIT, "small", "首页/阈值")
        draw_text(d, PANEL_X + 126 + w + 4, y + 8, unit, UNIT, "small", "首页/阈值")
        draw_text(d, PANEL_X + 266, y + 8, "越限" if over else "正常",
                  FAULT if over else OK, "small", "首页/阈值")

    # 右下：模块状态
    rect(d, PANEL_X, PANEL_Y2, PANEL_W, PANEL_H2, CARD, CARD_EDGE)
    draw_text(d, PANEL_X + 16, PANEL_Y2 + 10, "模块状态", LABEL, "small", "首页/模块")
    draw_text(d, PANEL_X + 16, PANEL_Y2 + 36, "DHT11", LABEL, "small", "首页/模块")
    draw_text(d, PANEL_X + 106, PANEL_Y2 + 36, "正常", OK, "small", "首页/模块")
    draw_text(d, PANEL_X + 16, PANEL_Y2 + 64, "电压表", LABEL, "small", "首页/模块")
    w = draw_text(d, PANEL_X + 106, PANEL_Y2 + 64, str(raw), VALUE, "small", "首页/模块")
    w2 = draw_text(d, PANEL_X + 106 + w + 10, PANEL_Y2 + 64, "->", LABEL, "small", "首页/模块")
    cx = PANEL_X + 106 + w + 10 + w2 + 20
    cx += draw_text(d, cx, PANEL_Y2 + 64, volt, VALUE, "small", "首页/模块")
    draw_text(d, cx + 4, PANEL_Y2 + 64, "V", UNIT, "small", "首页/模块")
    draw_text(d, PANEL_X + 16, PANEL_Y2 + 92, "摄像头", LABEL, "small", "首页/模块")
    draw_text(d, PANEL_X + 106, PANEL_Y2 + 92, "就绪", OK, "small", "首页/模块")

    # 底部提示
    rect(d, 0, FOOT_Y, W, H - FOOT_Y, TOP)
    draw_text(d, 24, FOOT_Y + 8, "K0/UP 切换页面    K1 重试摄像头    K2 立即测温湿度",
              LABEL, "small", "首页/底栏")
    return img


# ---------------------------------------------------------------- 摄像头页
def camera():
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # 左侧预览区：画一个示意画面（真机上是 OV5640 的实时画面放大 2 倍）
    for yy in range(0, H, 4):
        shade = 40 + (yy * 120) // H
        d.rectangle([0, yy, CAM_VIEW_W - 1, yy + 3], fill=(shade, shade + 30, shade + 60))
    d.rectangle([0, 0, CAM_VIEW_W - 1, H - 1], outline=CARD_EDGE)
    centered_region = CAM_VIEW_W // 2
    for txt, y, col in (("OV5640 预览", 180, VALUE), ("320x240 RGB565 -> 640x480", 220, LABEL)):
        w = text_width(txt, "large" if y == 180 else "small")
        draw_text(d, centered_region - w // 2, y, txt, col, "large" if y == 180 else "small", "摄像头/预览")

    # 右侧状态栏
    rect(d, CAM_SIDE_X, 0, CAM_SIDE_W, H, TOP)
    draw_text(d, CAM_SIDE_X + 8, 12, "摄像头预览", VALUE, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 60, "温度", LABEL, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 92, "25℃", VALUE, "large", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 130, "湿度", LABEL, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 162, "60%", VALUE, "large", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 200, "电压", LABEL, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 232, "2.53V", VALUE, "large", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 300, "预警生效", LABEL, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 334, "系统正常", OK, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 364, "预览正常", OK, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 400, "K0 返回首页", LABEL, "small", "摄像头/侧栏")
    draw_text(d, CAM_SIDE_X + 8, 428, "K1 重初始化", LABEL, "small", "摄像头/侧栏")
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "sim")
    os.makedirs(out, exist_ok=True)

    shots = [("home_normal.png", home()),
             ("home_alarm.png", home(temp=33, humi=78, volt="3.05", raw=2510)),
             ("camera.png", camera())]
    for name, img in shots:
        p = os.path.join(out, name)
        img.save(p)
        print("saved: %s" % p)

    if _overflows:
        print("\n发现 %d 处文字可能越界：" % len(_overflows))
        for o in sorted(set(_overflows)):
            print("  " + o)
    else:
        print("\n未发现文字越界。")


if __name__ == "__main__":
    main()

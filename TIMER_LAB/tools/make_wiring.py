# -*- coding: utf-8 -*-
"""
TIMER_LAB 接线示意图生成器
  用 PIL 画出清晰的原理图风格接线图（不是 ASCII 图），便于打印/对照接线。
  输出到 tools/wiring/ 目录。
"""
import os

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "wiring")

SIMSUN = r"C:\Windows\Fonts\simsun.ttc"
F14 = ImageFont.truetype(SIMSUN, 14)
F16 = ImageFont.truetype(SIMSUN, 16)
F18 = ImageFont.truetype(SIMSUN, 18)
F22 = ImageFont.truetype(SIMSUN, 22)

BLACK = (20, 20, 20)
GRAY = (140, 140, 140)
LGRAY = (225, 225, 225)
RED = (200, 30, 30)
GREEN = (20, 140, 60)
BLUE = (30, 70, 200)
ORANGE = (230, 120, 20)
BG = (255, 255, 255)


def new(w, h):
    img = Image.new("RGB", (w, h), BG)
    return img, ImageDraw.Draw(img)


def txt(d, x, y, s, font=F16, fill=BLACK, anchor=None):
    d.text((x, y), s, font=font, fill=fill, anchor=anchor)


def center(d, cx, y, s, font=F16, fill=BLACK):
    d.text((cx, y), s, font=font, fill=fill, anchor="ma")


def wire(d, pts, color=BLACK, w=2):
    d.line(pts, fill=color, width=w)


def junction(d, x, y, color=BLACK, r=4):
    d.ellipse([x - r, y - r, x + r, y + r], fill=color)


def resistor(d, cx, cy, label="1K", w=46, h=22):
    d.rectangle([cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2],
                outline=BLACK, width=2, fill=BG)
    center(d, cx, cy - 9, label, F14, BLACK)


def led(d, cx, cy, color=RED, size=15):
    """向下导通的 LED：上正下负"""
    d.polygon([(cx - size, cy - size), (cx + size, cy - size), (cx, cy + size)],
              outline=color, fill=BG)
    d.line([(cx - size, cy + size), (cx + size, cy + size)], fill=color, width=3)
    # 发光箭头
    for k in (0, 1):
        ox = cx + size + 6 + k * 9
        d.line([(ox, cy - 2), (ox + 7, cy - 11)], fill=color, width=2)
        d.polygon([(ox + 7, cy - 11), (ox + 2, cy - 9), (ox + 6, cy - 5)], fill=color)


def pinbox(d, x, y, w, h, name, sub="", fill=(240, 245, 255)):
    d.rectangle([x, y, x + w, y + h], outline=BLACK, width=2, fill=fill)
    txt(d, x + 8, y + 5, name, F16)
    if sub:
        txt(d, x + 8, y + 24, sub, F14, GRAY)


def title(d, cx, y, s):
    center(d, cx, y, s, F22, BLACK)
    d.line([(40, y + 30), (cx * 2 - 40, y + 30)], fill=LGRAY, width=2)


# =====================================================================
# 1. LED 接法（通用规则）
# =====================================================================
def d1_led_basic():
    img, d = new(880, 470)
    title(d, 440, 16, "外接 LED 的统一接法（低电平点亮）")

    x = 300
    y0 = 90
    txt(d, x, y0 - 30, "3.3V", F18, RED, anchor="ma")
    wire(d, [(x, y0), (x, y0 + 25)], RED, 3)

    resistor(d, x, y0 + 48)
    wire(d, [(x, y0 + 59), (x, y0 + 95)])

    led(d, x, y0 + 112, RED, 16)
    wire(d, [(x, y0 + 128), (x, y0 + 175)])

    pinbox(d, x - 90, y0 + 175, 180, 46, "MCU 引脚", "")
    wire(d, [(x, y0 + 175), (x, y0 + 175)], BLACK, 2)

    txt(d, x + 120, y0 + 40, "限流电阻 1KΩ", F16)
    txt(d, x + 120, y0 + 62, "（必须串，否则烧 LED 或烧 IO）", F14, GRAY)
    txt(d, x + 120, y0 + 100, "LED 正极（长脚）朝上", F16)
    txt(d, x + 120, y0 + 122, "负极（短脚）朝下，接 MCU 引脚", F14, GRAY)
    txt(d, x + 120, y0 + 168, "MCU 输出【低电平】点亮", F18, GREEN)
    txt(d, x + 120, y0 + 192, "MCU 输出【高电平】熄灭", F18, RED)

    d.rectangle([40, 380, 840, 450], outline=LGRAY, width=2)
    txt(d, 56, 390, "为什么是低电平点亮？", F16)
    txt(d, 56, 414,
        "本板 LED 是 VCC → 电阻 → LED → MCU 引脚（MCU 灌电流），且不带三极管，"
        "所以引脚可直接做 PWM 调光；", F14, GRAY)
    txt(d, 56, 432, "PWM 亮度 = 1 - 占空比（代码里写的是 CCR = 满量程 - 亮度）", F14, GRAY)
    img.save(os.path.join(OUT, "01_led_basic.png"))


# =====================================================================
# 2. P5 排针引脚表
# =====================================================================
def d2_header():
    rows = [
        ("1", "PD14", "★ 实验四  PWM 输出 (TIM4_CH3)", ORANGE),
        ("2", "PD15", "", BLACK),
        ("3", "PD0", "", BLACK),
        ("4", "PD1", "", BLACK),
        ("5", "PE7", "", BLACK),
        ("6", "PE8", "", BLACK),
        ("7", "PE9", "★ 实验五  闪光 LED (TIM1_CH1)", ORANGE),
        ("8", "PE10", "★ 实验二  外接提示灯（可选，板载DS0已够用）", GRAY),
        ("9", "PE11", "", BLACK),
        ("10", "PE12", "", BLACK),
        ("11", "PE13", "", BLACK),
        ("12", "PE14", "", BLACK),
        ("13", "PE15", "", BLACK),
        ("14", "PD8", "", BLACK),
        ("15", "PD9", "", BLACK),
        ("16", "PD10", "", BLACK),
    ]
    RH = 30
    img, d = new(880, 120 + RH * len(rows))
    title(d, 440, 16, "P5 排针引脚分布（1×16，板上丝印 FMC_D0 ~ D15）")

    x0, y0 = 60, 80
    d.rectangle([x0, y0, x0 + 760, y0 + RH * len(rows)], outline=BLACK, width=2)
    for i, (no, pin, use, col) in enumerate(rows):
        y = y0 + i * RH
        d.line([(x0, y), (x0 + 760, y)], fill=LGRAY, width=1)
        d.line([(x0 + 70, y), (x0 + 70, y + RH)], fill=LGRAY, width=1)
        d.line([(x0 + 180, y), (x0 + 180, y + RH)], fill=LGRAY, width=1)
        center(d, x0 + 35, y + 7, no, F14)
        center(d, x0 + 125, y + 7, pin, F16, col if col != BLACK else BLACK)
        txt(d, x0 + 195, y + 7, use, F14, col)

    txt(d, 60, y0 + RH * len(rows) + 12,
        "★ = 本工程用到。其余引脚保持悬空即可。", F14, GRAY)
    img.save(os.path.join(OUT, "02_header_p5.png"))


# =====================================================================
# 3. 实验三：三色 LED
# =====================================================================
def d3_exp3():
    img, d = new(940, 520)
    title(d, 470, 16, "实验三 · 三色呼吸灯接线（TIM3 三路 PWM，1kHz）")

    ytop = 100
    chans = [
        (240, "PB5", "蓝", BLUE, "只需外接这一只！", True),
        (470, "PB0", "绿", GREEN, "板载 DS1，已现成", False),
        (700, "PB1", "红", RED, "板载 DS0，已现成", False),
    ]
    # 3.3V 母线
    wire(d, [(200, ytop), (760, ytop)], RED, 3)
    txt(d, 210, ytop - 26, "3.3V", F18, RED)

    for cx, pin, color_name, color, note, need in chans:
        junction(d, cx, ytop)
        wire(d, [(cx, ytop), (cx, ytop + 30)], BLACK, 2)
        resistor(d, cx, ytop + 44, "1K")
        wire(d, [(cx, ytop + 55), (cx, ytop + 90)], BLACK, 2)
        led(d, cx, ytop + 108, color, 16)
        wire(d, [(cx, ytop + 124), (cx, ytop + 190)], BLACK, 2)

        fill = (255, 246, 220) if need else (238, 238, 238)
        pinbox(d, cx - 80, ytop + 190, 160, 50, pin, color_name + "灯", fill)
        wire(d, [(cx, ytop + 190), (cx, ytop + 190)], BLACK, 2)

        txt(d, cx - 70, ytop + 250, note, F14, ORANGE if need else GRAY)

    d.rectangle([40, 440, 900, 505], outline=LGRAY, width=2)
    txt(d, 56, 448, "说明", F16)
    txt(d, 56, 470,
        "红、绿两只就是板上的 DS0 / DS1，不用接线；"
        "只把一只蓝色 LED 接到 PB5（P2/P3 引出排针）即可看到完整三色效果。", F14, GRAY)
    img.save(os.path.join(OUT, "03_exp3_rgb.png"))


# =====================================================================
# 4. 实验四：跳线
# =====================================================================
def d4_exp4():
    img, d = new(900, 420)
    title(d, 450, 16, "实验四 · 频率/占空比自测仪接线（一根杜邦线）")

    y = 170
    pinbox(d, 70, y - 45, 230, 90, "PD14", "P5 排针 1 脚\nTIM4_CH3  输出 PWM", (255, 246, 220))
    pinbox(d, 600, y - 45, 230, 90, "PB5", "P2/P3 排针\nTIM3_CH2  输入捕获", (232, 244, 255))

    wire(d, [(300, y - 20), (600, y - 20)], ORANGE, 3)
    center(d, 450, y - 46, "杜邦线直接短接", F16, ORANGE)
    junction(d, 300, y - 20, ORANGE)
    junction(d, 600, y - 20, ORANGE)

    txt(d, 320, y + 8, "PWM 输出 → 捕获输入", F14, GRAY)

    d.rectangle([40, 250, 860, 400], outline=LGRAY, width=2)
    txt(d, 56, 258, "安全说明（实验要求里特别强调的一点）", F16, GREEN)
    txt(d, 56, 284,
        "PB5 在这个模式里配置成 TIM3_CH2 的【复用输入】，不是推挽输出，", F14, GRAY)
    txt(d, 56, 304, "所以和 PD14 直连是安全的，不会出现两个输出脚对拉。", F14, GRAY)
    txt(d, 56, 332,
        "拔掉这根线，程序会在 150ms 内显示「测 无信号」，不会继续显示旧数据。", F14, GRAY)
    txt(d, 56, 360, "测试点：100Hz/25%   、 1kHz/50%   、 10kHz/75%（UP 键切换）", F14, ORANGE)
    img.save(os.path.join(OUT, "04_exp4_jumper.png"))


# =====================================================================
# 5. 实验五：闪光 LED + 急停按键
# =====================================================================
def d5_exp5():
    img, d = new(940, 560)
    title(d, 470, 16, "实验五 · N 次闪光与硬件急停接线（TIM1）")

    # ---- 左：闪光 LED ----
    x = 240
    ytop = 90
    txt(d, x, ytop - 26, "3.3V", F18, RED)
    wire(d, [(x, ytop), (x, ytop + 25)], RED, 3)
    resistor(d, x, ytop + 44, "1K")
    wire(d, [(x, ytop + 55), (x, ytop + 88)])
    led(d, x, ytop + 106, ORANGE, 16)
    wire(d, [(x, ytop + 122), (x, ytop + 180)])
    pinbox(d, x - 95, ytop + 180, 190, 56, "PE9", "P5 排针 7 脚\nTIM1_CH1 输出", (255, 246, 220))

    # ---- 右：急停按键 ----
    bx = 660
    txt(d, bx, ytop - 26, "PB12", F18, BLACK)
    pinbox(d, bx - 95, ytop + 4, 190, 56, "PB12", "P2/P3 引出排针\nTIM1_BKIN", (232, 244, 255))
    wire(d, [(bx, ytop + 60), (bx, ytop + 110)], BLACK, 2)

    # 按键符号
    d.ellipse([bx - 34, ytop + 110, bx + 34, ytop + 178], outline=BLACK, width=2, fill=BG)
    wire(d, [(bx - 34, ytop + 144), (bx - 12, ytop + 144)], BLACK, 2)
    wire(d, [(bx + 12, ytop + 144), (bx + 34, ytop + 144)], BLACK, 2)
    wire(d, [(bx - 12, ytop + 144), (bx + 12, ytop + 144)], BLACK, 3)
    txt(d, bx + 46, ytop + 132, "急停按键", F14)

    wire(d, [(bx, ytop + 178), (bx, ytop + 215)], BLACK, 2)
    wire(d, [(bx - 40, ytop + 215), (bx + 40, ytop + 215)], BLACK, 3)
    for i in range(3):
        wire(d, [(bx - 26 + i * 26, ytop + 215), (bx - 34 + i * 26, ytop + 228)], BLACK, 2)
    txt(d, bx, ytop + 232, "GND", F16, BLACK, anchor="ma")

    d.rectangle([40, 400, 900, 545], outline=LGRAY, width=2)
    txt(d, 56, 408, "为什么这两个必须外接？", F16)
    txt(d, 56, 434,
        "TIM1_CH1 在 F429 上只能落在 PE9 / PA8 / PA9 / PA10 / PA11 / PE13，"
        "没有一个是板载 LED；", F14, GRAY)
    txt(d, 56, 456,
        "TIM1_BKIN 只能落在 PA6 / PB12 / PE15，也没有一个是板载按键。", F14, GRAY)
    txt(d, 56, 486, "急停是硬件动作：一按下去，硬件立刻清 MOE，LED 立即熄灭，"
                    "松手也不会自动恢复，", F14, GRAY)
    txt(d, 56, 506, "必须按板上的 K2 键确认后才重新使能输出。", F14, GRAY)
    img.save(os.path.join(OUT, "05_exp5_flash_bkin.png"))


# =====================================================================
# 6. 总览
# =====================================================================
def d6_overview():
    rows = [
        ("实验一", "OLED 显示", "不用接线", "OLED 模块直接插板上 P6", GREEN),
        ("实验二", "反应速度", "不用接线", "提示灯 = 板载 DS0（红色）", GREEN),
        ("实验三", "三色呼吸", "外接 1 只蓝灯 → PB5", "红=板载DS0  绿=板载DS1", ORANGE),
        ("实验四", "频率占空比", "跳线 PD14 与 PB5 短接", "一根杜邦线短接", ORANGE),
        ("实验五", "N次闪光急停", "LED → PE9；按键 → PB12+GND", "两处都要外接", ORANGE),
    ]
    RH = 62
    img, d = new(960, 150 + RH * len(rows))
    title(d, 480, 16, "五个实验接线总览")

    x0, y0 = 50, 80
    d.rectangle([x0, y0, x0 + 860, y0 + RH * len(rows)], outline=BLACK, width=2)
    for i, (a, b, c, e, col) in enumerate(rows):
        y = y0 + i * RH
        d.line([(x0, y), (x0 + 860, y)], fill=LGRAY, width=1)
        d.line([(x0 + 110, y), (x0 + 110, y + RH)], fill=LGRAY, width=1)
        d.line([(x0 + 250, y), (x0 + 250, y + RH)], fill=LGRAY, width=1)
        d.line([(x0 + 560, y), (x0 + 560, y + RH)], fill=LGRAY, width=1)
        d.rectangle([x0 + 8, y + 14, x0 + 24, y + 48], fill=col)
        txt(d, x0 + 34, y + 20, a, F16)
        txt(d, x0 + 124, y + 20, b, F16)
        txt(d, x0 + 264, y + 20, c, F14, col)
        txt(d, x0 + 574, y + 20, e, F14, GRAY)

    txt(d, x0, y0 + RH * len(rows) + 14,
        "绿色 = 不用接线；橙色 = 需要接线。全部外接 LED 都要串 1K 限流电阻，低电平点亮。",
        F14, GRAY)
    img.save(os.path.join(OUT, "06_overview.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    d1_led_basic()
    d2_header()
    d3_exp3()
    d4_exp4()
    d5_exp5()
    d6_overview()
    for f in sorted(os.listdir(OUT)):
        print("  ", f, os.path.getsize(os.path.join(OUT, f)), "bytes")

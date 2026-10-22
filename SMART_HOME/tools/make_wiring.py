# -*- coding: utf-8 -*-
"""
SMART_HOME / FACE_LAB 接线示意图生成器（PIL 绘制，原理图风格）

用法（在工程根目录下执行）：
    py tools\\make_wiring.py
输出：tools/wiring/*.png

本脚本会按"自己所在工程的目录名"自动选择要画的图：
    ...\\SMART_HOME\\tools\\make_wiring.py  -> 实验三（智能家居）那一套
    ...\\FACE_LAB\\tools\\make_wiring.py    -> 实验四（人脸识别）那一套
两者共用的图（供电/BOOT/DAP、RGB 屏、摄像头）代码只写一份。

绘图约定（与 TIMER_LAB 的 make_wiring.py 保持一致）：
    绿色 = 不用接线；橙色 = 需要动手；红色 = 电源/危险；蓝色 = 输入型信号
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.basename(os.path.dirname(HERE))          # SMART_HOME 或 FACE_LAB
OUT = os.path.join(HERE, "wiring")

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
CARD = (240, 245, 255)
WARN = (255, 246, 220)


def new(w, h):
    img = Image.new("RGB", (w, h), BG)
    return img, ImageDraw.Draw(img)


def txt(d, x, y, s, font=F16, fill=BLACK, anchor=None):
    d.text((x, y), s, font=font, fill=fill, anchor=anchor)


def center(d, cx, y, s, font=F16, fill=BLACK):
    d.text((cx, y), s, font=font, fill=fill, anchor="ma")


def wire(d, pts, color=BLACK, w=2):
    d.line(pts, fill=color, width=w)


def title(d, cx, y, s):
    center(d, cx, y, s, F22, BLACK)
    d.line([(40, y + 30), (cx * 2 - 40, y + 30)], fill=LGRAY, width=2)


def box(d, x0, y0, x1, y1, outline=LGRAY, width=2, fill=None):
    d.rectangle([x0, y0, x1, y1], outline=outline, width=width, fill=fill)


def note(d, x, y, lines, head=None, color=GRAY):
    """底部说明框；lines 为若干行文字"""
    h = 30 + 22 * (len(lines) + (1 if head else 0)) + 10
    box(d, 40, y, 40 + 880, y + h)
    yy = y + 8
    if head:
        txt(d, 56, yy, head, F16, GREEN)
        yy += 24
    for l in lines:
        txt(d, 56, yy, l, F14, color)
        yy += 22
    return y + h


def table(d, x0, y0, widths, rows, rh=28, head_bg=None):
    """rows[0] 为表头；widths 为各列宽度列表"""
    total = sum(widths)
    box(d, x0, y0, x0 + total, y0 + rh * len(rows), BLACK, 2)
    x = x0
    for w in widths[:-1]:
        x += w
        d.line([(x, y0), (x, y0 + rh * len(rows))], fill=LGRAY, width=1)
    for i, row in enumerate(rows):
        y = y0 + i * rh
        if i:
            d.line([(x0, y), (x0 + total, y)], fill=LGRAY, width=1)
        cx = x0
        for j, cell in enumerate(row):
            if isinstance(cell, tuple):
                s, col = cell
            else:
                s, col = cell, BLACK
            f = F16 if i == 0 else F14
            txt(d, cx + 6, y + (6 if i == 0 else 5), s, f, col)
            cx += widths[j]
    return y0 + rh * len(rows)


def pinbox(d, x, y, w, h, name, sub="", fill=CARD):
    box(d, x, y, x + w, y + h, BLACK, 2, fill)
    txt(d, x + 8, y + 5, name, F16)
    if sub:
        for i, line in enumerate(sub.split("\n")):
            txt(d, x + 8, y + 26 + i * 18, line, F14, GRAY)


def power_led(d, cx, cy):
    """电源符号：向下的红色箭头 + 标签"""
    wire(d, [(cx, cy), (cx, cy + 25)], RED, 3)
    txt(d, cx, cy - 26, "DC 6~15V", F18, RED, anchor="ma")


def led(d, cx, cy, color=RED, size=15):
    d.polygon([(cx - size, cy - size), (cx + size, cy - size), (cx, cy + size)],
              outline=color, fill=BG)
    d.line([(cx - size, cy + size), (cx + size, cy + size)], fill=color, width=3)


def resistor(d, cx, cy, label="1K", w=46, h=22):
    box(d, cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2, BLACK, 2, BG)
    center(d, cx, cy - 9, label, F14, BLACK)


def key_sym(d, cx, cy, r=30):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=BLACK, width=2, fill=BG)
    wire(d, [(cx - r, cy), (cx - 10, cy)], BLACK, 2)
    wire(d, [(cx + 10, cy), (cx + r, cy)], BLACK, 2)
    wire(d, [(cx - 10, cy), (cx + 10, cy)], BLACK, 3)


def gnd(d, cx, y, w=40):
    wire(d, [(cx, y - 20), (cx, y)], BLACK, 2)
    wire(d, [(cx - w // 2, y), (cx + w // 2, y)], BLACK, 3)
    for i in range(3):
        wire(d, [(cx - w // 2 + 4 + i * (w // 4), y), (cx - w // 2 + 12 + i * (w // 4), y + 12)],
             BLACK, 2)
    txt(d, cx, y + 16, "GND", F16, BLACK, anchor="ma")


# =====================================================================
# 共用图 1：总览
# =====================================================================
def d_overview():
    if PROJ == "FACE_LAB":
        rows = [
            ("模块", "接到哪里", "怎么接", "备注"),
            ("7 寸 RGB 屏", "板上 RGBLCD 座", "对插，按丝印对齐", "20 根 LTDC + 背光 PB5"),
            ("ATK-OV5640", "板上 P6（OLED/CAM）", "2x8 模块插 2x9 排针", "与 OLED 互斥"),
            ("W25Q256 Flash", "板载（SPI5）", "不用接", "PF6~PF9，板上已连好"),
            ("SDRAM", "板载（FMC）", "不用接", "帧缓冲/图库/模型都在这里"),
            ("双色 LED", "板载 DS0/DS1", "不用接", "绿灯=通过，红灯=不通过"),
            ("4 个按键", "板载", "不用接", "K0 拍照 / K2 换模式"),
            ("仿真器", "板上 20 针 JTAG/SWD 座", "对插实验箱的 DAP", "CMSIS-DAP，选 SWD 模式"),
        ]
        concl = "结论：本工程【一根杜邦线都不需要】，插上屏、摄像头和 DAP 就能跑。"
    else:
        rows = [
            ("模块", "接到哪里", "怎么接", "备注"),
            ("7 寸 RGB 屏", "板上 RGBLCD 座", "对插，按丝印对齐", "20 根 LTDC + 背光 PB5"),
            ("ATK-OV5640", "板上 P6（OLED/CAM）", "2x8 模块插 2x9 排针", "与 OLED 互斥"),
            ("DHT11", "板上单总线座 U10", "对插（或 3 根杜邦线）", "PB12，板上 4.7K 上拉"),
            ("电压表 RV1", "P10 跳线帽", "板上已连，插好跳线帽", "PA5 = ADC1_IN5，0~3.3V"),
            ("SDRAM", "板载（FMC）", "不用接", "帧缓冲 750KiB 放这里"),
            ("双色 LED", "板载 DS0/DS1", "不用接", "绿灯=正常，红灯=越限/故障"),
            ("4 个按键", "板载", "不用接", "K0 换页 / K2 测温湿度"),
            ("仿真器", "板上 20 针 JTAG/SWD 座", "对插实验箱的 DAP", "CMSIS-DAP，选 SWD 模式"),
        ]
        concl = "结论：本工程【一根杜邦线都不需要】——屏、摄像头、DHT11 都是板载座子对插，RV1 靠 P10 跳线帽。"

    img, d = new(980, 170 + 34 * len(rows) + 70)
    title(d, 490, 16, ("实验四 人脸录入与识别" if PROJ == "FACE_LAB" else "实验三 智能家居中控") + " · 接线总览")
    y = table(d, 40, 80, [150, 220, 250, 280], rows, 34)
    txt(d, 56, y + 14, concl, F16, GREEN)
    txt(d, 56, y + 40, "绿色 = 不用接线；橙色 = 需要动手。屏幕与摄像头的 LTDC/DCMI 信号在板上是硬走线，没有飞线的可能。",
        F14, GRAY)
    img.save(os.path.join(OUT, "01_overview.png"))


# =====================================================================
# 共用图 2：供电 / 启动跳线帽 / 仿真器（真机操作）
# =====================================================================
def d_power_boot_dap():
    img, d = new(980, 620)
    title(d, 490, 16, "上电与调试接线（两个实验完全一样）")

    # 左：供电
    box(d, 40, 80, 320, 300)
    txt(d, 56, 90, "① 供电", F18, BLACK)
    txt(d, 56, 118, "开发板总开关 K1（板上丝印 POWER）", F14)
    txt(d, 56, 142, "DC_IN 外接直流：DC 6~15V", F14, RED)
    txt(d, 56, 166, "常用：12V/1A 适配器，", F14, GRAY)
    txt(d, 56, 188, "      或实验箱板载的 DP100 数控电源", F14, GRAY)
    txt(d, 56, 216, "看电源指示灯（PWR LED）：", F14)
    txt(d, 56, 238, "亮 = 上电正常；不亮先查 K1 是否按下", F14, GREEN)
    txt(d, 56, 266, "★ 仿真器不要给开发板供电（手册 2.12）", F14, RED)

    # 中：BOOT 跳线帽
    box(d, 340, 80, 640, 300)
    txt(d, 356, 90, "② 启动跳线帽", F18, BLACK)
    txt(d, 356, 118, "板上 BOOT0 / BOOT1 两颗跳线帽：", F14)
    for i, (n, v, c) in enumerate([("BOOT0", "0（接 GND）", GREEN),
                                   ("BOOT1", "0（接 GND）", GREEN)]):
        y = 148 + i * 60
        box(d, 372, y, 500, y + 44, BLACK, 2, WARN)
        center(d, 436, y + 12, n, F16)
        txt(d, 516, y + 6, "= " + v, F16, c)
        txt(d, 516, y + 26, "正常运行（从 Flash 启动）", F14, GRAY)
    txt(d, 356, 272, "只有用串口 ISP 下载时，才把 BOOT0 拨到 1，下载完再拨回 0", F14, ORANGE)

    # 右：仿真器
    box(d, 660, 80, 940, 300)
    txt(d, 676, 90, "③ 仿真器", F18, BLACK)
    txt(d, 676, 118, "板上【20 针标准 JTAG/SWD 座】", F14)
    txt(d, 676, 142, "← 对插实验箱标配的 DAP 排线", F14, ORANGE)
    txt(d, 676, 170, "调试器务必选【SWD 模式】", F16, RED)
    txt(d, 676, 198, "原因：摄像头占用了 PB3/PB4/PA15，", F14, GRAY)
    txt(d, 676, 220, "      它们同时是 JTAG 的 JTDO/NJTRST/JTDI，", F14, GRAY)
    txt(d, 676, 242, "      只有 SWD（PA13/PA14）能空出来。", F14, GRAY)
    txt(d, 676, 270, "板上另有 P1（5P SWD 排针，默认未焊）", F14, GRAY)

    y = note(d, 40, 320, [
        "1) 屏、摄像头都插好后，再按 K1 上电；带电插拔 P6 有可能打坏 DCMI 输入脚。",
        "2) 上电后程序会先做 SDRAM 自检，失败会直接显示在屏上（屏不亮则先查 P10/背光 → PB5 → LTDC 时序 → SDRAM）。",
        "3) 下载：命令行在本目录 make all 后双击 flash.bat；或用 STM32CubeIDE 导入本目录后 Debug/Download。",
        "4) 下载完按板上 RESET（或重新上电），进入主界面；按键操作见各工程 Readme.txt。",
    ], head="上电顺序与自检")
    img.save(os.path.join(OUT, "02_power_boot_dap.png"))


# =====================================================================
# 共用图 3：7 寸 RGB 屏
# =====================================================================
def d_rgblcd():
    img, d = new(980, 640)
    title(d, 490, 16, "7 寸 800x480 RGB 屏 · 对插板上 RGBLCD 座")

    # 左：屏模块示意
    box(d, 60, 90, 420, 420, BLACK, 2, CARD)
    center(d, 240, 100, "7 寸 RGB 屏模块", F18)
    for i in range(6):
        box(d, 90 + i * 52, 140, 90 + i * 52 + 44, 300, GRAY, 1)
    txt(d, 90, 312, "2x20 排针（LTDC 信号 + 背光）", F14, GRAY)
    txt(d, 90, 340, "缺口/防呆方向按丝印对齐", F16, ORANGE)
    txt(d, 90, 370, "插反 = 没显示（不一定会烧）", F14, GRAY)

    # 右：板座
    box(d, 520, 90, 920, 420, BLACK, 2, WARN)
    center(d, 720, 100, "阿波罗 V2 板上 RGBLCD 座", F18)
    txt(d, 544, 140, "板上丝印：RGBLCD（与 TFTLCD 共用一组", F14)
    txt(d, 544, 162, "LTDC 信号线）", F14)
    txt(d, 544, 196, "20 根 LTDC 信号 + 1 根背光控制 PB5", F16, ORANGE)
    txt(d, 544, 232, "★ 这些信号在 IO 分配表里标注为", F14, RED)
    txt(d, 544, 254, "  「未引出，不能做普通 IO 用」", F14, RED)
    txt(d, 544, 276, "  → 只能对插，不存在飞线的可能", F14, RED)
    txt(d, 544, 310, "背光：PB5（普通 GPIO，高电平点亮）", F14)
    txt(d, 544, 340, "屏不亮的排查顺序见 WIRING.txt 第 9 节", F14, GRAY)

    y = note(d, 40, 440, [
        "R3~R6=PH9 PH10 PH11 PH12   R7=PG6        G2~G4=PH13 PH14 PH15   G5~G7=PI0 PI1 PI2",
        "B3=PG11   B4~B7=PI4 PI5 PI6 PI7          CLK=PG7(30MHz)  DE=PF10  VSYNC=PI9  HSYNC=PI10",
        "面板只引出高 5/6/5 位（就是 RGB565），所以本工程用 RGB565，一帧 750KiB。",
        "行/场同步低有效、DE 高有效、上升沿采样——这四位写在 bsp_lcd.c 的 LTDC 初始化里，换屏先核对这里。",
    ], head="LTDC 信号与引脚（全部 AF14）")
    img.save(os.path.join(OUT, "03_rgblcd.png"))


# =====================================================================
# 共用图 4：摄像头
# =====================================================================
def d_camera():
    img, d = new(980, 660)
    title(d, 490, 16, "ATK-OV5640 摄像头 · 对插板上 P6（丝印 OLED/CAMERA）")

    box(d, 60, 90, 460, 300, BLACK, 2, CARD)
    center(d, 260, 100, "板上 P6 排针（2x9）", F18)
    txt(d, 76, 140, "3V3 / GND 在这一端 →", F16, RED)
    for i in range(9):
        box(d, 90 + i * 38, 170, 90 + i * 38 + 30, 250, GRAY, 1)
    txt(d, 76, 262, "2x8 的摄像头模块插 2x9 的排针，", F14, ORANGE)
    txt(d, 76, 282, "另一端空 2 个孔是正常的", F14, ORANGE)

    box(d, 520, 90, 920, 300, BLACK, 2, CARD)
    center(d, 720, 100, "ATK-OV5640 模块", F18)
    txt(d, 540, 140, "自带 24MHz 有源晶振", F16, GREEN)
    txt(d, 540, 168, "→ 主控【不驱动 PA8(DCMI_XCLK)】", F14, GRAY)
    txt(d, 540, 196, "→ 正好避开 LF0038 红外接收头", F14, GRAY)
    txt(d, 540, 228, "SCCB：SCL=PB4  SDA=PB3（软件模拟，", F14)
    txt(d, 540, 250, "板上已有 4.7K 上拉，约 200kHz）", F14)
    txt(d, 540, 278, "复位 RST=PA15（低有效，板上 10K 上拉）", F14)

    y = note(d, 40, 320, [
        "D0~D4=PC6 PC7 PC8 PC9 PC11   D5=PD3   D6/D7=PB8 PB9",
        "PCLK=PA6（模块自己出）  HREF=PH8  VSYNC=PB7   —— 全部 AF13",
        "P6 是「一座两用」：实验一/二的 SSD1306 OLED 用的正是这组脚（PB7=CS PB4=RS PH8=WR PB3=RD PA15=RST），",
        "所以【OLED 与摄像头不能同时插】。本工程要把 OLED 拔下来再插摄像头。",
        "★ 插拔摄像头前先断电；排线/排针要对齐，插歪可能让 DCMI 输入脚长期对拉。",
    ], head="DCMI 信号（全部 AF13）与注意事项")
    img.save(os.path.join(OUT, "04_camera_p6.png"))


# =====================================================================
# 共用图 5：板载 LED 与按键
# =====================================================================
def d_led_key():
    is_face = (PROJ == "FACE_LAB")
    rows = [
        ("丝印", "引脚", "有效电平", "本工程作用"),
        ("KEY0 / K0", "PH3", "低", "拍照（录入一张 / 识别一次）" if is_face else "切换页面（首页 <-> 摄像头页）"),
        ("KEY1 / K1", "PH2", "低", "删除该人最后一张 / 切换算法" if is_face else "摄像头页：重新初始化 OV5640"),
        ("KEY2 / K2", "PC13", "低", "在四个模式间循环切换" if is_face else "立即测温湿度一次"),
        ("WK_UP / UP", "PA0", "高", "选人 / 选真值" if is_face else "同 K0（切换页面）"),
    ]
    img, d = new(980, 560)
    title(d, 490, 16, "板载双色 LED 与 4 个按键（都不用接线）")

    box(d, 40, 80, 940, 250, BLACK, 2, BG)
    txt(d, 56, 90, "双色 LED（依据硬件手册 LED 原理图）", F18)
    x = 200
    for i, (name, pin, col) in enumerate([("DS1 绿", "PB0", GREEN), ("DS0 红", "PB1", RED)]):
        cx = x + i * 380
        txt(d, cx, 130, "VCC3.3", F16, RED, anchor="ma")
        wire(d, [(cx, 150), (cx, 170)], RED, 3)
        resistor(d, cx, 184, "510R")
        wire(d, [(cx, 195), (cx, 215)], BLACK, 2)
        led(d, cx, 232, col, 15)
        wire(d, [(cx, 247), (cx, 262)], BLACK, 2)
        txt(d, cx, 268, pin + " → " + name, F16, col, anchor="ma")
    txt(d, 56, 296, "电流从 VCC 经 510Ω、经 LED 流入 MCU 引脚（灌电流）→ 【输出低电平点亮，高电平熄灭】",
        F14, GRAY)
    if is_face:
        txt(d, 56, 320, "本工程状态：识别【通过】→ 绿灯亮红灯灭；【不通过】→ 红灯亮绿灯灭（跟随当前选中算法）",
            F14, GREEN)
    else:
        txt(d, 56, 320, "本工程状态：正常 → 绿灯常亮；越限 → 红灯 2Hz 闪；故障 → 红灯常亮（上电默认两灯都灭）",
            F14, GREEN)

    table(d, 40, 370, [200, 120, 160, 400], rows, 30)
    txt(d, 56, 370 + 30 * len(rows) + 12, "消抖：10ms 扫一次、连续稳定 20ms 才认，只在按下边沿产生一次事件。",
        F14, GRAY)
    img.save(os.path.join(OUT, "05_led_key.png"))


# =====================================================================
# 实验三专属：DHT11 与 RV1
# =====================================================================
def d_dht11():
    img, d = new(980, 520)
    title(d, 490, 16, "DHT11 温湿度 · 板上单总线座 U10（PB12）")

    box(d, 60, 90, 460, 330, BLACK, 2, CARD)
    center(d, 260, 100, "方案 A：板载单总线座 U10", F18, GREEN)
    txt(d, 76, 140, "IO 分配表：U10 = 1WIRE_DQ，", F14)
    txt(d, 76, 162, "4 个镀金排孔，支持 DHT11 / DS18B20", F14)
    txt(d, 76, 196, "模块直接对插即可，注意 3V3/GND 方向", F16, ORANGE)
    txt(d, 76, 232, "PB12 板上已有 4.7K 上拉，", F14, GRAY)
    txt(d, 76, 254, "不要再外加一个上拉电阻", F14, GRAY)
    txt(d, 76, 288, "PB12 同时接 PCF8574 的 INT（开漏+上拉），", F14, GRAY)
    txt(d, 76, 310, "本工程不用 PCF8574，不会互相干扰", F14, GRAY)

    box(d, 520, 90, 920, 330, BLACK, 2, WARN)
    center(d, 720, 100, "方案 B：3 根杜邦线引出的模块", F18, ORANGE)
    for i, (a, b, c) in enumerate([("VCC", "3.3V（不要接 5V）", RED),
                                   ("DQ", "PB12（P2/P3 排针）", BLUE),
                                   ("GND", "GND", BLACK)]):
        y = 150 + i * 52
        box(d, 550, y, 660, y + 40, BLACK, 2, BG)
        center(d, 605, y + 10, a, F16)
        wire(d, [(660, y + 20), (700, y + 20)], c, 2)
        txt(d, 710, y + 10, b, F14, c)

    note(d, 40, 350, [
        "代码把 PB12 配成【开漏输出 + 上拉】拉低，读的时候切成【输入 + 上拉】，全程不和从机对拉（不能用推挽）。",
        "时序：主机拉低 20ms（>=18ms）→ 从机 80us 应答 → 40 位数据（每位 = 50us 低 + 26~28us 判 0 / 70us 判 1）。",
        "读取期间要关中断，所以中断服务里不能写长逻辑（本工程中断只有 SysTick / DCMI / DMA，回调里只置标志）。",
        "采样周期 CFG_DHT_PERIOD_MS = 2000ms，另外按 K2 可以随时手动测一次。",
    ], head="电气与软件要点")
    img.save(os.path.join(OUT, "06_dht11_u10.png"))


def d_rv1():
    img, d = new(980, 540)
    title(d, 490, 16, "电子电压表 · 板载 RV1 → P10 跳线帽 → PA5(ADC1_IN5)")

    txt(d, 200, 110, "板载电位器 RV1", F18, BLACK, anchor="ma")
    box(d, 110, 150, 290, 250, BLACK, 2, CARD)
    center(d, 200, 180, "分压点", F16)
    txt(d, 120, 210, "旋转改变电压", F14, GRAY)

    wire(d, [(290, 200), (400, 200)], ORANGE, 3)
    box(d, 400, 160, 560, 240, BLACK, 2, WARN)
    center(d, 480, 176, "P10 跳线帽", F18, ORANGE)
    txt(d, 410, 204, "必须插好！", F16, RED)

    wire(d, [(560, 200), (680, 200)], ORANGE, 3)
    pinbox(d, 680, 160, 240, 80, "PA5  (ADC1_IN5)", "模拟输入，0~3.3V")

    note(d, 40, 290, [
        "1) 通路上是板上既定的：RV1 的分压点经 P10 跳线帽送到 PA5 —— 本工程不需要外接线。",
        "2) 拔掉 P10 跳线帽，PA5 就「完全独立」（IO 分配表原话），此时读数悬空、测不到 RV1。",
        "3) 量程只有 0~3.3V，【绝对不能接 5V 或未知电压】。",
        "4) PA5 配成【模拟输入】，不能开上拉/下拉（会改变 RV1 的分压比）。",
        "5) 换算：V(mV) = raw x 3300 / 4095；采样 480 个 ADC 周期（最长档）+ 16 次平均。",
        "6) IO 分配表还提示 PA5 兼作 TPAD 检测脚 —— 本工程不用 TPAD。",
    ], head="操作要点")
    img.save(os.path.join(OUT, "07_rv1_p10.png"))


# =====================================================================
# 实验四专属：板载 SPI Flash
# =====================================================================
def d_flash():
    img, d = new(980, 560)
    title(d, 490, 16, "板载 W25Q256 SPI NOR Flash（32MB）· 板上已连好，不需要接线")

    table(d, 40, 80, [160, 140, 200, 420], [
        ("信号", "MCU 引脚", "板上连到", "IO 分配表原话 / 说明"),
        ("CS（片选）", "PF6", "W25Q256 片选", "「PF6 接 W25Q256 的片选信号，不建议做普通 IO 使用」"),
        ("SCK", "PF7", "SPI5_SCK", "SPI5，AF5，APB2 90MHz / 4 = 22.5MHz"),
        ("MISO", "PF8", "SPI5_MISO", "8 位、模式 0、MSB 先出"),
        ("MOSI", "PF9", "SPI5_MOSI", "阻塞式收发，未用 DMA"),
    ], 34)

    note(d, 40, 300, [
        "PF6~PF9 与 SDRAM/LTDC/DCMI 的引脚【完全不重叠】，所以加这一路不影响前三套总线。",
        "容量 32MB > 16MB，地址是 25 位 → 上电先发 0xB7 进入【4 字节地址模式】，",
        "之后用 0x13(读) / 0x12(页编程) / 0x21(4KB 扇区擦除) / 0xDC(64KB 块擦除) / 0x9F(读 ID)。",
        "程序上电会读 JEDEC ID 核对（W25Q256=0xEF4019）：ID 不符或读不到会退化成",
        "「仅内存图库」并在屏上提示，此时断电后录入的人脸会丢失。",
        "人脸库写在 Flash 的独立数据区：【从 0x01000000（16MiB）起，占 1MiB】，",
        "刻意避开板上前半部分可能存在的字库等资源，不覆盖已有数据。",
    ], head="为什么不用接线 + 软件要点")
    img.save(os.path.join(OUT, "06_flash_spi5.png"))


def main():
    os.makedirs(OUT, exist_ok=True)
    d_overview()
    d_power_boot_dap()
    d_rgblcd()
    d_camera()
    d_led_key()
    if PROJ == "FACE_LAB":
        d_flash()
    else:
        d_dht11()
        d_rv1()
    print("project:", PROJ)
    for f in sorted(os.listdir(OUT)):
        print("  %-26s %7d bytes" % (f, os.path.getsize(os.path.join(OUT, f))))


if __name__ == "__main__":
    main()

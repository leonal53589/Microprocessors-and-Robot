# -*- coding: utf-8 -*-
"""
OLED 显示离线仿真器
  按 oled.c 里完全相同的取位方式（SSD1306 页格式、byte = page*W + col、
  bit = row & 7）把 app.c 的各个界面重绘成 PNG，
  用于在接硬件之前核对：布局是否越界、字库是否正确、中英混排是否对齐。
"""
import os
import re

from PIL import Image

ROOT = r"D:\STM32Projects\TIMER_LAB"
OUT = r"C:\Users\落尘\Desktop\working space\out"

W, H = 128, 64
SCALE = 3

# ---------------------------------------------------------------- 解析字库
src = open(os.path.join(ROOT, "Inc", "oledfont.h"), encoding="utf-8").read()


def grab(name):
    m = re.search(r"const uint8_t %s\[\] = \{(.*?)\};" % name, src, re.S)
    return [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))]


ASC = grab("oled_asc2_1608")
CN = grab("oled_cn_1616")
CN_IDX = {}
m = re.search(r"const OledCnItem oled_cn_index\[\] = \{(.*?)\};", src, re.S)
for ch, off in re.findall(r'\{"(.)",\s*(\d+)U\}', m.group(1)):
    CN_IDX[ch] = int(off)

print("font: ascii bytes=%d (%d chars), cn bytes=%d (%d glyphs)"
      % (len(ASC), len(ASC) // 16, len(CN), len(CN) // 32))

# ---------------------------------------------------------------- 仿真绘制
class Screen:
    def __init__(self, title):
        self.title = title
        self.g = [[0] * W for _ in range(H)]
        self.overflow = 0

    def px(self, x, y, on):
        if x >= W or y >= H:
            self.overflow += 1
            return
        if on:
            self.g[y][x] = 1

    def ascii(self, x, y, ch):
        if ch < 0x20 or ch > 0x7E:
            ch = 0x20
        base = (ch - 0x20) * 16
        for col in range(8):
            for row in range(16):
                byte = ASC[base + (row >> 3) * 8 + col]
                if x + col >= W:
                    self.overflow += 1
                    continue
                self.px(x + col, y + row, (byte >> (row & 7)) & 1)

    def cn(self, x, y, off):
        for col in range(16):
            for row in range(16):
                byte = CN[off + (row >> 3) * 16 + col]
                if x + col >= W:
                    self.overflow += 1
                    continue
                self.px(x + col, y + row, (byte >> (row & 7)) & 1)

    def string(self, x, y, s):
        i = 0
        while i < len(s):
            c = s[i]
            if ord(c) < 0x80:
                self.ascii(x, y, ord(c))
                x += 8
                i += 1
            else:
                off = CN_IDX.get(c)
                if off is None:
                    self.ascii(x, y, ord("?"))
                else:
                    self.cn(x, y, off)
                x += 16
                i += 1
            if x >= W:
                break

    def integer(self, x, y, v):
        for k, c in enumerate(str(v)):
            self.ascii(x + k * 8, y, ord(c))

    def clear(self):
        self.g = [[0] * W for _ in range(H)]

    def image(self):
        img = Image.new("1", (W, H), 0)
        p = img.load()
        for y in range(H):
            for x in range(W):
                p[x, y] = self.g[y][x]
        return img


LINE_H = 16


def fixed1(sc, x, y, v10):
    """与 display.c 的 Disp_Fixed1 一致（含负号处理）"""
    iv = int(v10 / 10)          # C 除法向零截断
    fr = v10 - iv * 10
    neg = v10 < 0
    if neg:
        iv, fr = -iv, -fr
        sc.string(x, y, "-")
        x += 8
    sc.integer(x, y, iv)
    x += len(str(iv)) * 8
    sc.string(x, y, ".")
    sc.integer(x + 8, y, fr)


# ---------------------------------------------------------------- 各界面
screens = []


def menu(sel):
    names = ["1 OLED显示", "2 反应速度", "3 三色呼吸", "4 频率占空比", "5 N次闪光急停"]
    sc = Screen("menu sel=%d" % sel)
    first = 0 if sel < 3 else sel - 2
    sc.string(0, 0, "定时器实验")
    for i in range(3):
        idx = first + i
        if idx >= len(names):
            break
        sc.string(0, (i + 1) * LINE_H, ">" if idx == sel else " ")
        sc.string(8, (i + 1) * LINE_H, names[idx])
    screens.append(sc)


menu(0)
menu(4)

sc = Screen("exp1 OLED显示")
sc.string(0, 0, "定时器实验")
sc.string(0, 16, "姓名 "); sc.string(40, 16, "蓝彦扬")
sc.string(0, 32, "学号 "); sc.string(40, 32, "2024095004")
sc.string(0, 48, "计数 "); sc.integer(40, 48, 12345)
screens.append(sc)

for state in ("IDLE", "WAIT", "GO", "RESULT", "FALSE"):
    sc = Screen("exp2 " + state)
    sc.string(0, 0, "2 反应速度")
    if state == "IDLE":
        sc.string(0, 16, "按K0开始")
        sc.string(0, 32, "最好 "); sc.integer(40, 32, 215); sc.string(72, 32, "ms")
        sc.string(0, 48, "等红灯亮再按K0")
    elif state == "WAIT":
        sc.string(0, 16, "请等待")
        sc.string(0, 32, "不要抢跑")
        sc.string(0, 48, "等红灯亮再按K0")
    elif state == "GO":
        sc.string(0, 16, "按K0！")
    elif state == "RESULT":
        sc.string(0, 16, "本次 "); sc.integer(40, 16, 253); sc.string(72, 16, "ms")
        sc.string(0, 32, "最好 "); sc.integer(40, 32, 215); sc.string(72, 32, "ms")
        sc.string(0, 48, "按K0再测")
    else:
        sc.string(0, 16, "抢跑！")
        sc.string(0, 32, "等红灯亮再按")
        sc.string(0, 48, "按K0重来")
    screens.append(sc)

for mode in ("同步呼吸", "三色流水", "警灯闪烁"):
    sc = Screen("exp3 " + mode)
    sc.string(0, 0, "3 三色呼吸")
    sc.string(0, 16, mode)
    sc.string(0, 32, "运行中")
    sc.string(0, 48, "UP切换 K0停")
    screens.append(sc)

for tag, freq, duty, meas in (("100Hz/25%", 100, 250, 100),
                              ("1kHz/50%", 1000, 500, 999),
                              ("10kHz/75%", 10000, 750, 10000)):
    sc = Screen("exp4 " + tag)
    sc.string(0, 0, "4 频率占空比")
    sc.string(0, 16, "设 "); sc.integer(24, 16, freq); sc.string(24 + len(str(freq)) * 8, 16, "Hz ")
    fixed1(sc, 24 + len(str(freq)) * 8 + 24, 16, duty)
    sc.string(0, 32, "测 "); sc.integer(24, 32, meas); sc.string(24 + len(str(meas)) * 8, 32, "Hz ")
    fixed1(sc, 24 + len(str(meas)) * 8 + 24, 32, duty)
    err = int((meas - freq) * 1000 / freq)
    sc.string(0, 48, "误 "); fixed1(sc, 24, 48, err)
    w = (1 if err < 0 else 0) + len(str(abs(int(err / 10)))) + 2
    sc.string(24 + w * 8, 48, "%")
    screens.append(sc)

sc = Screen("exp4 无信号")
sc.string(0, 0, "4 频率占空比")
sc.string(0, 16, "设 "); sc.integer(24, 16, 1000); sc.string(24 + 4 * 8, 16, "Hz "); fixed1(sc, 24 + 4 * 8 + 24, 16, 500)
sc.string(0, 32, "测 无信号")
sc.string(0, 48, "请接跳线")
screens.append(sc)

for n, st in ((3, "运行中"), (1, "已停止"), (9, "已刹车 K2确认")):
    sc = Screen("exp5 N=%d %s" % (n, st))
    sc.string(0, 0, "5 N次闪光急停")
    sc.string(0, 16, "次数 N="); sc.integer(56, 16, n)
    sc.string(0, 32, "UP改 K0启动")
    sc.string(0, 48, st)
    screens.append(sc)

# ---------------------------------------------------------------- 拼接输出
COLS = 3
ROWS = (len(screens) + COLS - 1) // COLS
PAD = 6
sheet = Image.new("1", (COLS * (W * SCALE + PAD) + PAD,
                        ROWS * (H * SCALE + PAD) + PAD), 0)
for i, sc in enumerate(screens):
    cx = PAD + (i % COLS) * (W * SCALE + PAD)
    cy = PAD + (i // COLS) * (H * SCALE + PAD)
    sheet.paste(sc.image().resize((W * SCALE, H * SCALE), Image.NEAREST), (cx, cy))

path = os.path.join(OUT, "oled_screens.png")
sheet.save(path)
print("screens: %d -> %s  (%dx%d)" % (len(screens), path, sheet.width, sheet.height))

bad = [s.title for s in screens if s.overflow]
print("OVERFLOW screens:", bad if bad else "none")
